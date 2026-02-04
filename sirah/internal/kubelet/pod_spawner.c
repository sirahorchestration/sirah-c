// internal/kubelet/pod_spawner.c
// Pod spawning integration for kubelet
// Connects pod status transitions to actual VM creation via QEMU

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <json-c/json.h>
#include "pod_spawner.h"
#include "qemu_manager.h"
#include "../../pkg/types/pod.h"

// ============================================================================
// Pod Spawning State Tracking
// ============================================================================

typedef struct {
    char pod_name[256];
    char namespace[256];
    char vm_id[512];
    int vm_pid;
    char image[512];
    int memory_mb;
    int cpu_count;
    time_t created_at;
    int spawned;
} spawned_pod_t;

#define MAX_SPAWNED_PODS 256
static spawned_pod_t g_spawned_pods[MAX_SPAWNED_PODS];
static int g_spawned_pod_count = 0;

// ============================================================================
// Find or create spawned pod entry
// ============================================================================

static spawned_pod_t* find_spawned_pod(const char* namespace, const char* pod_name) {
    for (int i = 0; i < g_spawned_pod_count; i++) {
        if (strcmp(g_spawned_pods[i].namespace, namespace) == 0 &&
            strcmp(g_spawned_pods[i].pod_name, pod_name) == 0) {
            return &g_spawned_pods[i];
        }
    }
    return NULL;
}

static spawned_pod_t* add_spawned_pod(const char* namespace, const char* pod_name) {
    if (g_spawned_pod_count >= MAX_SPAWNED_PODS) {
        fprintf(stderr, "[POD_SPAWNER] Max spawned pods reached\n");
        return NULL;
    }
    
    spawned_pod_t* pod = &g_spawned_pods[g_spawned_pod_count++];
    memset(pod, 0, sizeof(*pod));
    strncpy(pod->namespace, namespace, sizeof(pod->namespace) - 1);
    strncpy(pod->pod_name, pod_name, sizeof(pod->pod_name) - 1);
    snprintf(pod->vm_id, sizeof(pod->vm_id), "%s-%s-%ld",
             namespace, pod_name, time(NULL));
    pod->created_at = time(NULL);
    pod->spawned = 0;
    pod->vm_pid = -1;
    
    return pod;
}

// ============================================================================
// Extract container image from pod JSON
// ============================================================================

static char* extract_container_image(const char* pod_json) {
    if (!pod_json) return NULL;
    
    json_object* pod_obj = json_tokener_parse(pod_json);
    if (!pod_obj) return NULL;
    
    json_object* spec = json_object_object_get(pod_obj, "spec");
    if (!spec) {
        json_object_put(pod_obj);
        return NULL;
    }
    
    json_object* containers = json_object_object_get(spec, "containers");
    if (!containers || !json_object_is_type(containers, json_type_array)) {
        json_object_put(pod_obj);
        return NULL;
    }
    
    int count = json_object_array_length(containers);
    if (count == 0) {
        json_object_put(pod_obj);
        return NULL;
    }
    
    json_object* first_container = json_object_array_get_idx(containers, 0);
    if (!first_container) {
        json_object_put(pod_obj);
        return NULL;
    }
    
    json_object* image_obj = json_object_object_get(first_container, "image");
    if (!image_obj) {
        json_object_put(pod_obj);
        return NULL;
    }
    
    const char* image = json_object_get_string(image_obj);
    char* result = image ? strdup(image) : NULL;
    json_object_put(pod_obj);
    
    return result;
}

// ============================================================================
// Extract resource requests from pod JSON
// ============================================================================

