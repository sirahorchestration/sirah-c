// internal/kubelet/qemu_manager.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <json-c/json.h>
#include "qemu_manager.h"

#define MAX_VMS 100
#define MAX_QMP_RESPONSE 8192

// Global state
static struct {
    char qemu_bin[512];
    char vm_dir[512];
    int kvm_enabled;
    int default_memory_mb;
    int default_vcpus;
    qemu_vm_t vms[MAX_VMS];
    int vm_count;
} g_qemu_manager = {
    .kvm_enabled = 1,
    .default_memory_mb = 256,
    .default_vcpus = 2,
    .vm_count = 0
};

// ===== Utility Functions =====

static qemu_vm_t* find_vm(const char* vm_id) {
    for (int i = 0; i < g_qemu_manager.vm_count; i++) {
        if (strcmp(g_qemu_manager.vms[i].vm_id, vm_id) == 0) {
            return &g_qemu_manager.vms[i];
        }
    }
    return NULL;
}

static char* generate_vm_id(const char* pod_name, const char* namespace) {
    static char vm_id[256];
    snprintf(vm_id, sizeof(vm_id), "%s-%s-%ld", namespace, pod_name, time(NULL));
    return vm_id;
}

static int build_qemu_cmdline(qemu_vm_t* vm, char* cmdline, int max_len) {
    int offset = 0;
    
    // Basic QEMU command
    offset += snprintf(cmdline + offset, max_len - offset, "%s", g_qemu_manager.qemu_bin);
    
    // Machine type and accelerator
    if (g_qemu_manager.kvm_enabled) {
        offset += snprintf(cmdline + offset, max_len - offset, " -machine type=pc,accel=kvm");
    } else {
        offset += snprintf(cmdline + offset, max_len - offset, " -machine type=pc");
    }
    
    // Memory and CPUs
    offset += snprintf(cmdline + offset, max_len - offset, 
                      " -m %d -smp cpus=%d", vm->memory_mb, vm->vcpus);
    
    // Unikernel kernel image
    offset += snprintf(cmdline + offset, max_len - offset, 
                      " -kernel %s", vm->image_path);
    
    // Serial console for output
    offset += snprintf(cmdline + offset, max_len - offset,
                      " -serial unix:%s,server", vm->serial_socket);
    
    // QMP (QEMU Monitor Protocol) socket
    offset += snprintf(cmdline + offset, max_len - offset,
                      " -qmp unix:%s,server,nowait", vm->vm_socket);
    
    // Disable graphics (headless mode)
    offset += snprintf(cmdline + offset, max_len - offset, " -nographic");
    
    // Enable UART emulation for serial
    offset += snprintf(cmdline + offset, max_len - offset, " -device isa-serial");
    
    // VM identifier (for log files)
    offset += snprintf(cmdline + offset, max_len - offset, 
                      " -name %s", vm->vm_id);
    
    // PIDs to monitor
    offset += snprintf(cmdline + offset, max_len - offset,
                      " -pidfile %s/%s.pid", g_qemu_manager.vm_dir, vm->vm_id);
    
    return 0;
}

// ===== VM Lifecycle Management =====

char* qemu_create_vm(const char* pod_name, const char* namespace,
                     const char* unikernel_image, int memory_mb, int vcpus) {
    if (g_qemu_manager.vm_count >= MAX_VMS) {
        return NULL;  // VM quota exceeded
    }
    
    if (!unikernel_image || access(unikernel_image, F_OK) != 0) {
        return NULL;  // Invalid image path
    }
    
    qemu_vm_t* vm = &g_qemu_manager.vms[g_qemu_manager.vm_count++];
    
    // Initialize VM structure
    char* vm_id = generate_vm_id(pod_name, namespace);
    strcpy(vm->vm_id, vm_id);
    strcpy(vm->pod_name, pod_name);
    strcpy(vm->namespace, namespace);
    strcpy(vm->image_path, unikernel_image);
    vm->memory_mb = memory_mb > 0 ? memory_mb : g_qemu_manager.default_memory_mb;
    vm->vcpus = vcpus > 0 ? vcpus : g_qemu_manager.default_vcpus;
    vm->state = QEMU_STATE_CREATED;
    vm->qemu_pid = 0;
    vm->exit_code = -1;
    vm->created_at = time(NULL);
    
    // Setup socket paths
    snprintf(vm->vm_socket, sizeof(vm->vm_socket), 
             "%s/%s.qmp", g_qemu_manager.vm_dir, vm->vm_id);
    snprintf(vm->serial_socket, sizeof(vm->serial_socket),
             "%s/%s.serial", g_qemu_manager.vm_dir, vm->vm_id);
    
    return vm->vm_id;
}

