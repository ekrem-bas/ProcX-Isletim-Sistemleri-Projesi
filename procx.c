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
SharedData *g_shared_mem = NULL;                        // Shared memory pointer'ı
sem_t *g_sem = NULL;                                    // Semafor pointer'ı
int g_mq_id = -1;                                       // Mesaj kuyruğu ID'si
volatile sig_atomic_t g_shutdown = 0;                   // Programın çalışıp çalışmadığını tutar
pthread_mutex_t g_ui_mutex = PTHREAD_MUTEX_INITIALIZER; // UI mutex'i

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

            // Process inaktifse direkt sil (belki yanlışlıkla kalmıştır vs)
            if (!proc->is_active)
            {
                g_shared_mem->processes[i] = g_shared_mem->processes[g_shared_mem->process_count - 1];
                g_shared_mem->process_count--;
                i--;
                continue;
            }

            // Eğer process başka bir instance'a aitse waitpid çağırma.
            // Sadece detached ve zaten inactive olanları temizle.
            if (proc->owner_pid != getpid())
            {
                if (proc->mode == MODE_DETACHED && proc->is_active)
                {
                    // Process kapanmış mı kapanmamış mı kontrol et
                    if (kill(proc->pid, 0) == -1 && errno == ESRCH)
                    {
                        should_clean = 1;
                    }
                }
                if (!should_clean)
                {
                    continue; // Başka instance'ın aktif process'ine dokunma
                }
            }
            else // Processi ben başlatmışım
            {
                if (proc->is_active) // Process aktifse
                {
                    int status;
                    // Non-blocking waitpid ile kontrol et
                    // Eğer process sonlandıysa should_clean = 1 yap
                    pid_t result = waitpid(proc->pid, &status, WNOHANG);
                    if (result > 0)
                    {
                        should_clean = 1;
                    }
                    else if (result == 0)
                    {
                        // Hala çalışıyor o zaman bir şey yapma
                        continue;
                    }
                    else // Process bulunamadı veya hata
                    {
                        // Eğer child process yoksa temizle
                        if (errno == ECHILD)
                        {
                            should_clean = 1;
                        }
                        else
                        {
                            perror("waitpid hatası");
                            continue;
                        }
                    }
                }
            }

            if (should_clean)
            {
                // Process sonlandı bilgisini ver
                snprintf(buffer, sizeof(buffer), "[MONITOR] Process sonlandı: PID %d", proc->pid);
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
                i--; // Kaydırma sonrası aynı indexi tekrar kontrol et
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
    // Kendim dahil tüm instance'lara mesaj gönder (her instance mesaj kuyruğunu okuyacak)
    // Kendi mesajlarımızı zaten yutuyoruz listener thread'te
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

    // --- DEDUPLICATION GEÇMİŞİ ---
    // Son 20 mesajın PID ve zamanını tutar
    struct
    {
        pid_t pid;
        time_t timestamp;
    } history[20];
    int history_idx = 0;

    // Geçmişi sıfırla
    memset(history, 0, sizeof(history));

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

        time_t now = time(NULL);
        int is_duplicate = 0;

        // Geçmişte bu PID için son 2 saniye içinde mesaj gelmiş mi kontrol et
        for (int i = 0; i < 20; i++)
        {
            if (history[i].pid == msg.target_pid && (now - history[i].timestamp) <= 2)
            {
                is_duplicate = 1;
                break;
            }
        }

        if (is_duplicate)
        {
            continue; // Mükerrer mesajı yut
        }

        // Yeni mesajı geçmişe ekle
        history[history_idx].pid = msg.target_pid;
        history[history_idx].timestamp = now;
        history_idx = (history_idx + 1) % 20; // Dairesel döngü

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

    pthread_mutex_lock(&g_ui_mutex);
    printf("[SUCCESS] Process başlatıldı: PID %d (Mod: %s)\n", pid,
           (mode == MODE_DETACHED ? "Detached" : "Attached"));
    fflush(stdout);
    pthread_mutex_unlock(&g_ui_mutex);
}

// Process'i sonlandırma fonksiyonu
void terminate_process(pid_t target_pid)
{
    if (g_sem == NULL || g_shared_mem == NULL)
    {
        fprintf(stderr, "[HATA] Shared Memory veya Semafor başlatılmamış.\n");
        return;
    }
    // Sinyal gönder
    if (kill(target_pid, SIGTERM) == 0)
    {
        pthread_mutex_lock(&g_ui_mutex);
        printf("[INFO] Process %d için sonlandırma emri (SIGTERM) verildi.\n", target_pid);
        fflush(stdout);
        pthread_mutex_unlock(&g_ui_mutex);
    }
    else
    {
        perror("Process sonlandırma hatası");
    }
}

// UI Menüsü Basma Fonksiyonu
void print_program_output()
{
    printf("\r\033[K"); // Satırı temizle
    printf("╔════════════════════════════════════╗\n");
    printf("║             ProcX v1.0             ║\n");
    printf("╠════════════════════════════════════╣\n");
    printf("║ 1. Yeni Program Çalıştır           ║\n");
    printf("║ 2. Çalışan Programları Listele     ║\n");
    printf("║ 3. Program Sonlandır               ║\n");
    printf("║ 0. Çıkış                           ║\n");
    printf("╚════════════════════════════════════╝\n");
    printf("Seçiminiz: ");
}

