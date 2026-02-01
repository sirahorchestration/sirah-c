// internal/kubelet/unikernel_runtime.h
// Unikernel container runtime abstraction for Sirah
// Replaces traditional container runtimes (docker, containerd) with unikernel VMs
#ifndef SIRAH_UNIKERNEL_RUNTIME_H
#define SIRAH_UNIKERNEL_RUNTIME_H

typedef struct {
    char container_id[256];       // Container/VM ID
    char pod_name[256];           // Pod name
    char namespace[256];          // Namespace
    char image[512];              // Unikernel image path
    int memory_mb;                // Memory allocation
    int vcpus;                    // Virtual CPUs
    int running;                  // 1 if running, 0 otherwise
    char* logs;                   // Container logs
    int exit_code;                // Exit code when stopped
} unikernel_container_t;

// ===== Runtime Lifecycle =====

// Initialize unikernel runtime
// backend: "qemu" or "firecracker" (currently supports qemu)
// unikernel_dir: Directory containing unikernel images
int unikernel_runtime_init(const char* backend, const char* unikernel_dir);

// Shutdown runtime
int unikernel_runtime_shutdown();

// ===== Container/VM Management =====

// Create and run a container (VM)
// image: Path to unikernel image file
// Returns container ID
char* unikernel_container_create(const char* pod_name, const char* namespace,
                                 const char* image, int memory_mb, int vcpus);

// Run a created container
int unikernel_container_run(const char* container_id);

// Stop a running container
int unikernel_container_stop(const char* container_id, int timeout_sec);

// Kill a container (force)
int unikernel_container_kill(const char* container_id);

// Remove a container (must be stopped)
int unikernel_container_remove(const char* container_id);

// ===== Container State =====

// Get container state
int unikernel_container_inspect(const char* container_id, unikernel_container_t* info);

// Check if container exists
int unikernel_container_exists(const char* container_id);

// ===== Container I/O =====

// Get container logs
char* unikernel_container_logs(const char* container_id, int tail_lines);

// Execute command in container
char* unikernel_container_exec(const char* container_id, const char* command);

// ===== Image Management =====

// Check if image exists
int unikernel_image_exists(const char* image_path);

// Verify image integrity
int unikernel_image_verify(const char* image_path);

// Get image metadata
int unikernel_image_info(const char* image_path, char* info_buffer);

// ===== Network Integration =====

// Attach container to network
int unikernel_container_attach_network(const char* container_id, const char* network);

// Get container IP address
char* unikernel_container_get_ip(const char* container_id);

// ===== Stats & Monitoring =====

// Get container resource usage
int unikernel_container_stats(const char* container_id, 
                             long* cpu_ms, long* memory_bytes);

// Get system-wide runtime stats
int unikernel_runtime_stats(int* total_containers, int* running_containers,
                           long* total_memory_bytes);

#endif // SIRAH_UNIKERNEL_RUNTIME_H