static void extract_resources(const char* pod_json, int* memory_mb_out, int* cpu_out) {
    *memory_mb_out = 256;  // Default 256 MB
    *cpu_out = 1;          // Default 1 CPU
    
    if (!pod_json) return;
    
    json_object* pod_obj = json_tokener_parse(pod_json);
    if (!pod_obj) return;
    
    json_object* spec = json_object_object_get(pod_obj, "spec");
    if (!spec) {
        json_object_put(pod_obj);
        return;
    }
    
    json_object* containers = json_object_object_get(spec, "containers");
    if (!containers || !json_object_is_type(containers, json_type_array)) {
        json_object_put(pod_obj);
        return;
    }
    
    json_object* first_container = json_object_array_get_idx(containers, 0);
    if (!first_container) {
        json_object_put(pod_obj);
        return;
    }
    
    json_object* resources = json_object_object_get(first_container, "resources");
    if (!resources) {
        json_object_put(pod_obj);
        return;
    }
    
    json_object* limits = json_object_object_get(resources, "limits");
    if (limits) {
        json_object* memory_obj = json_object_object_get(limits, "memory");
        if (memory_obj) {
            const char* mem_str = json_object_get_string(memory_obj);
            if (mem_str) {
                // Parse memory string (e.g., "256Mi", "512M", "1Gi")
                int value = atoi(mem_str);
                if (strstr(mem_str, "Gi")) {
                    value *= 1024;  // GiB to MiB
                } else if (strstr(mem_str, "Mi")) {
                    // Already in MiB
                } else if (strstr(mem_str, "Ki")) {
                    value /= 1024;  // KiB to MiB
                }
                if (value > 0 && value <= 32768) {  // Sanity: 32GB max
                    *memory_mb_out = value;
                }
            }
        }
        
        json_object* cpu_obj = json_object_object_get(limits, "cpu");
        if (cpu_obj) {
            const char* cpu_str = json_object_get_string(cpu_obj);
            if (cpu_str) {
                // Parse CPU string (e.g., "1", "2", "500m")
                if (strstr(cpu_str, "m")) {
                    int millicpus = atoi(cpu_str);
                    int cpus = (millicpus + 999) / 1000;  // Round up
                    if (cpus > 0 && cpus <= 16) {
                        *cpu_out = cpus;
                    }
                } else {
                    int cpus = atoi(cpu_str);
                    if (cpus > 0 && cpus <= 16) {
                        *cpu_out = cpus;
                    }
                }
            }
        }
    }
    
    json_object_put(pod_obj);
}

// ============================================================================
// Public API - Pod Spawner Functions
// ============================================================================

int pod_spawner_spawn_pod(const char* namespace, const char* pod_name, const char* pod_json) {
    if (!namespace || !pod_name || !pod_json) {
        fprintf(stderr, "[POD_SPAWNER] Invalid arguments\n");
        return -1;
    }
    
    // Check if already spawned
    spawned_pod_t* existing = find_spawned_pod(namespace, pod_name);
    if (existing && existing->spawned) {
        fprintf(stderr, "[POD_SPAWNER] Pod %s/%s already spawned (VM: %s)\n",
                namespace, pod_name, existing->vm_id);
        return 0;  // Already spawned, success
    }
    
    // Extract image from pod spec
    char* image = extract_container_image(pod_json);
    if (!image) {
        fprintf(stderr, "[POD_SPAWNER] Failed to extract image from pod %s/%s\n",
                namespace, pod_name);
        return -1;
    }
    
    // Extract resource requirements
    int memory_mb, cpu_count;
    extract_resources(pod_json, &memory_mb, &cpu_count);
    
    fprintf(stderr, "[POD_SPAWNER] Spawning pod %s/%s (image: %s, memory: %dMB, cpus: %d)\n",
            namespace, pod_name, image, memory_mb, cpu_count);
    
    // Find or create spawned pod entry
    spawned_pod_t* spawned = existing ? existing : add_spawned_pod(namespace, pod_name);
    if (!spawned) {
        fprintf(stderr, "[POD_SPAWNER] Failed to create spawned pod entry\n");
        free(image);
        return -1;
    }
    
    // Copy pod information
    strncpy(spawned->image, image, sizeof(spawned->image) - 1);
    spawned->memory_mb = memory_mb;
    spawned->cpu_count = cpu_count;
    
    // Create VM specification
    qemu_vm_t* vm = qemu_create_vm(pod_name, namespace, image, memory_mb, cpu_count);
    if (!vm) {
        fprintf(stderr, "[POD_SPAWNER] Failed to create VM for pod %s/%s\n",
                namespace, pod_name);
        free(image);
        return -1;
    }
    
    // Start the VM
    if (qemu_start_vm(vm->vm_id) != 0) {
        fprintf(stderr, "[POD_SPAWNER] Failed to start VM for pod %s/%s\n",
                namespace, pod_name);
        free(image);
        return -1;
    }
    
    // Record VM launch
    spawned->vm_pid = vm->qemu_pid;
    spawned->spawned = 1;
    
    fprintf(stderr, "[POD_SPAWNER] Successfully spawned pod %s/%s (VM PID: %d)\n",
            namespace, pod_name, spawned->vm_pid);
    
    free(image);
    return 0;
}

