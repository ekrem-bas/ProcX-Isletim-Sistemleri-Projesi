# ProcX - Process Yönetim Sistemi

**ProcX**, Unix/Linux ve macOS sistemlerinde çalışan, birden fazla terminal üzerinden süreç (process) yönetimi yapabilen bir C uygulamasıdır. Paylaşımlı bellek (Shared Memory), semaforlar ve mesaj kuyrukları gibi IPC (Inter-Process Communication) mekanizmalarını kullanarak terminaller arası senkronizasyon sağlar.

## 📋 İçindekiler

- [Özellikler](#-özellikler)
- [Gereksinimler](#-gereksinimler)
- [Derleme](#-derleme)
- [Kullanım](#-kullanım)
- [Mimari](#-mimari)
- [Veri Yapıları](#-veri-yapıları)
- [Fonksiyonlar](#-fonksiyonlar)
- [IPC Mekanizmaları](#-ipc-mekanizmaları)
- [Thread Yapısı](#-thread-yapısı)

---

## ✨ Özellikler

- **Çoklu Terminal Desteği**: Birden fazla ProcX instance'ı aynı anda çalışabilir
- **Attached/Detached Modları**: Process'ler bağlı veya bağımsız modda başlatılabilir
- **Gerçek Zamanlı İzleme**: Arka plan thread'i ile process durumları sürekli izlenir
- **IPC Bildirimleri**: Terminaller arası anlık bildirim sistemi
- **Otomatik Temizlik**: Uygulama kapanırken attached process'ler otomatik sonlandırılır

---

## 📦 Gereksinimler

- **İşletim Sistemi**: macOS, Linux veya Unix-benzeri sistem
- **Derleyici**: GCC veya Clang (C11 desteği)
- **Kütüphaneler**:
  - POSIX Threads (`pthread`)
  - POSIX Shared Memory (`shm_open`, `mmap`)
  - POSIX Semaphores (`sem_open`)
  - System V Message Queues (`msgget`, `msgsnd`, `msgrcv`)

---

## 🔧 Derleme

Projeyi derlemek için `make` komutunu kullanabilirsiniz:

```bash
make
```

Manuel derleme:

```bash
gcc -o procx procx.c -lpthread -Wall -Wextra
```

---

## 🚀 Kullanım

### Programı Başlatma

```bash
./procx
```

### Menü Seçenekleri

```
╔════════════════════════════════════╗
║             ProcX v1.0             ║
╠════════════════════════════════════╣
║ 1. Yeni Program Çalıştır           ║
║ 2. Çalışan Programları Listele     ║
║ 3. Program Sonlandır               ║
║ 0. Çıkış                           ║
╚════════════════════════════════════╝
```

### Process Modları

| Mod | Açıklama |
|-----|----------|
| **Attached (0)** | ProcX kapandığında process de sonlandırılır |
| **Detached (1)** | ProcX kapansa bile process çalışmaya devam eder |

### Örnek Kullanım

```bash
# Yeni bir sleep komutu başlat (Attached mod)
Seçiminiz: 1
Çalıştırılacak komutu girin: sleep 100
Mod seçin (0: Attached, 1: Detached): 0

# Çalışan process'leri listele
Seçiminiz: 2

# Belirli bir process'i sonlandır
Seçiminiz: 3
Sonlandırılacak process PID: 12345
```

---

## 🏗 Mimari

```
┌─────────────────────────────────────────────────────────────┐
│                        ProcX Instance                       │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │ Main Thread │  │  Monitor    │  │    IPC Listener     │  │
│  │   (UI)      │  │   Thread    │  │      Thread         │  │
│  └─────────────┘  └─────────────┘  └─────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
           │                │                    │
           └────────────────┼────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                    IPC Kaynakları                           │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │   Shared    │  │  Semaphore  │  │   Message Queue     │  │
│  │   Memory    │  │  (/procx_   │  │   (System V)        │  │
│  │ (/procx_shm)│  │    sem)     │  │                     │  │
│  └─────────────┘  └─────────────┘  └─────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

---

## 📊 Veri Yapıları

### ProcessMode (Enum)

Process'in çalışma modunu tanımlar.

```c
typedef enum {
    MODE_ATACHED = 0,   // Bağlı mod - ProcX ile birlikte sonlanır
    MODE_DETACHED = 1   // Bağımsız mod - ProcX kapansa da devam eder
} ProcessMode;
```

### ProcessStatus (Enum)

Process'in mevcut durumunu tanımlar.

```c
typedef enum {
    STATUS_RUNNING = 0,     // Çalışıyor
    STATUS_TERMINATED = 1,  // Sonlandırıldı
    STATUS_CREATED = 2      // Yeni oluşturuldu
} ProcessStatus;
```

### ProcessInfo (Struct)

Tek bir process hakkındaki tüm bilgileri tutar.

```c
typedef struct {
    pid_t pid;            // Process ID
    pid_t owner_pid;      // Başlatan ProcX instance'ının PID'si
    char command[256];    // Çalıştırılan komut
    ProcessMode mode;     // Attached veya Detached
    ProcessStatus status; // Çalışma durumu
    time_t start_time;    // Başlangıç zamanı
    int is_active;        // Aktiflik durumu (1: aktif, 0: pasif)
} ProcessInfo;
```

| Alan | Tip | Açıklama |
|------|-----|----------|
| `pid` | `pid_t` | İşletim sistemi tarafından atanan process ID |
| `owner_pid` | `pid_t` | Bu process'i başlatan ProcX instance'ının PID'si |
| `command` | `char[256]` | Kullanıcının girdiği komut (örn: "sleep 100") |
| `mode` | `ProcessMode` | Attached veya Detached çalışma modu |
| `status` | `ProcessStatus` | Running, Terminated veya Created |
| `start_time` | `time_t` | Process'in başlatıldığı Unix timestamp |
| `is_active` | `int` | Process'in aktif olup olmadığını belirten bayrak |

### SharedData (Struct)

Tüm ProcX instance'ları arasında paylaşılan ana veri yapısı.

```c
typedef struct {
    ProcessInfo processes[50];  // Maksimum 50 process bilgisi
    int process_count;          // Aktif process sayısı
    int instance_count;         // Çalışan ProcX instance sayısı
} SharedData;
```

| Alan | Tip | Açıklama |
|------|-----|----------|
| `processes` | `ProcessInfo[50]` | Process bilgilerini tutan dizi |
| `process_count` | `int` | Dizideki aktif process sayısı |
| `instance_count` | `int` | Sistemde çalışan ProcX sayısı |

### Message (Struct)

IPC mesaj kuyruğu için mesaj yapısı.

```c
typedef struct {
    long msg_type;      // Mesaj tipi (System V requirement)
    int command;        // Komut tipi (STATUS_CREATED, STATUS_TERMINATED)
    pid_t sender_pid;   // Mesajı gönderen ProcX'in PID'si
    pid_t target_pid;   // İlgili process'in PID'si
} Message;
```

| Alan | Tip | Açıklama |
|------|-----|----------|
| `msg_type` | `long` | System V mesaj kuyruğu için zorunlu alan |
| `command` | `int` | Mesajın türü (oluşturma/sonlandırma bildirimi) |
| `sender_pid` | `pid_t` | Mesajı gönderen instance |
| `target_pid` | `pid_t` | Mesajın ilgili olduğu process |

---

## 🔧 Fonksiyonlar

### IPC Kaynak Yönetimi

#### `init_ipc_resources()`

IPC kaynaklarını (shared memory, semaphore, message queue) başlatır.

```c
void init_ipc_resources();
```

**İşlevi:**
1. Shared memory segmenti oluşturur veya mevcut olana bağlanır
2. İlk instance ise belleği sıfırlar
3. Semafor oluşturur/bağlanır
4. Message queue için key dosyası oluşturur
5. Message queue'yu başlatır
6. Instance sayacını artırır

**Kullanılan Sistem Çağrıları:**
- `shm_open()` - POSIX shared memory
- `ftruncate()` - Bellek boyutu ayarlama
- `mmap()` - Bellek eşleme
- `sem_open()` - POSIX semaphore
- `ftok()` - IPC key oluşturma
- `msgget()` - Message queue oluşturma

---

#### `disconnect_ipc_resources()`

IPC kaynaklarından bağlantıyı keser (silmez).

```c
void disconnect_ipc_resources();
```

**İşlevi:**
- `munmap()` ile shared memory bağlantısını keser
- `sem_close()` ile semaforu kapatır

---

#### `destroy_ipc_resources()`

IPC kaynaklarını sistemden tamamen siler.

```c
void destroy_ipc_resources();
```

**İşlevi:**
- `shm_unlink()` ile shared memory'yi siler
- `sem_unlink()` ile semaforu siler
- `msgctl()` ile message queue'yu siler

> ⚠️ **Not:** Bu fonksiyon yalnızca son instance kapanırken çağrılır.

---

### Process Yönetimi

#### `create_new_process()`

Yeni bir child process oluşturur.

```c
void create_new_process(char *command, ProcessMode mode);
```

**Parametreler:**
| Parametre | Tip | Açıklama |
|-----------|-----|----------|
| `command` | `char*` | Çalıştırılacak komut |
| `mode` | `ProcessMode` | Attached veya Detached |

**İşlevi:**
1. `fork()` ile yeni process oluşturur
2. Child process'te:
   - Komutu tokenize eder
   - Detached modda `setsid()` çağırır
   - `execvp()` ile programı çalıştırır
3. Parent process'te:
   - Shared memory'ye process bilgisini ekler
   - Diğer instance'lara IPC bildirimi gönderir

---

#### `terminate_process()`

Belirtilen PID'ye sahip process'i sonlandırır.

```c
void terminate_process(pid_t target_pid);
```

**Parametreler:**
| Parametre | Tip | Açıklama |
|-----------|-----|----------|
| `target_pid` | `pid_t` | Sonlandırılacak process'in ID'si |

**İşlevi:**
- `kill(target_pid, SIGTERM)` ile process'e sonlandırma sinyali gönderir

---

#### `parse_command()`

Komut string'ini argüman dizisine ayırır.

```c
int parse_command(char *command, char *argv[]);
```

**Parametreler:**
| Parametre | Tip | Açıklama |
|-----------|-----|----------|
| `command` | `char*` | Ayrıştırılacak komut string'i |
| `argv` | `char*[]` | Argümanların yazılacağı dizi |

**Dönüş Değeri:** Bulunan argüman sayısı

**Örnek:**
```c
// "ls -la /tmp" komutu için:
// argv[0] = "ls"
// argv[1] = "-la"
// argv[2] = "/tmp"
// argv[3] = NULL
```

---

### IPC İletişimi

#### `send_ipc_message()`

Tüm ProcX instance'larına mesaj gönderir.

```c
void send_ipc_message(Message *msg);
```

**Parametreler:**
| Parametre | Tip | Açıklama |
|-----------|-----|----------|
| `msg` | `Message*` | Gönderilecek mesaj yapısı |

**İşlevi:**
- Instance sayısı kadar mesaj kopyası gönderir
- Her instance kendi kopyasını alır

---

### Thread Fonksiyonları

#### `monitor_processes()`

Arka planda çalışan process izleme thread'i.

```c
void *monitor_processes(void *arg);
```

**İşlevi:**
1. 2 saniyede bir tüm process'leri kontrol eder
2. Kendi başlattığı process'ler için `waitpid(WNOHANG)` kullanır
3. Başkasının process'leri için `kill(pid, 0)` ile varlık kontrolü yapar
4. Sonlanan process'leri shared memory'den kaldırır
5. IPC bildirimi gönderir

**Kullanılan Teknikler:**
- `waitpid(pid, &status, WNOHANG)`: Non-blocking bekleme
- `kill(pid, 0)`: Process varlık kontrolü (sinyal göndermez)

---

#### `ipc_listener()`

IPC mesajlarını dinleyen thread.

```c
void *ipc_listener(void *arg);
```

**İşlevi:**
1. Message queue'dan mesaj bekler (`msgrcv`)
2. Kendi gönderdiği mesajları yoksayar
3. Diğer instance'lardan gelen bildirimleri ekrana basar
4. Açgözlülük önleme için `usleep()` kullanır

---

### Kullanıcı Arayüzü

#### `print_program_output()`

Ana menüyü ekrana basar.

```c
void print_program_output();
```

---

#### `print_running_processes()`

Çalışan process'leri tablo formatında listeler.

```c
void print_running_processes(SharedData *data);
```

**Çıktı Formatı:**
```
╔═══════╤═════════════════╤══════════╤════════════╤════════════╗
║ PID   │ Command         │ Mode     │ Status     │ Süre       ║
╠═══════╪═════════════════╪══════════╪════════════╪════════════╣
║ 12345 │ sleep           │ Attached │ Running    │ 45s        ║
╚═══════╧═════════════════╧══════════╧════════════╧════════════╝
```

---

#### `repaint_ui()`

Ekranı temizleyip UI'ı yeniden çizer.

```c
void repaint_ui(const char *message);
```

**Parametreler:**
| Parametre | Tip | Açıklama |
|-----------|-----|----------|
| `message` | `const char*` | Gösterilecek bildirim (NULL olabilir) |

**İşlevi:**
1. ANSI escape kodları ile ekranı temizler
2. Varsa bildirimi gösterir
3. Menüyü yeniden basar
4. `fflush(stdout)` ile buffer'ı temizler

---

#### `clean_exit()`

Programdan güvenli çıkış yapar.

```c
void clean_exit();
```

**İşlevi:**
1. Attached process'leri sonlandırır
2. Sonlandırılan process'ler için IPC bildirimi gönderir
3. Instance sayacını azaltır
4. Son instance ise kaynakları yok eder
5. Değilse sadece bağlantıyı keser

---

## 🔗 IPC Mekanizmaları

### Shared Memory (POSIX)

| Öğe | Değer | Açıklama |
|-----|-------|----------|
| **İsim** | `/procx_shm` | POSIX shared memory adı |
| **Boyut** | `sizeof(SharedData)` | ~2.5 KB |
| **İzinler** | `0666` | Tüm kullanıcılar okuyabilir/yazabilir |

**Kullanım Amacı:** Tüm instance'ların process listesini paylaşması

### Semaphore (POSIX)

| Öğe | Değer | Açıklama |
|-----|-------|----------|
| **İsim** | `/procx_sem` | POSIX semaphore adı |
| **Başlangıç Değeri** | `1` | Binary semaphore (mutex) |

**Kullanım Amacı:** Shared memory'ye eşzamanlı erişimi engellemek

### Message Queue (System V)

| Öğe | Değer | Açıklama |
|-----|-------|----------|
| **Key Dosyası** | `/tmp/procx_ipc_key` | ftok için dosya |
| **Proje ID** | `65` | ftok için ID |

**Kullanım Amacı:** Instance'lar arası anlık bildirim

---

## 🧵 Thread Yapısı

| Thread | Fonksiyon | Görevi |
|--------|-----------|--------|
| **Main Thread** | `main()` | Kullanıcı arayüzü ve girdi işleme |
| **Monitor Thread** | `monitor_processes()` | Process durumlarını izleme |
| **IPC Listener** | `ipc_listener()` | Diğer instance'lardan gelen mesajları dinleme |

---

## ⚠️ Bilinen Sınırlamalar

1. **Maksimum Process Sayısı:** 50
2. **Maksimum Komut Uzunluğu:** 255 karakter
3. **Maksimum Argüman Sayısı:** 10
4. **Platform:** POSIX uyumlu sistemler (macOS, Linux)

---
