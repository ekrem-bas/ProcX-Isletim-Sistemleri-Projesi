#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>

// Enum
typedef enum
{
    MODE_ATACHED = 0,
    MODE_DETACHED = 1
} ProcessMode;

typedef enum
{
    STATUS_RUNNING = 0,
    STATUS_TERMINATED = 1,
    STATUS_CREATED = 2
} ProcessStatus;

// Veri Yapıları
typedef struct
{
    pid_t pid;            // Process ID
    pid_t owner_pid;      // Başlatan instance'ın PID'si
    char command[256];    // Çalıştırılan komut
    ProcessMode mode;     // Attached (0) veya Detached (1)
    ProcessStatus status; // Running (0) veya Terminated (1)
    time_t start_time;    // Başlangıç zamanı
    int is_active;
} ProcessInfo;

typedef struct
{
    ProcessInfo processes[50]; // Maksimum 50 process
    int process_count;         // Aktif process sayısı
} SharedData;

typedef struct
{
    long msg_type;    // Mesaj tipi
    int command;      // Komut (START/TERMINATE)
    pid_t sender_pid; // Gönderen PID
    pid_t target_pid; // Hedef process PID
} Message;

#define SHM_NAME "/procx_shm"
#define SEM_NAME "/procx_sem"
#define MSG_QUEUE_KEY "/procx_mq"
// GLOBAL DEĞİŞKENLER
SharedData *g_shared_mem = NULL; // Shared memory pointer'ı
sem_t *g_sem = NULL;             // Semafor pointer'ı
int g_mq_id = -1;                // Mesaj kuyruğu ID'si

// IPC kaynaklarını oluşturma fonksiyonu (mesaj kuyruğu, paylaşılan bellek, semafor)
void init_ipc_resources()
{
    int shm_fd;
    int is_first_instance = 0;

    // 1. Shared Memory Oluşturma/Bağlanma
    // O_EXCL flag'i ile "Sadece yoksa oluştur" diyoruz. Başarılı olursa ilk biziz.
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0666);

    if (shm_fd > 0)
    {
        // Dosya yeni oluşturuldu, demek ki ilk instance biziz.
        is_first_instance = 1;
        printf("[DEBUG] İlk instance başlatılıyor, shared memory oluşturuldu.\n");
    }
    else
    {
        if (errno == EEXIST)
        {
            // Zaten var, normal aç
            shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
            printf("[DEBUG] Var olan sisteme bağlanıldı.\n");
        }
        else
        {
            perror("Shared memory açma hatası");
            exit(1);
        }
    }

    // Boyut ayarla
    if (ftruncate(shm_fd, sizeof(SharedData)) == -1)
    {
        perror("ftruncate hatası");
        exit(1);
    }

    // Memory Mapping (Pointer'ı global değişkene ata)
    g_shared_mem = (SharedData *)mmap(NULL, sizeof(SharedData),
                                      PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (g_shared_mem == MAP_FAILED)
    {
        perror("mmap hatası");
        exit(1);
    }

    // Eğer ilk instance ise belleği SIFIRLA (Çöp verileri temizle)
    if (is_first_instance)
    {
        memset(g_shared_mem, 0, sizeof(SharedData));
        g_shared_mem->process_count = 0;
    }

    // 2. Semafor Oluşturma/Bağlanma
    // Başlangıç değeri 1 (Mutex gibi çalışacak)
    g_sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (g_sem == SEM_FAILED)
    {
        perror("Semafor açma hatası");
        exit(1);
    }

    // 3. Message Queue Oluşturma
    // System V Message Queue kullanımı
    key_t key = ftok(MSG_QUEUE_KEY, 65);
    if ((g_mq_id = msgget(key, 0666 | IPC_CREAT)) == -1)
    {
        perror("Message queue oluşturma hatası");
        exit(1);
    }
}