int qemu_start_vm(const char* vm_id) {
    qemu_vm_t* vm = find_vm(vm_id);
    if (!vm) return -1;
    if (vm->state == QEMU_STATE_RUNNING) return 0;
    
    char cmdline[2048] = {0};
    build_qemu_cmdline(vm, cmdline, sizeof(cmdline));
    
    pid_t pid = fork();
    if (pid == 0) {
        // Child process: execute QEMU
        // Redirect output to log file
        char log_file[512];
        snprintf(log_file, sizeof(log_file), "%s/%s.log", g_qemu_manager.vm_dir, vm->vm_id);
        
        FILE* logf = fopen(log_file, "a");
        if (logf) {
            dup2(fileno(logf), STDOUT_FILENO);
            dup2(fileno(logf), STDERR_FILENO);
            fclose(logf);
        }
        
        // Execute QEMU
        char* args[] = {
            g_qemu_manager.qemu_bin,
            "-machine", g_qemu_manager.kvm_enabled ? "type=pc,accel=kvm" : "type=pc",
            "-m", NULL,  // Will be set below
            "-smp", NULL,  // Will be set below
            "-kernel", (char*)vm->image_path,
            "-serial", NULL,  // Will be set below
            "-qmp", NULL,  // Will be set below
            "-nographic",
            "-device", "isa-serial",
            "-name", (char*)vm->vm_id,
            NULL
        };
        
        // Build dynamic arguments
        static char mem_arg[32], cpu_arg[32], serial_arg[256], qmp_arg[256];
        snprintf(mem_arg, sizeof(mem_arg), "%d", vm->memory_mb);
        snprintf(cpu_arg, sizeof(cpu_arg), "cpus=%d", vm->vcpus);
        snprintf(serial_arg, sizeof(serial_arg), "unix:%s,server", vm->serial_socket);
        snprintf(qmp_arg, sizeof(qmp_arg), "unix:%s,server,nowait", vm->vm_socket);
        
        // Set dynamic args
        for (int i = 0; args[i]; i++) {
            if (args[i] == NULL && args[i-1]) {
                if (strcmp(args[i-1], "-m") == 0) args[i] = mem_arg;
                else if (strcmp(args[i-1], "-smp") == 0) args[i] = cpu_arg;
                else if (strcmp(args[i-1], "-serial") == 0) args[i] = serial_arg;
                else if (strcmp(args[i-1], "-qmp") == 0) args[i] = qmp_arg;
            }
        }
        
        // Execute
        execvp(g_qemu_manager.qemu_bin, args);
        exit(1);  // Only reached on exec failure
    }
    else if (pid > 0) {
        // Parent process
        vm->qemu_pid = pid;
        vm->state = QEMU_STATE_RUNNING;
        vm->started_at = time(NULL);
        return 0;
    }
    
    return -1;  // fork failed
}

int qemu_pause_vm(const char* vm_id) {
    qemu_vm_t* vm = find_vm(vm_id);
    if (!vm || vm->state != QEMU_STATE_RUNNING) return -1;
    
    // Send SIGSTOP to QEMU process
    if (kill(vm->qemu_pid, SIGSTOP) == 0) {
        vm->state = QEMU_STATE_PAUSED;
        return 0;
    }
    return -1;
}

int qemu_resume_vm(const char* vm_id) {
    qemu_vm_t* vm = find_vm(vm_id);
    if (!vm || vm->state != QEMU_STATE_PAUSED) return -1;
    
    // Send SIGCONT to QEMU process
    if (kill(vm->qemu_pid, SIGCONT) == 0) {
        vm->state = QEMU_STATE_RUNNING;
        return 0;
    }
    return -1;
}

