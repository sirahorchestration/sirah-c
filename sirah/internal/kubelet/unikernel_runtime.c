// internal/kubelet/unikernel_runtime.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include "unikernel_runtime.h"
#include "qemu_manager.h"

#define MAX_CONTAINERS 1000

static struct {
    char backend[64];                     // "qemu" or "firecracker"
    char unikernel_dir[512];              // Directory with unikernel images
    unikernel_container_t containers[MAX_CONTAINERS];
    int container_count;
} g_runtime = {0};

// ===== Utility Functions =====

static unikernel_container_t* find_container(const char* container_id) {
    for (int i = 0; i < g_runtime.container_count; i++) {
        if (strcmp(g_runtime.containers[i].container_id, container_id) == 0) {
            return &g_runtime.containers[i];
        }
    }
    return NULL;
}

static char* generate_container_id(const char* pod_name, const char* namespace) {
    static char id[256];
    snprintf(id, sizeof(id), "%s-%s-%ld", namespace, pod_name, time(NULL));
    return id;
}

static char* resolve_image_path(const char* image) {
    static char full_path[512];
    
    // If absolute path, use as-is
    if (image[0] == '/') {
        strcpy(full_path, image);
    } else {
        // Relative path - look in unikernel directory
        snprintf(full_path, sizeof(full_path), 
                 "%s/%s", g_runtime.unikernel_dir, image);
    }
    
    return full_path;
}

// ===== Runtime Lifecycle =====

int unikernel_runtime_init(const char* backend, const char* unikernel_dir) {
    if (!backend || !unikernel_dir) return -1;
    
    strcpy(g_runtime.backend, backend);
    strcpy(g_runtime.unikernel_dir, unikernel_dir);
    g_runtime.container_count = 0;
    
    // Initialize QEMU backend if selected
    if (strcmp(backend, "qemu") == 0) {
        char vm_dir[512];
        snprintf(vm_dir, sizeof(vm_dir), "%s/.vms", unikernel_dir);
        
        if (qemu_manager_init("/usr/bin/qemu-system-x86_64", vm_dir) != 0) {
            return -1;
        }
    }
    
    return 0;
}

int unikernel_runtime_shutdown() {
    // Stop all running containers
    for (int i = 0; i < g_runtime.container_count; i++) {
        if (g_runtime.containers[i].running) {
            unikernel_container_stop(g_runtime.containers[i].container_id, 5);
        }
    }
    
    if (strcmp(g_runtime.backend, "qemu") == 0) {
        qemu_manager_shutdown();
    }
    
    return 0;
}

// ===== Container/VM Management =====

char* unikernel_container_create(const char* pod_name, const char* namespace,
                                 const char* image, int memory_mb, int vcpus) {
    if (g_runtime.container_count >= MAX_CONTAINERS) {
        return NULL;
    }
    
    char* image_path = resolve_image_path(image);
    if (access(image_path, F_OK) != 0) {
        return NULL;  // Image not found
    }
    
    if (strcmp(g_runtime.backend, "qemu") == 0) {
        // Create QEMU VM
        char* vm_id = qemu_create_vm(pod_name, namespace, image_path, memory_mb, vcpus);
        if (!vm_id) return NULL;
        
        // Add to containers list
        unikernel_container_t* container = &g_runtime.containers[g_runtime.container_count++];
        strcpy(container->container_id, vm_id);
        strcpy(container->pod_name, pod_name);
        strcpy(container->namespace, namespace);
        strcpy(container->image, image_path);
        container->memory_mb = memory_mb;
        container->vcpus = vcpus;
        container->running = 0;
        container->logs = calloc(1, 4096);
        container->exit_code = -1;
        
        return container->container_id;
    }
    
    return NULL;  // Unsupported backend
}

int unikernel_container_run(const char* container_id) {
    unikernel_container_t* container = find_container(container_id);
    if (!container || container->running) return -1;
    
    if (strcmp(g_runtime.backend, "qemu") == 0) {
        if (qemu_start_vm(container_id) != 0) {
            return -1;
        }
        container->running = 1;
        return 0;
    }
    
    return -1;
}

int unikernel_container_stop(const char* container_id, int timeout_sec) {
    unikernel_container_t* container = find_container(container_id);
    if (!container || !container->running) return 0;
    
    if (strcmp(g_runtime.backend, "qemu") == 0) {
        if (qemu_stop_vm(container_id, timeout_sec) != 0) {
            return -1;
        }
        container->running = 0;
        
        // Get exit code from QEMU VM
        qemu_vm_t* vm = qemu_get_vm(container_id);
        if (vm) {
            container->exit_code = vm->exit_code;
        }
        
        return 0;
    }
    
    return -1;
}

int unikernel_container_kill(const char* container_id) {
    unikernel_container_t* container = find_container(container_id);
    if (!container) return -1;
    
    if (strcmp(g_runtime.backend, "qemu") == 0) {
        qemu_kill_vm(container_id);
        container->running = 0;
        container->exit_code = -1;
        return 0;
    }
    
    return -1;
}

