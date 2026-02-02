#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include "qemu.h"

#define QEMU_DIR "/tmp/sirah-qemu"
#define UNIKERNEL_DIR "/tmp/sirah-unikernels"

// Track spawned QEMU processes
typedef struct {
    char vm_id[256];
    pid_t pid;
    char status[32];
} qemu_vm_t;

#define MAX_VMS 100
static qemu_vm_t vms[MAX_VMS];
static int vm_count = 0;

// Find VM by ID
static qemu_vm_t* qemu_find_vm(const char* vm_id) {
    for (int i = 0; i < vm_count; i++) {
        if (strcmp(vms[i].vm_id, vm_id) == 0) {
            return &vms[i];
        }
    }
    return NULL;
}

// Add VM to tracking
static int qemu_track_vm(const char* vm_id, pid_t pid) {
    if (vm_count >= MAX_VMS) {
        fprintf(stderr, "ERROR: Maximum VMs reached\n");
        return -1;
    }
    
    strncpy(vms[vm_count].vm_id, vm_id, sizeof(vms[vm_count].vm_id) - 1);
    vms[vm_count].pid = pid;
    strcpy(vms[vm_count].status, "running");
    vm_count++;
    
    return 0;
}

// Remove VM from tracking
static void qemu_untrack_vm(const char* vm_id) {
    for (int i = 0; i < vm_count; i++) {
        if (strcmp(vms[i].vm_id, vm_id) == 0) {
            // Shift remaining VMs
            for (int j = i; j < vm_count - 1; j++) {
                vms[j] = vms[j + 1];
            }
            vm_count--;
            return;
        }
    }
}

int qemu_init(void) {
    // Create necessary directories
    mkdir(QEMU_DIR, 0755);
    mkdir(UNIKERNEL_DIR, 0755);
    
    printf("[QEMU Runtime] Initialized\n");
    printf("  QEMU Directory: %s\n", QEMU_DIR);
    printf("  Unikernel Directory: %s\n", UNIKERNEL_DIR);
    
    vm_count = 0;
    return 0;
}