int qemu_stop_vm(const char* vm_id, int timeout_sec) {
    qemu_vm_t* vm = find_vm(vm_id);
    if (!vm || vm->state == QEMU_STATE_STOPPED) return 0;
    
    // Send SIGTERM for graceful shutdown
    if (vm->qemu_pid > 0) {
        kill(vm->qemu_pid, SIGTERM);
        
        // Wait for process to exit
        for (int i = 0; i < timeout_sec * 10; i++) {
            int status = 0;
            pid_t result = waitpid(vm->qemu_pid, &status, WNOHANG);
            if (result == vm->qemu_pid) {
                vm->state = QEMU_STATE_STOPPED;
                if (WIFEXITED(status)) {
                    vm->exit_code = WEXITSTATUS(status);
                } else {
                    vm->exit_code = -1;
                }
                return 0;
            }
            usleep(100000);  // 100ms
        }
        
        // Timeout - force kill
        return qemu_kill_vm(vm_id);
    }
    
    vm->state = QEMU_STATE_STOPPED;
    return 0;
}

int qemu_kill_vm(const char* vm_id) {
    qemu_vm_t* vm = find_vm(vm_id);
    if (!vm) return -1;
    
    if (vm->qemu_pid > 0) {
        kill(vm->qemu_pid, SIGKILL);
        int status = 0;
        waitpid(vm->qemu_pid, &status, 0);
        vm->exit_code = -1;
    }
    
    vm->state = QEMU_STATE_STOPPED;
    return 0;
}

int qemu_delete_vm(const char* vm_id) {
    qemu_vm_t* vm = find_vm(vm_id);
    if (!vm) return -1;
    if (vm->state != QEMU_STATE_STOPPED) return -1;
    
    // Clean up socket files
    unlink(vm->vm_socket);
    unlink(vm->serial_socket);
    
    char pid_file[512];
    snprintf(pid_file, sizeof(pid_file), "%s/%s.pid", g_qemu_manager.vm_dir, vm->vm_id);
    unlink(pid_file);
    
    char log_file[512];
    snprintf(log_file, sizeof(log_file), "%s/%s.log", g_qemu_manager.vm_dir, vm->vm_id);
    unlink(log_file);
    
    // Remove from list
    for (int i = 0; i < g_qemu_manager.vm_count; i++) {
        if (&g_qemu_manager.vms[i] == vm) {
            for (int j = i; j < g_qemu_manager.vm_count - 1; j++) {
                g_qemu_manager.vms[j] = g_qemu_manager.vms[j + 1];
            }
            g_qemu_manager.vm_count--;
            return 0;
        }
    }
    
    return 0;
}

// ===== VM State Management =====

qemu_vm_state_t qemu_get_state(const char* vm_id) {
    qemu_vm_t* vm = find_vm(vm_id);
    if (!vm) return QEMU_STATE_FAILED;
    return vm->state;
}

qemu_vm_t* qemu_get_vm(const char* vm_id) {
    return find_vm(vm_id);
}

int qemu_list_vms(qemu_vm_t** vms, int max_vms) {
    int count = 0;
    for (int i = 0; i < g_qemu_manager.vm_count && count < max_vms; i++) {
        vms[count++] = &g_qemu_manager.vms[i];
    }
    return count;
}

int qemu_vm_exists(const char* vm_id) {
    return find_vm(vm_id) != NULL;
}

// ===== VM Communication =====

char* qemu_send_qmp_command(const char* vm_id, const char* command) {
    // Note: In production, this would use proper QMP protocol
    // For MVP, we return a simulated response
    static char response[512];
    snprintf(response, sizeof(response), 
             "{\"return\":{\"status\":\"%s\"}}", command);
    return response;
}

char* qemu_execute_command(const char* vm_id, const char* command, int timeout_sec) {
    // Would use serial console communication
    static char output[2048];
    snprintf(output, sizeof(output), 
             "Command executed: %s\\nOutput: simulated response", command);
    return output;
}

