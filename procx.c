#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>    // fork, execvp, sleep
#include <fcntl.h>     // O_CREAT, O_EXCL, O_RDWR
#include <sys/mman.h>  // shm_open, mmap, shm_unlink, munmap
#include <sys/stat.h>  // 0666
#include <semaphore.h> // sem_open, sem_wait, sem_post, sem_close, sem_unlink
#include <sys/msg.h>   // msgget, msgsnd, msgrcv
#include <sys/types.h> // pid_t, key_t
#include <errno.h>     // error handling
#include <time.h>      // time
#include <signal.h>    // kill, SIGTERM
#include <pthread.h>   // pthread_create, pthread_join
#include <sys/wait.h>  // waitpid, WNOHANG

#define SHM_NAME "/procx_shm"
#define SEM_NAME "/procx_sem"
#define IPC_KEY_FILE "/tmp/procx_ipc_key"
#define MAX_PROCESSES 50
#define MAX_ARGS 10 // Bir komut için maksimum argüman sayısı

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
    ProcessInfo processes[MAX_PROCESSES]; // Maksimum 50 process
    int process_count;                    // Aktif process sayısı
    int instance_count;                   // Aktif ProcX instance sayısı
} SharedData;

typedef struct
{
    long msg_type;    // Mesaj tipi
    int command;      // Komut (START/TERMINATE)
    pid_t sender_pid; // Gönderen PID
    pid_t target_pid; // Hedef process PID
} Message;

// GLOBAL DEĞİŞKENLER
SharedData *g_shared_mem = NULL; // Shared memory pointer'ı
sem_t *g_sem = NULL;             // Semafor pointer'ı
int g_mq_id = -1;                // Mesaj kuyruğu ID'si

// Fonksiyon prototipleri
void init_ipc_resources();
void disconnect_ipc_resources();
void destroy_ipc_resources();
void clean_exit();
void *monitor_processes(void *arg);
void *ipc_listener(void *arg);
void send_ipc_message(Message *msg);
int parse_command(char *command, char *argv[]);
void create_new_process(char *command, ProcessMode mode);
void terminate_process(pid_t target_pid);
void print_program_output();
void print_running_processes(SharedData *data);
void repaint_ui(const char *message);

// IPC kaynaklarını oluşturma fonksiyonu (mesaj kuyruğu, paylaşılan bellek, semafor)
void init_ipc_resources()
{
    int shm_fd;
    int is_first_instance = 0;

    /*
    Shared Memory oluşturma/bağlanma
    O_CREAT : Eğer yoksa oluştur
    O_EXCL  : Eğer zaten varsa hata ver
    O_RDWR  : Okuma/Yazma izni
    0666    : İzinler (okuma/yazma herkes için)
    */
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0666);

    // Shared memory oluştuysa >= 0 döner
    if (shm_fd >= 0)
    {
        // Dosya yeni oluşturuldu, demek ki ilk instance biziz.
        is_first_instance = 1;
        // Boyut ayarla
        if (ftruncate(shm_fd, sizeof(SharedData)) == -1)
        {
            perror("ftruncate hatası");
            shm_unlink(SHM_NAME);
            exit(1);
        }
    }
    else
    {
        if (errno == EEXIST)
        {
            // Zaten var, normal aç
            shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
            // Hata kontrolü
            if (shm_fd == -1)
            {
                perror("Shared memory açma hatası");
                exit(1);
            }
        }
        else
        {
            perror("Shared memory açma hatası");
            exit(1);
        }
    }

    // Belleği ilgili pointer'a eşle
    g_shared_mem = (SharedData *)mmap(NULL, sizeof(SharedData),
                                      PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    // Map ettikten sonra dosyayı kapat
    close(shm_fd);
    if (g_shared_mem == MAP_FAILED)
    {
        perror("mmap hatası");
        exit(1);
    }

    // Eğer ilk instance ise belleği sıfırla
    if (is_first_instance)
    {
        memset(g_shared_mem, 0, sizeof(SharedData));
        g_shared_mem->process_count = 0;
    }

    // Semafor oluşturma/bağlanma
    g_sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (g_sem == SEM_FAILED)
    {
        perror("Semafor açma hatası");
        exit(1);
    }

    // Mesaj kuyruğu için IPC key dosyasını oluştur
    int fd = open(IPC_KEY_FILE, O_CREAT | O_RDWR, 0666);
    if (fd == -1)
    {
        perror("IPC Key dosyası oluşturulamadı");
        exit(1);
    }
    close(fd);

    // Key oluştur
    key_t key = ftok(IPC_KEY_FILE, 65);
    if (key == -1)
    {
        perror("ftok hatası");
        exit(1);
    }

    // Mesaj kuyruğu oluşturma/baglanma
    if ((g_mq_id = msgget(key, 0666 | IPC_CREAT)) == -1)
    {
        perror("Message queue oluşturma hatası");
        exit(1);
    }

    sem_wait(g_sem);
    // Instance sayısını artır
    g_shared_mem->instance_count++;
    sem_post(g_sem);
}