// IPC Kaynaklarını temizleme fonksiyonu
void cleanup_ipc_resources()
{
    // Semaforu kapat
    if (g_sem != NULL)
    {
        sem_close(g_sem);
        sem_unlink(SEM_NAME);
        if (g_sem == SEM_FAILED)
        {
            perror("Semafor kapatma hatası");
        }
    }

    // Shared memory'yi kapat
    if (g_shared_mem != NULL)
    {
        munmap(g_shared_mem, sizeof(SharedData));
        shm_unlink(SHM_NAME);
        if (g_shared_mem == MAP_FAILED)
        {
            perror("Shared memory kapatma hatası");
        }
    }

    // Message queue'yu sil
    if (g_mq_id != -1)
    {
        msgctl(g_mq_id, IPC_RMID, NULL);
        if (msgctl(g_mq_id, IPC_RMID, NULL) == -1)
        {
            perror("Message queue silme hatası");
        }
    }
}

// Monitor Thread fonksiyonu
void *monitor_processes(void *arg)
{
    while (1)
    {
        sleep(2);        // 2 saniyede bir kontrol et
        sem_wait(g_sem); // Kritik bölge başlangıcı
        for (int i = 0; i < g_shared_mem->process_count; i++)
        {
            ProcessInfo *proc = &g_shared_mem->processes[i];
            if (proc->is_active && proc->status == STATUS_RUNNING)
            {
                // Process'in durumunu kontrol et
                pid_t result = waitpid(proc->pid, NULL, WNOHANG);
                if (result == -1)
                {
                    perror("waitpid hatası");
                }
                else if (result > 0) // Process sonlanmış
                {
                    // Process'i shared memory'den kaldır
                    g_shared_mem->processes[i] = g_shared_mem->processes[g_shared_mem->process_count - 1];
                    g_shared_mem->process_count--;
                    printf("[MONITOR] Process %d sonlandı.\n", proc->pid);
                    // IPC ile diğer instance'lara bildirim gönder
                    Message msg;
                    msg.msg_type = 1; // Tüm instance'lara gönder
                    msg.command = STATUS_TERMINATED;
                    msg.sender_pid = getpid();
                    msg.target_pid = proc->pid;
                    send_ipc_message(&msg);
                    i--; // İndeksi geri al çünkü eleman kaydırıldı
                }
            }
        }
        sem_post(g_sem); // Kritik bölge sonu
    }
    return NULL;
}

// IPC Mesajı Gönderme Fonksiyonu
void send_ipc_message(Message *msg)
{
    if (msgsnd(g_mq_id, msg, sizeof(Message) - sizeof(long), 0) == -1)
    {
        perror("Mesaj gönderme hatası");
    }
}

// IPC Listener fonksiyonu
void *ipc_listener(void *arg)
{
    Message msg;
    while (1)
    {
        if (msgrcv(g_mq_id, &msg, sizeof(Message) - sizeof(long), 0, 0) == -1)
        {
            perror("Mesaj alma hatası");
            continue;
        }

        if (msg.sender_pid == getpid())
        {
            continue; // Kendi mesajımızı yoksay
        }

        if (msg.command == STATUS_TERMINATED)
        {
            printf("[IPC] Process sonlandırıldı: PID %d\n", msg.target_pid);
        }
        else if (msg.command == STATUS_CREATED)
        {
            printf("[IPC] Yeni process başlatıldı: PID %d\n", msg.target_pid);
        }
    }
}

#define MAX_ARGS 10 // Bir komut için maksimum argüman sayısı

// Argümanları ayırır ve bir char* dizisine (argv) doldurur.
// Döndürülen değer, bulunan argüman sayısıdır.
int parse_command(char *command, char *argv[])
{
    int count = 0;
    // strtok orijinal string'i değiştirdiği için, burada doğrudan komutun pointer'ını kullanırız.
    char *token = strtok(command, " \t\n"); // Boşluk ve tab karakterlerine göre ayır

    while (token != NULL && count < MAX_ARGS - 1)
    {
        argv[count++] = token;
        token = strtok(NULL, " \t\n");
    }
    argv[count] = NULL; // execvp'nin son argümanı NULL olmalıdır
    return count;
}

