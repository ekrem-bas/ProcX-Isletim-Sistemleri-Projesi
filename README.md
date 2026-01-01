# ProcX - Process Management System

**ProcX** is a C application running on Unix/Linux and macOS systems that can manage processes across multiple terminals. It uses IPC (Inter-Process Communication) mechanisms such as shared memory, semaphores, and message queues to provide synchronization between terminals.

## 📋 Contents

- [Features](#-features)
- [Requirements](#-requirements)
- [Build](#-build)
- [Usage](#-usage)
- [Architecture](#-architecture)
- [Data Structures](#-data-structures)
- [Functions](#-functions)
- [IPC Mechanisms](#-ipc-mechanisms)
- [Thread Structure](#-thread-structure)

---

## ✨ Features

- **Multi-Terminal Support**: Multiple ProcX instances can run concurrently
- **Attached/Detached Modes**: Processes can be started in attached or detached mode
- **Real-Time Monitoring**: Continuous process state monitoring via a background thread
- **IPC Notifications**: Instant notification system between terminals
- **Automatic Cleanup**: Attached processes are automatically terminated when the application exits

---

## 📦 Requirements

- **Operating System**: macOS, Linux, or other Unix-like systems
- **Compiler**: GCC or Clang (C11 support)
- **Libraries**:
  - POSIX Threads (`pthread`)
  - POSIX Shared Memory (`shm_open`, `mmap`)
  - POSIX Semaphores (`sem_open`)
  - System V Message Queues (`msgget`, `msgsnd`, `msgrcv`)

---

## 🔧 Build

You can build the project with the `make` command:

```bash
make
```

Manual build:

```bash
gcc -o procx procx.c -lpthread -Wall -Wextra
```

---

## 🚀 Usage

### Starting the Program

```bash
./procx
```

### Menu Options

```
╔════════════════════════════════════╗
║             ProcX v1.0             ║
╠════════════════════════════════════╣
║ 1. Run New Program                 ║
║ 2. List Running Programs           ║
║ 3. Terminate Program               ║
║ 0. Exit                            ║
╚════════════════════════════════════╝
```

### Process Modes

| Mode | Description |
|------|-------------|
| **Attached (0)** | Process is terminated when ProcX exits |
| **Detached (1)** | Process continues running even if ProcX exits |

### Example Usage

```bash
# Start a new sleep command (Attached mode)
Your choice: 1
Enter the command to run: sleep 100
Choose mode (0: Attached, 1: Detached): 0

# List running processes
Your choice: 2

# Terminate a specific process
Your choice: 3
PID to terminate: 12345
```

---

## 🏗 Architecture

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
│                    IPC Resources                            │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │   Shared    │  │  Semaphore  │  │   Message Queue     │  │
│  │   Memory    │  │  (/procx_   │  │   (System V)        │  │
│  │ (/procx_shm)│  │    sem)     │  │                     │  │
│  └─────────────┘  └─────────────┘  └─────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

---

## 📊 Data Structures

### ProcessMode (Enum)

Defines the running mode of a process.

```c
typedef enum {
    MODE_ATACHED = 0,   // Attached mode - terminates with ProcX
    MODE_DETACHED = 1   // Detached mode - continues after ProcX exits
} ProcessMode;
```

### ProcessStatus (Enum)

Defines the current status of a process.

```c
typedef enum {
    STATUS_RUNNING = 0,     // Running
    STATUS_TERMINATED = 1,  // Terminated
    STATUS_CREATED = 2      // Newly created
} ProcessStatus;
```

### ProcessInfo (Struct)

Holds all information about a single process.

```c
typedef struct {
    pid_t pid;            // Process ID
    pid_t owner_pid;      // PID of the ProcX instance that started it
    char command[256];    // The executed command
    ProcessMode mode;     // Attached or Detached
    ProcessStatus status; // Execution status
    time_t start_time;    // Start time
    int is_active;        // Active flag (1: active, 0: inactive)
} ProcessInfo;
```

| Field | Type | Description |
|-------|------|-------------|
| `pid` | `pid_t` | Process ID assigned by the OS |
| `owner_pid` | `pid_t` | PID of the ProcX instance that started this process |
| `command` | `char[256]` | User-entered command (e.g., "sleep 100") |
| `mode` | `ProcessMode` | Attached or Detached run mode |
| `status` | `ProcessStatus` | Running, Terminated, or Created |
| `start_time` | `time_t` | Unix timestamp when the process started |
| `is_active` | `int` | Flag indicating whether the process is active |

### SharedData (Struct)

Main data structure shared among all ProcX instances.

```c
typedef struct {
    ProcessInfo processes[50];  // Maximum 50 process entries
    int process_count;          // Number of active processes
    int instance_count;         // Number of running ProcX instances
} SharedData;
```

| Field | Type | Description |
|-------|------|-------------|
| `processes` | `ProcessInfo[50]` | Array storing process information |
| `process_count` | `int` | Number of active processes in the array |
| `instance_count` | `int` | Number of ProcX instances in the system |

### Message (Struct)

Message structure for the IPC message queue.

```c
typedef struct {
    long msg_type;      // Required for System V message queues
    int command;        // Command type (STATUS_CREATED, STATUS_TERMINATED)
    pid_t sender_pid;   // PID of the ProcX sending the message
    pid_t target_pid;   // PID of the related process
} Message;
```

| Field | Type | Description |
|-------|------|-------------|
| `msg_type` | `long` | Required field for System V message queues |
| `command` | `int` | Type of the message (creation/termination notification) |
| `sender_pid` | `pid_t` | Instance that sent the message |
| `target_pid` | `pid_t` | Process the message refers to |

---

## 🔧 Functions

### IPC Resource Management

#### `init_ipc_resources()`

Initializes IPC resources (shared memory, semaphore, message queue).

```c
void init_ipc_resources();
```

Purpose:
1. Create or attach to the shared memory segment
2. If first instance, initialize the memory to zeros
3. Create/attach the semaphore
4. Create key file for the message queue
5. Initialize the message queue
6. Increment the instance counter

System calls used:
- `shm_open()` - POSIX shared memory
- `ftruncate()` - adjust memory size
- `mmap()` - memory mapping
- `sem_open()` - POSIX semaphore
- `ftok()` - create IPC key
- `msgget()` - create message queue

---

#### `disconnect_ipc_resources()`

Detaches from IPC resources (does not remove them).

```c
void disconnect_ipc_resources();
```

Purpose:
- Use `munmap()` to unmap shared memory
- Use `sem_close()` to close the semaphore

---

#### `destroy_ipc_resources()`

Completely removes IPC resources from the system.

```c
void destroy_ipc_resources();
```

Purpose:
- Delete shared memory with `shm_unlink()`
- Delete semaphore with `sem_unlink()`
- Remove message queue with `msgctl()`

> ⚠️ Note: This function is only called when the last instance exits.

---

### Process Management

#### `create_new_process()`

Creates a new child process.

```c
void create_new_process(char *command, ProcessMode mode);
```

Parameters:
| Parameter | Type | Description |
|-----------|------|-------------|
| `command` | `char*` | Command to execute |
| `mode` | `ProcessMode` | Attached or Detached |

Behavior:
1. `fork()` to create a new process
2. In the child:
   - Tokenize the command
   - Call `setsid()` in detached mode
   - `execvp()` to run the program
3. In the parent:
   - Add process info to shared memory
   - Send IPC notifications to other instances

---

#### `terminate_process()`

Terminates the process with the specified PID.

```c
void terminate_process(pid_t target_pid);
```

Parameters:
| Parameter | Type | Description |
|-----------|------|-------------|
| `target_pid` | `pid_t` | PID of the process to terminate |

Behavior:
- Send termination signal using `kill(target_pid, SIGTERM)`

---

#### `parse_command()`

Splits a command string into an argument array.

```c
int parse_command(char *command, char *argv[]);
```

Parameters:
| Parameter | Type | Description |
|-----------|------|-------------|
| `command` | `char*` | Command string to parse |
| `argv` | `char*[]` | Array to write arguments into |

Return value: number of arguments found

Example:
```c
// For "ls -la /tmp":
// argv[0] = "ls"
// argv[1] = "-la"
// argv[2] = "/tmp"
// argv[3] = NULL
```

---

### IPC Communication

#### `send_ipc_message()`

Sends a message to all ProcX instances.

```c
void send_ipc_message(Message *msg);
```

Parameters:
| Parameter | Type | Description |
|-----------|------|-------------|
| `msg` | `Message*` | Message structure to send |

Behavior:
- Sends a copy of the message for each instance
- Each instance receives its own copy

---

### Thread Functions

#### `monitor_processes()`

Background thread that monitors processes.

```c
void *monitor_processes(void *arg);
```

Behavior:
1. Checks all processes every 2 seconds
2. Uses `waitpid(WNOHANG)` for processes it started
3. Uses `kill(pid, 0)` to check existence of others' processes
4. Removes terminated processes from shared memory
5. Sends IPC notifications for terminated processes

Techniques used:
- `waitpid(pid, &status, WNOHANG)`: non-blocking wait
- `kill(pid, 0)`: existence check (does not send a signal)

---

#### `ipc_listener()`

Thread that listens for IPC messages.

```c
void *ipc_listener(void *arg);
```

Behavior:
1. Waits for messages from the message queue (`msgrcv`)
2. Ignores messages it sent itself
3. Prints notifications from other instances to the screen
4. Uses `usleep()` to prevent busy-waiting

---

### User Interface

#### `print_program_output()`

Prints the main menu to the screen.

```c
void print_program_output();
```

---

#### `print_running_processes()`

Lists running processes in a table format.

```c
void print_running_processes(SharedData *data);
```

Output format:
```
╔═══════╤═════════════════╤══════════╤════════════╤════════════╗
║ PID   │ Command         │ Mode     │ Status     │ Duration   ║
╠═══════╪═════════════════╪══════════╪════════════╪════════════╣
║ 12345 │ sleep           │ Attached │ Running    │ 45s        ║
╚═══════╧═════════════════╧══════════╧════════════╧════════════╝
```

---

#### `repaint_ui()`

Clears the screen and redraws the UI.

```c
void repaint_ui(const char *message);
```

Parameters:
| Parameter | Type | Description |
|-----------|------|-------------|
| `message` | `const char*` | Notification to display (can be NULL) |

Behavior:
1. Clears the screen using ANSI escape codes
2. Displays the notification if provided
3. Redraws the menu
4. Calls `fflush(stdout)` to flush the buffer

---

#### `clean_exit()`

Performs a safe program exit.

```c
void clean_exit();
```

Behavior:
1. Terminates attached processes
2. Sends IPC notifications for terminated processes
3. Decrements the instance counter
4. If this is the last instance, destroys resources
5. Otherwise, just disconnects from resources

---

## 🔗 IPC Mechanisms

### Shared Memory (POSIX)

| Item | Value | Description |
|------|-------|-------------|
| **Name** | `/procx_shm` | POSIX shared memory name |
| **Size** | `sizeof(SharedData)` | ~2.5 KB |
| **Permissions** | `0666` | Read/write for all users |

Purpose: Share the process list among all instances

### Semaphore (POSIX)

| Item | Value | Description |
|------|-------|-------------|
| **Name** | `/procx_sem` | POSIX semaphore name |
| **Initial Value** | `1` | Binary semaphore (mutex) |

Purpose: Prevent concurrent access to shared memory

### Message Queue (System V)

| Item | Value | Description |
|------|-------|-------------|
| **Key File** | `/tmp/procx_ipc_key` | File for `ftok` |
| **Project ID** | `65` | ID for `ftok` |

Purpose: Instant notifications between instances

---

## 🧵 Thread Structure

| Thread | Function | Role |
|--------|----------|------|
| **Main Thread** | `main()` | User interface and input handling |
| **Monitor Thread** | `monitor_processes()` | Monitor process states |
| **IPC Listener** | `ipc_listener()` | Listen for messages from other instances |

---

## ⚠️ Known Limitations

1. **Maximum Process Count:** 50
2. **Maximum Command Length:** 255 characters
3. **Maximum Argument Count:** 10
4. **Platform:** POSIX-compliant systems (macOS, Linux)