// IPC kaynaklarından bağlantıyı kesme fonksiyonu
void disconnect_ipc_resources()
{
    if (g_shared_mem != NULL)
    {
        munmap(g_shared_mem, sizeof(SharedData));
    }
    if (g_sem != NULL)
    {
        sem_close(g_sem);
    }
}

// Son instance için IPC kaynaklarını yok etme fonksiyonu
void destroy_ipc_resources()
{
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_NAME);
    msgctl(g_mq_id, IPC_RMID, NULL);
}

// Instance için çıkış fonksiyonu
void clean_exit()
{
    // Kendi başlattığımız Attached Process'leri öldür ve bildir
    if (g_sem != NULL && g_shared_mem != NULL)
    {
        sem_wait(g_sem);

        for (int i = 0; i < g_shared_mem->process_count; i++)
        {
            ProcessInfo *proc = &g_shared_mem->processes[i];

            // Sadece attached ve kendi başlattıklarımızı öldür
            if (proc->is_active && proc->owner_pid == getpid() && proc->mode == MODE_ATACHED)
            {
                // Processi kill et
                if (kill(proc->pid, SIGTERM) == 0)
                {
                    // Shared Memory'de durumu güncelle
                    proc->is_active = 0;
                    proc->status = STATUS_TERMINATED;

                    // Diğer Terminallere IPC Mesajı Gönder
                    Message msg;
                    msg.msg_type = 1;
                    msg.command = STATUS_TERMINATED;
                    msg.sender_pid = getpid();
                    msg.target_pid = proc->pid;

                    // Mesaj gönderme (msgsnd)
                    if (g_mq_id != -1)
                    {
                        send_ipc_message(&msg);
                    }
                }
            }
        }

        // Sayacı azalt ve sonuncu instance mıyım kontrol et
        g_shared_mem->instance_count--;
        int remaining_instances = g_shared_mem->instance_count;

        sem_post(g_sem);

        // IPC kaynaklarını temizleme işlemleri
        if (remaining_instances <= 0)
        {
            destroy_ipc_resources(); // Sonuncusaysan her şeyi sil
        }
        else
        {
            disconnect_ipc_resources(); // Değilsen sadece bağlantını kes
        }
    }
    exit(0);
}

// Monitor Thread fonksiyonu
void *monitor_processes(void *arg)
{
    (void)arg; // Makefile unused parameter warning go away
    char buffer[256];
    while (1)
    {
        sleep(2); // 2 saniyede bir kontrol et

        if (g_shared_mem == NULL || g_sem == NULL)
            continue;

        sem_wait(g_sem);

        for (int i = 0; i < g_shared_mem->process_count; i++)
        {
            ProcessInfo *proc = &g_shared_mem->processes[i];
            int should_clean = 0; // Silinmeli mi bayrağı

            // Sadece aktif olanları kontrol et
            if (proc->is_active)
            {
                // Durum 1: Benim başlattığım süreç (Parent-Child)
                if (proc->owner_pid == getpid())
                {
                    int status;
                    // WNOHANG: Process bitmediyse bekleme yapma, hemen dön
                    pid_t result = waitpid(proc->pid, &status, WNOHANG);

                    if (result > 0) // Process sonlandı
                    {
                        should_clean = 1;
                    }
                }
                // Durum 2: Başkasının başlattığı süreç (Detached veya Sahibi Ölmüş)
                else
                {
                    // kill(pid, 0) sinyal göndermez, sadece varlığı kontrol eder.
                    // Eğer -1 dönerse ve errno == ESRCH ise, böyle bir process YOKTUR.
                    if (kill(proc->pid, 0) == -1 && errno == ESRCH)
                    {
                        should_clean = 1; // Process sistemde yok, listeden silmeliyiz
                    }
                }

                // Eğer process silinmeli ise
                if (should_clean)
                {
                    snprintf(buffer, sizeof(buffer), "[MONITOR] Process %d sonlandı.", proc->pid);

                    repaint_ui(buffer);

                    // IPC Mesajı Gönder
                    Message msg;
                    msg.msg_type = 1;
                    msg.command = STATUS_TERMINATED;
                    msg.sender_pid = getpid();
                    msg.target_pid = proc->pid;

                    send_ipc_message(&msg);

                    // Shared Memory'den sil (Kaydırma Yöntemi)
                    g_shared_mem->processes[i] = g_shared_mem->processes[g_shared_mem->process_count - 1];
                    g_shared_mem->process_count--;

                    // İndeksi düzelt (Kaydırdığımız elemanı atlamamak için)
                    i--;
                }
            }
        }
        sem_post(g_sem);
    }
    return NULL;
}