void create_new_process(char *command, ProcessMode mode)
{
    char command_for_tokenize[256];
    char command_for_save[256];
    pid_t pid;

    // komutları kopyala
    strncpy(command_for_tokenize, command, sizeof(command_for_tokenize) - 1);
    command_for_tokenize[sizeof(command_for_tokenize) - 1] = '\0';

    strncpy(command_for_save, command, sizeof(command_for_save) - 1);
    command_for_save[sizeof(command_for_save) - 1] = '\0';

    // Yeni process oluştur
    pid = fork();

    if (pid < 0)
    {
        perror("Fork hatası");
        return;
    }

    // --- CHILD PROCESS ---
    else if (pid == 0)
    {

        char *argv[MAX_ARGS];

        // Komutu tokenize et
        int arg_count = parse_command(command_for_tokenize, argv);

        if (arg_count == 0)
        {
            fprintf(stderr, "HATA: Geçersiz veya boş komut.\n");
            exit(EXIT_FAILURE);
        }

        if (mode == MODE_DETACHED)
        {
            if (setsid() < 0)
            {
                perror("setsid hatası");
                exit(EXIT_FAILURE);
            }
        }

        // Programı çalıştır (argv[0] komutun kendisidir)
        execvp(argv[0], argv);

        // Hata durumunda (Komut bulunamazsa)
        perror("execvp hatası (Komut bulunamadı veya çalıştırılamadı)");
        exit(EXIT_FAILURE);
    }

    // --- PARENT PROCESS  ---
    // Shared Memory'ye process bilgisini ekle
    sem_wait(g_sem); // Kilidi al

    int max_processes = sizeof(g_shared_mem->processes) / sizeof(g_shared_mem->processes[0]);
    if (g_shared_mem->process_count >= max_processes)
    {
        fprintf(stderr, "HATA: Shared memory dolu (Maksimum %lu sürece ulaşıldı).\n", max_processes);
        sem_post(g_sem); // Kilidi aç
        return;
    }

    // Process Bilgisini Doldurma
    ProcessInfo *new_proc = &g_shared_mem->processes[g_shared_mem->process_count];

    new_proc->pid = pid;
    new_proc->owner_pid = getpid();
    new_proc->mode = mode;
    new_proc->status = 0; // Running

    // Kaydedilen orijinal komutu kopyala
    strncpy(new_proc->command, command_for_save, sizeof(new_proc->command) - 1);
    new_proc->command[sizeof(new_proc->command) - 1] = '\0';

    new_proc->start_time = time(NULL);
    new_proc->is_active = 1;

    g_shared_mem->process_count++;

    sem_post(g_sem); // Kilidi bırak

    // 4. IPC Bildirimi Gönder
    Message ipc_msg;
    ipc_msg.msg_type = 1;
    ipc_msg.command = STATUS_CREATED;
    ipc_msg.sender_pid = getpid();
    ipc_msg.target_pid = pid;
    send_ipc_message(&ipc_msg);

    printf("[SUCCESS] Process başlatıldı: PID %d (Mod: %s)\n", pid,
           (mode == MODE_DETACHED ? "Detached" : "Attached"));
}

void print_program_output()
{
    printf("==============================\n");
    printf("          ProcX v1.0          \n");
    printf("==============================\n");
    printf("1. Yeni Program Çalıştır\n");
    printf("2. Çalışan Programları Listele\n");
    printf("3. Program Sonlandır\n");
    printf("0. Çıkış\n");
    printf("==============================\n");
}

void print_running_processes(SharedData *data)
{
    printf("Çalışan Programlar:\n");
    printf("PID\tKomut\tMod\tDurum\tBaşlangıç Zamanı\n");
    for (int i = 0; i < data->process_count; i++)
    {
        ProcessInfo *proc = &data->processes[i];
        if (proc->is_active)
        {
            printf(
                "%d\t%s\t%s\t%s\t%s",
                proc->pid,
                proc->command,
                proc->mode == MODE_ATACHED ? "Attached" : "Detached",
                proc->status == STATUS_RUNNING ? "Running" : "Terminated",
                ctime(&proc->start_time));
        }
    }
}

int main(int argc, char const *argv[])
{

    return 0;
}
