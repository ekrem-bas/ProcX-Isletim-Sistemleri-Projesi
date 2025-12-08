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
    STATUS_TERMINATED = 1
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