int pod_spawner_get_pod_vm_id(const char* namespace, const char* pod_name,
                              char* vm_id_out, int vm_id_len) {
    if (!namespace || !pod_name || !vm_id_out) return -1;
    
    spawned_pod_t* spawned = find_spawned_pod(namespace, pod_name);
    if (!spawned) {
        vm_id_out[0] = '\0';
        return -1;
    }
    
    strncpy(vm_id_out, spawned->vm_id, vm_id_len - 1);
    vm_id_out[vm_id_len - 1] = '\0';
    return 0;
}

int pod_spawner_is_pod_running(const char* namespace, const char* pod_name) {
    spawned_pod_t* spawned = find_spawned_pod(namespace, pod_name);
    if (!spawned) return 0;
    
    if (!spawned->spawned) return 0;
    
    // Check if VM process is still alive
    if (spawned->vm_pid <= 0) return 0;
    
    // Try to send signal 0 to check if process exists
    if (kill(spawned->vm_pid, 0) == 0) {
        return 1;  // Process alive
    }
    
    return 0;  // Process dead
}

int pod_spawner_kill_pod(const char* namespace, const char* pod_name, int timeout_sec) {
    if (!namespace || !pod_name) return -1;
    
    spawned_pod_t* spawned = find_spawned_pod(namespace, pod_name);
    if (!spawned || !spawned->spawned) {
        fprintf(stderr, "[POD_SPAWNER] Pod %s/%s not running\n", namespace, pod_name);
        return -1;
    }
    
    fprintf(stderr, "[POD_SPAWNER] Killing pod %s/%s (VM: %s)\n",
            namespace, pod_name, spawned->vm_id);
    
    // Stop the VM
    qemu_stop_vm(spawned->vm_id, timeout_sec);
    
    spawned->spawned = 0;
    spawned->vm_pid = -1;
    
    return 0;
}

int pod_spawner_cleanup(void) {
    fprintf(stderr, "[POD_SPAWNER] Cleaning up %d spawned pods\n", g_spawned_pod_count);
    
    for (int i = 0; i < g_spawned_pod_count; i++) {
        if (g_spawned_pods[i].spawned) {
            qemu_stop_vm(g_spawned_pods[i].vm_id, 5);
        }
    }
    
    g_spawned_pod_count = 0;
    return 0;
}

int pod_spawner_get_stats(int* total_spawned, int* total_running) {
    if (!total_spawned || !total_running) return -1;
    
    *total_spawned = 0;
    *total_running = 0;
    
    for (int i = 0; i < g_spawned_pod_count; i++) {
        if (g_spawned_pods[i].spawned) {
            (*total_spawned)++;
            if (pod_spawner_is_pod_running(g_spawned_pods[i].namespace,
                                          g_spawned_pods[i].pod_name)) {
                (*total_running)++;
            }
        }
    }
    
    return 0;
}
