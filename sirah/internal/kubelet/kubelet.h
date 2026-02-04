#ifndef SIRAH_KUBELET_H
#define SIRAH_KUBELET_H

#include "../storage/store.h"
#include "../../pkg/lifecycle/pod_lifecycle.h"
#include "volumes.h"
#include "unikernel_runtime.h"
#include "health_probe.h"
#include "metrics.h"
#include "kubelet_lifecycle.h"
#include <time.h>

// Kubelet represents a node agent
typedef struct {
    char* node_name;
    char* api_server_url;
    void* curl_handle;
    int update_interval;  // Seconds between status updates
    char* pod_cidr;       // CIDR for pod IPs on this node
    int last_ip_octet;    // Counter for assigning IPs
    
    char* pod_mount_base; // Base directory for pod volume mounts
    
    // Unikernel runtime for pod execution (QEMU backend)
    void* runtime;        // unikernel_runtime_t*
    
    // Managed pods - new lifecycle model
    pod_container_state_t** pod_states;
    int num_pod_states;
    
    // Legacy pod tracking (maintained for compatibility)
    pod_lifecycle_t** managed_pods;
    int num_managed_pods;
    
    // Metrics collector
    metrics_collector_t* metrics;
    
    time_t last_sync;
    time_t last_health_check;
} kubelet_t;

// Kubelet operations
kubelet_t* kubelet_new(const char* node_name, const char* api_server_url);
void kubelet_free(kubelet_t* kubelet);
int kubelet_init(kubelet_t* kubelet);
void kubelet_shutdown(kubelet_t* kubelet);
int kubelet_run(kubelet_t* kubelet);

// Pod management on node
typedef struct {
    char* pod_name;
    char* namespace;
    char* status;           // "running", "terminated", "pending"
    int restart_count;
    int exit_code;
    char* error_message;
} kubelet_pod_status_t;

// Get pods assigned to this node
int kubelet_get_assigned_pods(kubelet_t* kubelet, kubelet_pod_status_t** pods, int* count);

// Update pod status back to API server
int kubelet_update_pod_status(kubelet_t* kubelet, const char* namespace, const char* pod_name,
                             const char* phase, const char* message);

// Handle pod volumes
int kubelet_mount_pod_volumes(kubelet_t* kubelet, const char* namespace, const char* pod_name,
                             volume_definition_t* volumes, int num_volumes);

// Clean up pod volumes
int kubelet_cleanup_volumes(kubelet_t* kubelet, const char* namespace, const char* pod_name);

// ============================================================================
// Health & Metrics (Phase 4)
// ============================================================================

/**
 * Execute health checks on all managed pods
 */
int kubelet_run_health_checks(kubelet_t* kubelet);

/**
 * Get kubelet metrics for monitoring
 */
metrics_collector_t* kubelet_get_metrics(kubelet_t* kubelet);

/**
 * Generate Prometheus metrics output
 */
char* kubelet_get_metrics_prometheus(kubelet_t* kubelet);

/**
 * Check kubelet health status
 */
int kubelet_health_check(kubelet_t* kubelet);

#endif
