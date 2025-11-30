#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

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