int unikernel_container_remove(const char* container_id) {
    unikernel_container_t* container = find_container(container_id);
    if (!container || container->running) return -1;
    
    if (strcmp(g_runtime.backend, "qemu") == 0) {
        qemu_delete_vm(container_id);
    }
    
    // Remove from list
    if (container->logs) {
        free(container->logs);
    }
    
    for (int i = 0; i < g_runtime.container_count; i++) {
        if (&g_runtime.containers[i] == container) {
            for (int j = i; j < g_runtime.container_count - 1; j++) {
                g_runtime.containers[j] = g_runtime.containers[j + 1];
            }
            g_runtime.container_count--;
            return 0;
        }
    }
    
    return 0;
}

// ===== Container State =====

int unikernel_container_inspect(const char* container_id, unikernel_container_t* info) {
    if (!info) return -1;
    
    unikernel_container_t* container = find_container(container_id);
    if (!container) return -1;
    
    memcpy(info, container, sizeof(*info));
    info->logs = NULL;  // Don't copy log data
    
    return 0;
}

int unikernel_container_exists(const char* container_id) {
    return find_container(container_id) != NULL;
}

// ===== Container I/O =====

char* unikernel_container_logs(const char* container_id, int tail_lines) {
    unikernel_container_t* container = find_container(container_id);
    if (!container) return NULL;
    
    if (strcmp(g_runtime.backend, "qemu") == 0) {
        return qemu_get_serial_output(container_id, tail_lines);
    }
    
    return NULL;
}

char* unikernel_container_exec(const char* container_id, const char* command) {
    unikernel_container_t* container = find_container(container_id);
    if (!container || !container->running) return NULL;
    
    if (strcmp(g_runtime.backend, "qemu") == 0) {
        return qemu_execute_command(container_id, command, 30);
    }
    
    return NULL;
}

// ===== Image Management =====

int unikernel_image_exists(const char* image_path) {
    char* full_path = resolve_image_path(image_path);
    return access(full_path, F_OK) == 0;
}

int unikernel_image_verify(const char* image_path) {
    char* full_path = resolve_image_path(image_path);
    
    FILE* f = fopen(full_path, "rb");
    if (!f) return -1;
    
    // Check for ELF magic number (simplified verification)
    unsigned char magic[4];
    if (fread(magic, 1, 4, f) == 4) {
        fclose(f);
        // ELF magic: 0x7F 'E' 'L' 'F'
        if (magic[0] == 0x7F && magic[1] == 'E' && 
            magic[2] == 'L' && magic[3] == 'F') {
            return 0;  // Valid ELF binary
        }
    }
    fclose(f);
    
    return -1;  // Invalid image
}

int unikernel_image_info(const char* image_path, char* info_buffer) {
    if (!info_buffer) return -1;
    
    char* full_path = resolve_image_path(image_path);
    
    struct stat st;
    if (stat(full_path, &st) != 0) {
        sprintf(info_buffer, "{\"error\":\"image not found\"}");
        return -1;
    }
    
    // Return basic image info
    sprintf(info_buffer, 
            "{\"path\":\"%s\",\"size\":%ld,\"verified\":%d}",
            full_path, st.st_size,
            unikernel_image_verify(image_path) == 0 ? 1 : 0);
    
    return 0;
}

// ===== Network Integration =====

int unikernel_container_attach_network(const char* container_id, const char* network) {
    unikernel_container_t* container = find_container(container_id);
    if (!container) return -1;
    
    if (strcmp(g_runtime.backend, "qemu") == 0) {
        return qemu_attach_network(container_id, network, NULL);
    }
    
    return -1;
}

char* unikernel_container_get_ip(const char* container_id) {
    unikernel_container_t* container = find_container(container_id);
    if (!container || !container->running) return NULL;
    
    if (strcmp(g_runtime.backend, "qemu") == 0) {
        return qemu_get_ip_address(container_id, "eth0");
    }
    
    return NULL;
}

// ===== Stats & Monitoring =====

int unikernel_container_stats(const char* container_id, 
                             long* cpu_ms, long* memory_bytes) {
    if (!cpu_ms || !memory_bytes) return -1;
    
    unikernel_container_t* container = find_container(container_id);
    if (!container) return -1;
    
    // TODO: Implement actual stats collection from QEMU
    *cpu_ms = 0;
    *memory_bytes = (long)container->memory_mb * 1024 * 1024;
    
    return 0;
}

int unikernel_runtime_stats(int* total_containers, int* running_containers,
                           long* total_memory_bytes) {
    if (!total_containers || !running_containers || !total_memory_bytes) {
        return -1;
    }
    
    *total_containers = g_runtime.container_count;
    *running_containers = 0;
    *total_memory_bytes = 0;
    
    for (int i = 0; i < g_runtime.container_count; i++) {
        if (g_runtime.containers[i].running) {
            (*running_containers)++;
        }
        *total_memory_bytes += (long)g_runtime.containers[i].memory_mb * 1024 * 1024;
    }
    
    return 0;
}