// Çalışan processleri listeleme fonksiyonu
void print_running_processes(SharedData *data)
{
    pthread_mutex_lock(&g_ui_mutex);
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
    pthread_mutex_unlock(&g_ui_mutex);
}

// Ekranı temizleyip mesajı ve menüyü yeniden basan fonksiyon
void repaint_ui(const char *message)
{
    pthread_mutex_lock(&g_ui_mutex);

    // 1. İmleci satır başına al ve satırı temizle
    printf("\r\033[K");

    if (message != NULL)
    {
        // 2. Mesajı bas ve alt satıra geç
        printf(">>> %s\n", message);
    }

    // 3. Prompt'u (İstemciyi) tekrar göster
    printf("Seçiminiz: ");

    fflush(stdout);
    pthread_mutex_unlock(&g_ui_mutex);
}

void signal_handler(int signum)
{
    int saved_errno = errno;
    if (signum == SIGCHLD)
    {
        // Child process sonlandığında yapılacak işlemler
        // Burada herhangi bir işlem yapmıyoruz, monitor thread süreci kontrol edecek
        // İlgili child process'i waitpid ile toplamak monitor thread'in işi
        // Eğer burada waitpid çağırırsak monitor thread ile çakışabiliriz
    }
    else if (signum == SIGINT || signum == SIGTERM)
    {
        g_shutdown = 1;
    }
    errno = saved_errno;
}

int main(int argc, char const *argv[])
{
    (void)argc; // Makefile unused parameter warning go away
    (void)argv; // Makefile unused parameter warning go away

    // Sinyal işleyici yapısı
    struct sigaction sa;
    // Güvenlik için sinyal yapısını sıfırla
    memset(&sa, 0, sizeof(sa));
    // İşleyici fonksiyonunu ata
    sa.sa_handler = signal_handler;
    // SA_RESTART: Kesintiye uğrayan sistem çağrılarını yeniden başlat
    // SA_NOCLDSTOP: Child process durdurulduğunda SIGCHLD gönder
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    // Sinyal maskesini boşalt
    sigemptyset(&sa.sa_mask);
    // SIGCHLD sinyali için işleyici ayarlarını kaydet
    sigaction(SIGCHLD, &sa, NULL);

    sa.sa_handler = signal_handler;
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

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
    char input_buffer[256]; // Kullanıcı girişi için tampon

    while (!g_shutdown)
    {
        // Menüyü yazdır (race condition olmaması için mutex)
        pthread_mutex_lock(&g_ui_mutex);
        print_program_output();
        pthread_mutex_unlock(&g_ui_mutex);

        // Kullanıcı girişini oku
        while (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL)
        {
            // Eğer shutdown sinyali geldiyse çık
            // Cünkü fgets sinyal yüzünden kesilmiş olabilir
            if (g_shutdown)
                break;

            // Eğer sinyal yüzünden kesildiyse (ve SA_RESTART çalışmadıysa)
            // Menüyü tekrar basmadan döngüde kalıp tekrar okumayı dene
            // UI yapısını korumak için
            if (errno == EINTR)
            {
                clearerr(stdin);
                continue;
            }
            break; // Gerçek bir hata varsa çık
        }

        // Eğer shutdown sinyali geldiyse çık
        if (g_shutdown)
            break;

        // Girişi işle
        if (sscanf(input_buffer, "%d", &choice) != 1)
        {
            continue;
        }

        switch (choice)
        {
        case 1: // Yeni program çalıştırma
            // printf'te race condition olmaması için mutex kullan
            pthread_mutex_lock(&g_ui_mutex);
            printf("Çalıştırılacak komutu girin: ");
            fflush(stdout);
            pthread_mutex_unlock(&g_ui_mutex);

            if (fgets(command_buffer, sizeof(command_buffer), stdin) != NULL)
            {
                // Sondaki \n karakterini sil
                command_buffer[strcspn(command_buffer, "\n")] = 0;
            }

            // printf race condition olmaması için mutex kullan
            pthread_mutex_lock(&g_ui_mutex);
            printf("Mod seçin (0: Attached, 1: Detached): ");
            fflush(stdout);
            pthread_mutex_unlock(&g_ui_mutex);

            // Seçimi oku
            scanf("%d", &mode_choice);
            while (getchar() != '\n')
                ; // Temizlik
            create_new_process(command_buffer, (ProcessMode)mode_choice);
            break;
        case 2: // Çalışan programları listele
            // Çalışan processleri shared memory'den okuyacağımız için
            // semaforu al
            sem_wait(g_sem);
            print_running_processes(g_shared_mem);
            sem_post(g_sem);
            break;
        case 3: // Program sonlandır
            pthread_mutex_lock(&g_ui_mutex);
            printf("Sonlandırılacak process PID: ");
            fflush(stdout);
            pthread_mutex_unlock(&g_ui_mutex);
            scanf("%d", &pid_input);
            while (getchar() != '\n')
                ;

            terminate_process(pid_input);
            break;
        case 0: // Çıkış
            g_shutdown = 1;
            break;
        }

        sleep(1); // UI daha okunabilir olsun
    }

    // Shutdown: thread sonlandırma ve temiz çıkış
    pthread_cancel(monitor_thread);
    pthread_cancel(ipc_thread);
    pthread_join(monitor_thread, NULL);
    pthread_join(ipc_thread, NULL);

    clean_exit();
    return 0;
}
