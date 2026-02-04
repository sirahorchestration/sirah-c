// internal/kubelet/kubelet_lifecycle.h
// Kubelet pod lifecycle management with probes and metrics

#ifndef SIRAH_KUBELET_LIFECYCLE_H
#define SIRAH_KUBELET_LIFECYCLE_H

#include "health_probe.h"
#include "metrics.h"
#include "../../pkg/types/pod.h"
#include <time.h>

// Pod container state on kubelet
typedef struct {
    char* pod_name;
    char* namespace;
    
    // Container health states
    container_health_t** containers;
    int num_containers;
    
    // Pod-level state
    enum {
        POD_STATE_INIT = 0,
        POD_STATE_WAITING = 1,
        POD_STATE_RUNNING = 2,
        POD_STATE_SUCCEEDED = 3,
        POD_STATE_FAILED = 4,
        POD_STATE_TERMINATING = 5
    } state;
    
    // Pod readiness (all containers ready)
    int is_ready;
    
    // Timestamps
    time_t created_time;
    time_t started_time;
    time_t last_health_check;
    
    // VM/Container IDs
    char* vm_id;
    int vm_pid;
    
    // Resource allocation
    int memory_mb;
    int cpu_millicores;
    
    // Volume info
    char** mounted_volumes;
    int num_mounted_volumes;
} pod_container_state_t;

// ============================================================================
// Pod Lifecycle Operations
// ============================================================================

/**
 * Create pod container state from pod spec
 */
pod_container_state_t* kubelet_pod_state_create(const char* pod_name, const char* namespace,
                                                 const k8s_pod_t* pod);

/**
 * Free pod container state
 */
void kubelet_pod_state_free(pod_container_state_t* state);

/**
 * Initialize pod on kubelet (prepare containers, mount volumes)
 */
int kubelet_pod_init(pod_container_state_t* state, const char* api_server_url);

/**
 * Start pod containers (spawn VMs/containers)
 */
int kubelet_pod_start(pod_container_state_t* state, const char* vm_image_path);

/**
 * Run pod health checks (startup, readiness, liveness probes)
 */
int kubelet_pod_check_health(pod_container_state_t* state, metrics_collector_t* metrics);

/**
 * Update pod status based on container health
 */
int kubelet_pod_update_status(pod_container_state_t* state, const char* api_server_url);

/**
 * Handle pod termination (cleanup, stop containers, unmount volumes)
 */
int kubelet_pod_terminate(pod_container_state_t* state, const char* api_server_url);

/**
 * Check if pod should be restarted based on liveness probes
 */
int kubelet_pod_check_restart_needed(pod_container_state_t* state, metrics_collector_t* metrics);

// ============================================================================
// Container Operations
// ============================================================================

/**
 * Start single container in pod
 */
int kubelet_container_start(pod_container_state_t* state, int container_idx,
                            const char* vm_image_path);

/**
 * Restart container (increment restart count, reset health)
 */
int kubelet_container_restart(pod_container_state_t* state, int container_idx,
                              const char* vm_image_path, const char* api_server_url);

/**
 * Stop container
 */
int kubelet_container_stop(pod_container_state_t* state, int container_idx);

/**
 * Check container health and execute probes
 */
int kubelet_container_check_health(pod_container_state_t* state, int container_idx,
                                   metrics_collector_t* metrics);

// ============================================================================
// Diagnostics
// ============================================================================

/**
 * Get human-readable pod state summary
 */
char* kubelet_pod_state_summary(pod_container_state_t* state, char* buf, int buf_len);

/**
 * Get container state info
 */
char* kubelet_container_state_summary(pod_container_state_t* state, int container_idx,
                                      char* buf, int buf_len);

/**
 * Check pod readiness (all containers ready and healthy)
 */
int kubelet_pod_is_ready(pod_container_state_t* state);

#endif // SIRAH_KUBELET_LIFECYCLE_H