int qemu_spawn(vm_spec_t* spec) {
    if (!spec || !spec->id || !spec->image) {
        fprintf(stderr, "[QEMU] ERROR: Invalid VM spec\n");
        return -1;
    }
    
    printf("[QEMU] Spawning VM: %s (pod: %s/%s)\n", 
           spec->id, spec->namespace, spec->pod_name);
    
    // Check if already running
    if (qemu_find_vm(spec->id)) {
        fprintf(stderr, "[QEMU] ERROR: VM %s already running\n", spec->id);
        return -1;
    }
    
    // Determine image path
    char image_path[512];
    if (spec->image[0] == '/') {
        // Absolute path
        strncpy(image_path, spec->image, sizeof(image_path) - 1);
    } else {
        // Relative to unikernel directory
        snprintf(image_path, sizeof(image_path), "%s/%s", UNIKERNEL_DIR, spec->image);
    }
    
    // Check if image exists
    if (access(image_path, F_OK) != 0) {
        printf("[QEMU] WARNING: Image not found at %s, proceeding anyway\n", image_path);
        // Don't fail - simulation mode for testing
    }
    
    // Get memory and CPU counts
    int memory = (spec->memory_mb > 0) ? spec->memory_mb : 128;
    int cpus = (spec->cpu_count > 0) ? spec->cpu_count : 1;
    
    printf("[QEMU] Launching with: image=%s memory=%dMB cpus=%d\n", 
           image_path, memory, cpus);
    
    // Use fork+exec instead of system() for proper process spawning
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "[QEMU] ERROR: Failed to fork\n");
        return -1;
    }
    
    if (pid == 0) {
        // Child process - setup file descriptors and exec QEMU
        
        // Open log file for stdout/stderr
        // Write directly to final pod logs location: /tmp/sirah-logs/pods/{namespace}/{pod}/{container}.log
        char log_dir[512];
        char log_file[512];
        snprintf(log_dir, sizeof(log_dir), "/tmp/sirah-logs/pods/%s/%s", spec->namespace, spec->pod_name);
        snprintf(log_file, sizeof(log_file), "%s/app.log", log_dir);
        
        // Create directories if they don't exist
        mkdir("/tmp/sirah-logs", 0755);
        mkdir("/tmp/sirah-logs/pods", 0755);
        char ns_dir[512];
        snprintf(ns_dir, sizeof(ns_dir), "/tmp/sirah-logs/pods/%s", spec->namespace);
        mkdir(ns_dir, 0755);
        mkdir(log_dir, 0755);
        
        int log_fd = open(log_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log_fd < 0) {
            perror("open log");
            exit(1);
        }
        
        // Redirect stdout and stderr to log file
        dup2(log_fd, STDOUT_FILENO);
        dup2(log_fd, STDERR_FILENO);
        close(log_fd);
        
        // Detach from parent
        setsid();
        
        // Build argument list for execvp
        char mem_str[32];
        char cpus_str[32];
        char name_str[256];
        char serial_socket[512];
        
        snprintf(mem_str, sizeof(mem_str), "%d", memory);
        snprintf(cpus_str, sizeof(cpus_str), "%d", cpus);
        snprintf(name_str, sizeof(name_str), "%s", spec->id);
        snprintf(serial_socket, sizeof(serial_socket), "/tmp/qemu-serial-%s.sock", spec->id);
        
        // Build command with serial console for log capture:
        // -serial stdio: Output to stdout (captured in log file)
        // -serial unix:socket: Output to Unix socket (for advanced log streaming)
        // -monitor: QEMU monitor on stdio for control
        const char* argv[] = {
            "qemu-system-x86_64",
            "-kernel", image_path,
            "-m", mem_str,
            "-smp", cpus_str,
            "-nographic",
            "-name", name_str,
            "-serial", "stdio",              // Serial output to stdout (captured in log)
            "-monitor", "none",              // Disable QEMU monitor to avoid mixing output
            NULL
        };
        
        printf("[QEMU] Child execing with log capture:\n");
        printf("  qemu-system-x86_64 -kernel %s -m %s -smp %s -nographic -name %s\n", 
               image_path, mem_str, cpus_str, name_str);
        printf("  -serial stdio (logs to %s/%s/app.log)\n", log_dir, spec->pod_name);
        fflush(stdout);
        
        // Execute QEMU
        execvp("qemu-system-x86_64", (char* const*)argv);
        
        // If execvp returns, it failed
        perror("execvp");
        exit(1);
    }
    
    // Parent process continues here
    printf("[QEMU] VM spawned with PID: %d\n", pid);
    spec->vm_pid = pid;
    qemu_track_vm(spec->id, pid);
    
    // Give process a moment to start
    sleep(1);
    
    return 0;
}

int qemu_stop(const char* vm_id) {
    if (!vm_id) {
        return -1;
    }
    
    qemu_vm_t* vm = qemu_find_vm(vm_id);
    if (!vm) {
        fprintf(stderr, "[QEMU] ERROR: VM %s not found\n", vm_id);
        return -1;
    }
    
    printf("[QEMU] Stopping VM: %s\n", vm_id);
    
    // Kill VM if PID is known
    if (vm->pid > 0) {
        kill(vm->pid, SIGTERM);
        sleep(1);
        
        // Force kill if still running
        if (kill(vm->pid, 0) == 0) {
            kill(vm->pid, SIGKILL);
        }
    } else {
        // Kill by name pattern
        char kill_cmd[256];
        snprintf(kill_cmd, sizeof(kill_cmd), "pkill -f 'qemu.*%s'", vm_id);
        system(kill_cmd);
    }
    
    strcpy(vm->status, "stopped");
    qemu_untrack_vm(vm_id);
    
    return 0;
}

int qemu_get_status(const char* vm_id, char* status, size_t size) {
    if (!vm_id || !status) {
        return -1;
    }
    
    qemu_vm_t* vm = qemu_find_vm(vm_id);
    if (!vm) {
        snprintf(status, size, "unknown");
        return -1;
    }
    
    // Check if process still running
    if (vm->pid > 0 && kill(vm->pid, 0) != 0) {
        // Process no longer exists
        strcpy(vm->status, "stopped");
        qemu_untrack_vm(vm_id);
    }
    
    strncpy(status, vm->status, size - 1);
    status[size - 1] = '\0';
    
    return 0;
}

int qemu_cleanup(void) {
    // Stop all running VMs
    printf("[QEMU] Cleaning up %d VMs\n", vm_count);
    
    for (int i = 0; i < vm_count; i++) {
        if (vms[i].pid > 0) {
            kill(vms[i].pid, SIGTERM);
        }
    }
    
    vm_count = 0;
    printf("[QEMU] Cleanup complete\n");
    
    return 0;
}