// IPC Mesajı Gönderme Fonksiyonu
void send_ipc_message(Message *msg)
{
    // Sistemdeki toplam instance (terminal) sayısını al
    int total_instances = g_shared_mem->instance_count;

    // Kendim dahil tüm instance'lara mesaj gönder
    for (int i = 0; i < total_instances; i++)
    {
        if (msgsnd(g_mq_id, msg, sizeof(Message) - sizeof(long), 0) == -1)
        {
            perror("Mesaj gönderme hatası");
        }
    }
}

// IPC Listener fonksiyonu
void *ipc_listener(void *arg)
{
    (void)arg; // Makefile unused parameter warning go away
    Message msg;
    char buffer[256];
    while (1)
    {
        // Mesajı bekle
        if (msgrcv(g_mq_id, &msg, sizeof(Message) - sizeof(long), 0, 0) == -1)
        {
            if (errno == EIDRM || errno == EINVAL)
                break;
            perror("Mesaj alma hatası");
            continue;
        }

        // Kendi mesajım geldiyse yut
        if (msg.sender_pid == getpid())
        {
            // Diğer terminallerin mesajı kapabilmesi için sıramı savıyorum.
            usleep(50000); // 50ms bekle
            continue;
        }

        // Başkasından gelen mesajı işle
        if (msg.command == STATUS_TERMINATED)
        {
            snprintf(buffer, sizeof(buffer), "[IPC] Process sonlandırıldı: PID %d", msg.target_pid);
        }
        else if (msg.command == STATUS_CREATED)
        {
            snprintf(buffer, sizeof(buffer), "[IPC] Yeni process başlatıldı: PID %d", msg.target_pid);
        }

        // Ekrana bas
        repaint_ui(buffer);

        // Açgözlülük Önleme:
        // Mesajı aldım, işimi bitirdim. Kuyrukta başka kopya varsa
        // onları diğer terminaller alsın diye biraz bekliyorum.
        usleep(50000); // 50ms bekle
    }
    return NULL;
}

// Argümanları ayırır ve bir char* dizisine (argv) doldurur.
// Döndürülen değer, bulunan argüman sayısıdır.
int parse_command(char *command, char *argv[])
{
    int count = 0;
    char *token = strtok(command, " \t\n"); // Boşluk ve tab karakterlerine göre ayır

    while (token != NULL && count < MAX_ARGS - 1)
    {
        argv[count++] = token;
        token = strtok(NULL, " \t\n");
    }
    argv[count] = NULL; // execvp'nin son argümanı NULL olmalıdır
    return count;
}

