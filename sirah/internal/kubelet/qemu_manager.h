// internal/kubelet/qemu_manager.h
// QEMU VM management for unikernel pods
#ifndef SIRAH_QEMU_MANAGER_H
#define SIRAH_QEMU_MANAGER_H

#include <time.h>

typedef enum {
    QEMU_STATE_CREATED,
    QEMU_STATE_RUNNING,
    QEMU_STATE_PAUSED,
    QEMU_STATE_STOPPED,
    QEMU_STATE_FAILED
} qemu_vm_state_t;

typedef struct {
    char vm_id[256];              // Unique VM identifier (pod UUID)
    char pod_name[256];           // Pod name
    char namespace[256];          // Namespace
    char image_path[512];         // Path to unikernel image
    char vm_socket[512];          // QMP (QEMU Monitor Protocol) socket path
    char serial_socket[512];      // Serial console socket path
    int memory_mb;                // Allocated memory in MB
    int vcpus;                    // Virtual CPUs
    pid_t qemu_pid;               // QEMU process ID
    qemu_vm_state_t state;        // Current VM state
    time_t created_at;            // Creation timestamp
    time_t started_at;            // Start timestamp
    int exit_code;                // Exit code (when stopped)
    char error_message[512];      // Error message if failed
} qemu_vm_t;

typedef struct {
    int total_vms;                // Total VMs created
    int running_vms;              // Currently running VMs
    long total_memory_mb;         // Total memory allocated
    long used_memory_mb;          // Memory currently in use
    long cpu_time_ms;             // Total CPU time
} qemu_stats_t;

// ===== VM Lifecycle Management =====

// Create and start a QEMU VM
// Returns VM ID on success, NULL on failure
char* qemu_create_vm(const char* pod_name, const char* namespace,
                     const char* unikernel_image, int memory_mb, int vcpus);

// Start an existing QEMU VM
int qemu_start_vm(const char* vm_id);

// Pause a running QEMU VM
int qemu_pause_vm(const char* vm_id);

// Resume a paused QEMU VM
int qemu_resume_vm(const char* vm_id);

// Stop a QEMU VM gracefully
int qemu_stop_vm(const char* vm_id, int timeout_sec);

// Force kill a QEMU VM
int qemu_kill_vm(const char* vm_id);

// Delete a QEMU VM (must be stopped)
int qemu_delete_vm(const char* vm_id);

// ===== VM State Management =====

// Get VM state
qemu_vm_state_t qemu_get_state(const char* vm_id);

// Get VM details
qemu_vm_t* qemu_get_vm(const char* vm_id);

// List all VMs
int qemu_list_vms(qemu_vm_t** vms, int max_vms);

// Check if VM exists
int qemu_vm_exists(const char* vm_id);

// ===== VM Communication =====

// Send command to VM via QMP (QEMU Monitor Protocol)
// Returns response JSON string
char* qemu_send_qmp_command(const char* vm_id, const char* command);

// Execute command inside VM via serial console
// Returns command output
char* qemu_execute_command(const char* vm_id, const char* command, int timeout_sec);

// Get VM serial output (console logs)
char* qemu_get_serial_output(const char* vm_id, int lines);

// ===== Resource Management =====

// Set memory limit (live resize if supported)
int qemu_set_memory(const char* vm_id, int memory_mb);

// Set vCPU count (live hotplug if supported)
int qemu_set_vcpus(const char* vm_id, int vcpus);

// Get VM resource usage
int qemu_get_stats(const char* vm_id, qemu_stats_t* stats);

// ===== Network Integration =====

// Attach network interface to VM
int qemu_attach_network(const char* vm_id, const char* network_name, const char* mac_addr);

// Detach network interface from VM
int qemu_detach_network(const char* vm_id, const char* network_name);

// Get VM IP address
char* qemu_get_ip_address(const char* vm_id, const char* interface_name);

// ===== Storage Integration =====

// Attach block device to VM
int qemu_attach_block(const char* vm_id, const char* block_path, const char* device_id);

// Detach block device from VM
int qemu_detach_block(const char* vm_id, const char* device_id);

// ===== Configuration =====

// Initialize QEMU manager
// qemu_bin: Path to qemu-system-x86_64 binary
// vm_dir: Directory to store VM sockets and metadata
// Returns 0 on success
int qemu_manager_init(const char* qemu_bin, const char* vm_dir);

// Shutdown QEMU manager (stops all VMs)
int qemu_manager_shutdown();

// Set QEMU binary path
int qemu_set_binary_path(const char* qemu_bin);

// Set VM working directory
int qemu_set_vm_directory(const char* vm_dir);

// Enable/disable KVM acceleration
int qemu_set_kvm_enabled(int enabled);

// Set default VM memory
int qemu_set_default_memory(int memory_mb);

// Set default vCPU count
int qemu_set_default_vcpus(int vcpus);

#endif // SIRAH_QEMU_MANAGER_H
