#ifndef SIRAH_RUNTIME_H
#define SIRAH_RUNTIME_H

#include <stdint.h>
#include <sys/types.h>

typedef enum {
    RUNTIME_QEMU,
    RUNTIME_FIRECRACKER,
    RUNTIME_GVISOR
} runtime_type_t;

typedef struct {
    char* id;           // VM unique identifier (pod name)
    char* image;        // Path to unikernel image
    int memory_mb;      // Memory in MB
    int cpu_count;      // Number of vCPUs
    char* namespace;    // Kubernetes namespace
    char* pod_name;     // Pod name
    pid_t vm_pid;       // Process ID of running VM (-1 if not running)
    char* status;       // "pending", "running", "stopped", "failed"
} vm_spec_t;

typedef struct {
    runtime_type_t type;
    
    // Function pointers for runtime operations
    int (*init)(void);
    int (*spawn)(vm_spec_t* spec);
    int (*stop)(const char* vm_id);
    int (*get_status)(const char* vm_id, char* status_buf, size_t buf_size);
    int (*cleanup)(void);
} runtime_t;

// Global runtime instance
extern runtime_t* g_runtime;

// Initialize runtime with specified type
int runtime_init(runtime_type_t type);

// Get current runtime
runtime_t* runtime_get(void);

// Lifecycle operations
int runtime_spawn_vm(vm_spec_t* spec);
int runtime_stop_vm(const char* vm_id);
int runtime_get_vm_status(const char* vm_id, char* status, size_t size);
int runtime_cleanup(void);

#endif // SIRAH_RUNTIME_H