// Yeni process oluşturma fonksiyonu
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

    // Önce child process oluştu mu oluşmadı mı kontrol et
    usleep(1000); // Kısa bir bekleme

    int status;
    pid_t result = waitpid(pid, &status, WNOHANG);
    if (result < 0)
    {
        // Child process hemen sonlandı, demek ki execvp başarısız oldu
        // Shared memory'e ekleme yapma
        fprintf(stderr, "HATA: Process başlatılamadı. Komut hatası veya bulunamadı.\n");
        return;
    }

    // Shared Memory'ye process bilgisini ekle
    sem_wait(g_sem); // Kilidi al

    int max_processes = sizeof(g_shared_mem->processes) / sizeof(g_shared_mem->processes[0]);
    if (g_shared_mem->process_count >= max_processes)
    {
        fprintf(stderr, "HATA: Shared memory dolu (Maksimum %d sürece ulaşıldı).\n", max_processes);
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

// Process'i sonlandırma fonksiyonu
void terminate_process(pid_t target_pid)
{
    // Sinyal gönder
    if (kill(target_pid, SIGTERM) == 0)
    {
        printf("[INFO] Process %d için sonlandırma emri (SIGTERM) verildi.\n", target_pid);
    }
    else
    {
        perror("Process sonlandırma hatası");
    }
}

// UI Menüsü Basma Fonksiyonu
void print_program_output()
{
    printf("╔════════════════════════════════════╗\n");
    printf("║             ProcX v1.0             ║\n");
    printf("╠════════════════════════════════════╣\n");
    printf("║ 1. Yeni Program Çalıştır           ║\n");
    printf("║ 2. Çalışan Programları Listele     ║\n");
    printf("║ 3. Program Sonlandır               ║\n");
    printf("║ 0. Çıkış                           ║\n");
    printf("╚════════════════════════════════════╝\n");
}

// Çalışan processleri listeleme fonksiyonu
void print_running_processes(SharedData *data)
{
    time_t now = time(NULL);
    char duration_str[20]; // Süreyi "5s" şeklinde tutmak için geçici alan

    printf("╔═══════╤═════════════════╤══════════╤════════════╤════════════╗\n");
    printf("║ %-5s │ %-15s │ %-8s │ %-10s │ %-10s  ║\n",
           "PID", "Command", "Mode", "Status", "Süre");
    printf("╠═══════╪═════════════════╪══════════╪════════════╪════════════╣\n");

    for (int i = 0; i < data->process_count; i++)
    {
        ProcessInfo *proc = &data->processes[i];
        if (proc->is_active)
        {
            long elapsed_seconds = (long)difftime(now, proc->start_time);

            // Önce süreyi "5s" formatında bir metne dönüştür
            snprintf(duration_str, sizeof(duration_str), "%lds", elapsed_seconds);

            printf(
                "║ %-5d │ %-15.15s │ %-8s │ %-10s │ %-10s ║\n",
                proc->pid,
                proc->command,
                proc->mode == MODE_ATACHED ? "Attached" : "Detached",
                proc->status == STATUS_RUNNING ? "Running" : "Terminated",
                duration_str); // Artık metin olarak (bitişik) yazdırıyoruz
        }
    }
    printf("╚═══════╧═════════════════╧══════════╧════════════╧════════════╝\n");
}

// Ekranı temizleyip mesajı ve menüyü yeniden basan fonksiyon
void repaint_ui(const char *message)
{
    // ANSI Escape Code: Ekranı temizle ve imleci sol üst köşeye (Home) al
    // \033[H : İmleci başa al
    // \033[J : Ekranı temizle
    printf("\033[H\033[J");

    // 1. Varsa gelen Bildirimi (IPC/Monitor mesajını) Renkli veya Belirgin Bas
    if (message != NULL)
    {
        printf(">>> %s\n", message); // Mesajı en tepeye basıyoruz
    }

    // 2. Menüyü Tekrar Bas
    print_program_output(); // Senin mevcut menü fonksiyonun

    // 3. Prompt'u (İstemciyi) Bas
    printf("Seçiminiz: ");

    // 4. Çıktıyı Zorla (Buffer'ı boşalt)
    fflush(stdout);
}

int main(int argc, char const *argv[])
{
    (void)argc; // Makefile unused parameter warning go away
    (void)argv; // Makefile unused parameter warning go away
    // IPC kaynaklarını başlat
    init_ipc_resources();
    // Thread'leri başlat
    pthread_t monitor_thread;
    pthread_t ipc_thread;

    if (pthread_create(&monitor_thread, NULL, monitor_processes, NULL) != 0)
    {
        perror("Monitor thread oluşturulamadı");
        exit(1);
    }
    if (pthread_create(&ipc_thread, NULL, ipc_listener, NULL) != 0)
    {
        perror("Listener thread oluşturulamadı");
        exit(1);
    }

    // Ana döngü
    int choice;
    char command_buffer[256];
    int mode_choice;
    pid_t pid_input;

    while (1)
    {
        // Menüyü yazdır
        print_program_output();
        printf("Seçiminiz: ");
        if (scanf("%d", &choice) != 1)
        {
            // Buffer temizle
            while (getchar() != '\n')
                ;
            continue;
        }
        // Buffer'daki newline karakterini temizle
        while (getchar() != '\n')
            ;
        switch (choice)
        {
        case 1: // Yeni program çalıştırma
            printf("Çalıştırılacak komutu girin: ");
            // fgets ile boşluklu komutları da alabiliriz
            if (fgets(command_buffer, sizeof(command_buffer), stdin) != NULL)
            {
                // Sondaki \n karakterini sil
                command_buffer[strcspn(command_buffer, "\n")] = 0;
            }

            printf("Mod seçin (0: Attached, 1: Detached): ");
            scanf("%d", &mode_choice);
            while (getchar() != '\n')
                ; // Temizlik

            create_new_process(command_buffer, (ProcessMode)mode_choice);
            break;

        case 2: // Çalışan programları listele
            sem_wait(g_sem);
            print_running_processes(g_shared_mem);
            sem_post(g_sem);
            break;
        case 3: // Program sonlandır
            printf("Sonlandırılacak process PID: ");
            scanf("%d", &pid_input);
            while (getchar() != '\n')
                ;

            terminate_process(pid_input);
            break;
        case 0: // Çıkış
            clean_exit();
            break;
        }

        sleep(1); // UI daha okunabilir olsun
    }
    return 0;
}