char* qemu_get_serial_output(const char* vm_id, int lines) {
    qemu_vm_t* vm = find_vm(vm_id);
    if (!vm) return NULL;
    
    static char output[4096];
    char log_file[512];
    snprintf(log_file, sizeof(log_file), "%s/%s.log", g_qemu_manager.vm_dir, vm->vm_id);
    
    FILE* f = fopen(log_file, "r");
    if (!f) {
        strcpy(output, "No logs available");
        return output;
    }
    
    // Read last N lines (simplified)
    output[0] = '\0';
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        strncat(output, line, sizeof(output) - strlen(output) - 1);
    }
    fclose(f);
    
    return output;
}

// ===== Resource Management =====

int qemu_set_memory(const char* vm_id, int memory_mb) {
    qemu_vm_t* vm = find_vm(vm_id);
    if (!vm) return -1;
    
    // In production, use QMP balloon command for live resize
    vm->memory_mb = memory_mb;
    return 0;
}

int qemu_set_vcpus(const char* vm_id, int vcpus) {
    qemu_vm_t* vm = find_vm(vm_id);
    if (!vm) return -1;
    
    // In production, use QMP hotplug commands
    vm->vcpus = vcpus;
    return 0;
}

int qemu_get_stats(const char* vm_id, qemu_stats_t* stats) {
    if (!stats) return -1;
    
    memset(stats, 0, sizeof(*stats));
    stats->total_vms = g_qemu_manager.vm_count;
    
    int running = 0;
    long total_mem = 0;
    for (int i = 0; i < g_qemu_manager.vm_count; i++) {
        if (g_qemu_manager.vms[i].state == QEMU_STATE_RUNNING) {
            running++;
        }
        total_mem += g_qemu_manager.vms[i].memory_mb;
    }
    
    stats->running_vms = running;
    stats->total_memory_mb = total_mem;
    
    return 0;
}

// ===== Network Integration =====

int qemu_attach_network(const char* vm_id, const char* network_name, const char* mac_addr) {
    // Would use QMP to hot-add network device
    return 0;
}

int qemu_detach_network(const char* vm_id, const char* network_name) {
    // Would use QMP to hot-remove network device
    return 0;
}

char* qemu_get_ip_address(const char* vm_id, const char* interface_name) {
    static char ip[64];
    strcpy(ip, "192.168.1.100");  // Placeholder
    return ip;
}

// ===== Storage Integration =====

int qemu_attach_block(const char* vm_id, const char* block_path, const char* device_id) {
    // Would use QMP to hot-add block device
    return 0;
}

int qemu_detach_block(const char* vm_id, const char* device_id) {
    // Would use QMP to hot-remove block device
    return 0;
}

// ===== Configuration =====

int qemu_manager_init(const char* qemu_bin, const char* vm_dir) {
    if (!qemu_bin || !vm_dir) return -1;
    
    strcpy(g_qemu_manager.qemu_bin, qemu_bin);
    strcpy(g_qemu_manager.vm_dir, vm_dir);
    
    // Create VM directory if it doesn't exist
    mkdir(vm_dir, 0755);
    
    return 0;
}

int qemu_manager_shutdown() {
    // Stop all running VMs
    for (int i = 0; i < g_qemu_manager.vm_count; i++) {
        qemu_stop_vm(g_qemu_manager.vms[i].vm_id, 5);
    }
    
    return 0;
}

int qemu_set_binary_path(const char* qemu_bin) {
    if (!qemu_bin) return -1;
    strcpy(g_qemu_manager.qemu_bin, qemu_bin);
    return 0;
}

int qemu_set_vm_directory(const char* vm_dir) {
    if (!vm_dir) return -1;
    strcpy(g_qemu_manager.vm_dir, vm_dir);
    mkdir(vm_dir, 0755);
    return 0;
}

int qemu_set_kvm_enabled(int enabled) {
    g_qemu_manager.kvm_enabled = enabled;
    return 0;
}

int qemu_set_default_memory(int memory_mb) {
    if (memory_mb <= 0) return -1;
    g_qemu_manager.default_memory_mb = memory_mb;
    return 0;
}

int qemu_set_default_vcpus(int vcpus) {
    if (vcpus <= 0) return -1;
    g_qemu_manager.default_vcpus = vcpus;
    return 0;
}
