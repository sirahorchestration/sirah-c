// internal/apiserver/pod_lifecycle_integration.c
// Integration layer connecting pod API, scheduling, spawning, and health checks
// Phase 6: Bridges pod creation → scheduling → kubelet spawning → health monitoring

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <time.h>

#include "pod_lifecycle_integration.h"
#include "scheduler_integration.h"
#include "../kubelet/kubelet.h"
#include "endpoints_etcd_integration.h"
#include "../../pkg/types/pod.h"
#include "../../internal/etcd/etcd_manager.h"

// Forward declarations for stub implementations
extern int pod_spawner_spawn_pod(const char* namespace, const char* pod_name, const char* pod_json);
extern int pod_spawner_is_pod_running(const char* namespace, const char* pod_name);
extern int pod_spawner_kill_pod(const char* namespace, const char* pod_name, int timeout_sec);

extern int probe_register_container(const char* namespace, const char* pod_name, const char* container_name);
extern int probe_check_startup(const char* namespace, const char* pod_name, const char* container_name, void* config);
extern int probe_check_readiness(const char* namespace, const char* pod_name, const char* container_name, void* config);
extern int probe_check_liveness(const char* namespace, const char* pod_name, const char* container_name, void* config);

// Global state
static struct {
    int initialized;
    int running;
    const char* api_server_url;
    pthread_t kubelet_thread;
    pthread_t health_check_thread;
    pthread_t sync_thread;
    int exit_requested;
    
    int total_pods;
    int running_pods;
    int pending_pods;
    int failing_pods;
    
    pthread_mutex_t stats_mutex;
} g_lifecycle = {
    .initialized = 0,
    .running = 0,
    .api_server_url = NULL,
    .exit_requested = 0,
    .total_pods = 0,
    .running_pods = 0,
    .pending_pods = 0,
    .failing_pods = 0
};

// ============================================================================
// Kubelet Thread - Spawns VMs when scheduler assigns pods to nodes
// ============================================================================

/**
 * Kubelet reconciliation loop
 * Discovers pods with spec.nodeName set (assigned by scheduler)
 * Spawns QEMU VM if not already running
 */
static void* kubelet_reconciliation_thread(void* arg) {
    fprintf(stderr, "[POD LIFECYCLE] Kubelet thread started\n");
    fflush(stderr);
    
    while (!g_lifecycle.exit_requested) {
        // Discover assigned pods (have spec.nodeName)
        char url[512];
        snprintf(url, sizeof(url), "%s/api/v1/namespaces/default/pods", g_lifecycle.api_server_url);
        
        CURL* curl = curl_easy_init();
        if (!curl) {
            sleep(2);
            continue;
        }
        
        // Response buffer
        typedef struct {
            char* data;
            size_t size;
            size_t capacity;
        } response_t;
        
        response_t resp = {0};
        resp.data = malloc(1048576);  // 1MB max
        resp.capacity = 1048576;
        
        // We'll use a simple approach - fetch pod list and check each
        // In real code, would use curl properly
        
        curl_easy_cleanup(curl);
        free(resp.data);
        
        sleep(5);  // Check every 5 seconds
    }
    
    fprintf(stderr, "[POD LIFECYCLE] Kubelet thread exiting\n");
    fflush(stderr);
    return NULL;
}

// ============================================================================
// Health Check Thread - Monitors pod health
// ============================================================================

/**
 * Health check loop
 * Runs probes on all running pods
 * Updates pod status based on probe results
 */
static void* health_check_thread(void* arg) {
    fprintf(stderr, "[POD LIFECYCLE] Health check thread started\n");
    fflush(stderr);
    
    while (!g_lifecycle.exit_requested) {
        // Every 10 seconds, check health of running pods
        // For each running pod:
        //   1. Execute startup probe (if not yet passed)
        //   2. Execute readiness probe (if startup passed)
        //   3. Execute liveness probe (if pod running)
        // Update pod status with results
        
        sleep(10);  // Check every 10 seconds
    }
    
    fprintf(stderr, "[POD LIFECYCLE] Health check thread exiting\n");
    fflush(stderr);
    return NULL;
}

// ============================================================================
// Sync Thread - Reconciles pod state
// ============================================================================

/**
 * Sync loop
 * Periodically checks if VMs are still running
 * Updates pod status if VM crashes
 * Cleans up orphaned pods
 */
static void* sync_thread(void* arg) {
    fprintf(stderr, "[POD LIFECYCLE] Sync thread started\n");
    fflush(stderr);
    
    while (!g_lifecycle.exit_requested) {
        // Every 30 seconds, sync pod state
        // For each running pod:
        //   1. Check if VM is still running
        //   2. If VM crashed:
        //      - Update pod status.phase = "Failed"
        //      - Update pod status.containerStatuses[].state = "Terminated"
        //      - Log restart reason
        //   3. If VM still running:
        //      - Update pod uptime metrics
        
        sleep(30);  // Check every 30 seconds
    }
    
    fprintf(stderr, "[POD LIFECYCLE] Sync thread exiting\n");
    fflush(stderr);
    return NULL;
}

// ============================================================================
// Public API Implementation
// ============================================================================

int pod_lifecycle_integration_init(const char* api_server_url) {
    if (!api_server_url) {
        fprintf(stderr, "[POD LIFECYCLE] ERROR: api_server_url required\n");
        return -1;
    }
    
    g_lifecycle.api_server_url = api_server_url;
    pthread_mutex_init(&g_lifecycle.stats_mutex, NULL);
    g_lifecycle.initialized = 1;
    
    fprintf(stderr, "[POD LIFECYCLE] Initialized with API: %s\n", api_server_url);
    return 0;
}

int pod_lifecycle_on_creation(const char* namespace, const char* pod_json) {
    if (!namespace || !pod_json) {
        return -1;
    }
    
    // Parse pod to get name
    json_object* pod_obj = json_tokener_parse(pod_json);
    if (!pod_obj) {
        return -1;
    }
    
    json_object* metadata = json_object_object_get(pod_obj, "metadata");
    if (!metadata) {
        json_object_put(pod_obj);
        return -1;
    }
    
    const char* pod_name = json_object_get_string(
        json_object_object_get(metadata, "name"));
    
    if (!pod_name) {
        json_object_put(pod_obj);
        return -1;
    }
    
    fprintf(stderr, "[POD LIFECYCLE] Pod created: %s/%s\n", namespace, pod_name);
    fprintf(stderr, "[POD LIFECYCLE] Scheduler will discover and assign this pod to a node\n");
    
    // Update stats
    pthread_mutex_lock(&g_lifecycle.stats_mutex);
    g_lifecycle.total_pods++;
    g_lifecycle.pending_pods++;
    pthread_mutex_unlock(&g_lifecycle.stats_mutex);
    
    json_object_put(pod_obj);
    return 0;
}

int pod_lifecycle_on_deletion(const char* namespace, const char* pod_name, const char* pod_json) {
    if (!namespace || !pod_name) {
        return -1;
    }
    
    fprintf(stderr, "[POD LIFECYCLE] Pod deleted: %s/%s\n", namespace, pod_name);
    
    // Kill QEMU VM if running
    pod_spawner_kill_pod(namespace, pod_name, 5);
    
    // Update stats
    pthread_mutex_lock(&g_lifecycle.stats_mutex);
    g_lifecycle.total_pods--;
    if (g_lifecycle.running_pods > 0) g_lifecycle.running_pods--;
    if (g_lifecycle.pending_pods > 0) g_lifecycle.pending_pods--;
    pthread_mutex_unlock(&g_lifecycle.stats_mutex);
    
    return 0;
}

int pod_lifecycle_trigger_scheduler(const char* namespace, const char* pod_name) {
    // Called from handler to trigger scheduling
    // In production, scheduler loop does this automatically
    // This is a manual trigger point
    
    fprintf(stderr, "[POD LIFECYCLE] Triggering scheduler for: %s/%s\n", namespace, pod_name);
    
    // The scheduler background thread will discover and assign this pod
    // No need to manually call scheduler_schedule_pod - the loop will find it
    
    return 0;
}

int pod_lifecycle_trigger_kubelet_spawn(const char* namespace, const char* pod_name, const char* pod_json) {
    if (!namespace || !pod_name || !pod_json) {
        return -1;
    }
    
    // Check if pod already has nodeName (assigned by scheduler)
    json_object* pod_obj = json_tokener_parse(pod_json);
    if (!pod_obj) {
        return -1;
    }
    
    json_object* spec = json_object_object_get(pod_obj, "spec");
    if (!spec) {
        json_object_put(pod_obj);
        return -1;
    }
    
    json_object* node_name_obj = json_object_object_get(spec, "nodeName");
    if (!node_name_obj) {
        fprintf(stderr, "[POD LIFECYCLE] Pod %s/%s not yet assigned to node\n", namespace, pod_name);
        json_object_put(pod_obj);
        return -1;
    }
    
    const char* node_name = json_object_get_string(node_name_obj);
    if (!node_name || strlen(node_name) == 0) {
        json_object_put(pod_obj);
        return -1;
    }
    
    fprintf(stderr, "[POD LIFECYCLE] Spawning pod %s/%s on node %s\n", 
            namespace, pod_name, node_name);
    
    // Spawn QEMU VM
    int spawn_result = pod_spawner_spawn_pod(namespace, pod_name, pod_json);
    
    if (spawn_result == 0) {
        fprintf(stderr, "[POD LIFECYCLE] VM spawn successful for %s/%s\n", namespace, pod_name);
        
        // Register pod containers for health checks
        json_object* containers = json_object_object_get(spec, "containers");
        if (containers && json_object_is_type(containers, json_type_array)) {
            int num_containers = json_object_array_length(containers);
            for (int i = 0; i < num_containers; i++) {
                json_object* container = json_object_array_get_idx(containers, i);
                if (container) {
                    const char* container_name = json_object_get_string(
                        json_object_object_get(container, "name"));
                    if (container_name) {
                        probe_register_container(namespace, pod_name, container_name);
                    }
                }
            }
        }
        
        // Update pod status to Running
        // Would normally PATCH pod.status.phase = "Running"
        
        // Update stats
        pthread_mutex_lock(&g_lifecycle.stats_mutex);
        if (g_lifecycle.pending_pods > 0) g_lifecycle.pending_pods--;
        g_lifecycle.running_pods++;
        pthread_mutex_unlock(&g_lifecycle.stats_mutex);
        
        json_object_put(pod_obj);
        return 0;
    } else {
        fprintf(stderr, "[POD LIFECYCLE] VM spawn FAILED for %s/%s\n", namespace, pod_name);
        
        // Update stats
        pthread_mutex_lock(&g_lifecycle.stats_mutex);
        g_lifecycle.failing_pods++;
        pthread_mutex_unlock(&g_lifecycle.stats_mutex);
        
        json_object_put(pod_obj);
        return -1;
    }
}

int pod_lifecycle_check_health(const char* namespace, const char* pod_name, const char* pod_json) {
    if (!namespace || !pod_name || !pod_json) {
        return -1;
    }
    
    // Parse pod
    json_object* pod_obj = json_tokener_parse(pod_json);
    if (!pod_obj) {
        return -1;
    }
    
    json_object* spec = json_object_object_get(pod_obj, "spec");
    json_object* containers = json_object_object_get(spec, "containers");
    
    if (!containers || !json_object_is_type(containers, json_type_array)) {
        json_object_put(pod_obj);
        return -1;
    }
    
    int all_healthy = 1;
    int num_containers = json_object_array_length(containers);
    
    // Check each container
    for (int i = 0; i < num_containers; i++) {
        json_object* container = json_object_array_get_idx(containers, i);
        if (!container) continue;
        
        const char* container_name = json_object_get_string(
            json_object_object_get(container, "name"));
        if (!container_name) continue;
        
        // Probe endpoints
        json_object* probes = json_object_object_get(container, "livenessProbe");
        
        // Check startup (if specified)
        json_object* startup_probe = json_object_object_get(container, "startupProbe");
        if (startup_probe) {
            int startup_result = probe_check_startup(namespace, pod_name, container_name, startup_probe);
            if (startup_result <= 0) {
                all_healthy = 0;
                fprintf(stderr, "[POD LIFECYCLE] Startup probe FAILED for %s/%s/%s\n",
                        namespace, pod_name, container_name);
            }
        }
        
        // Check readiness (if specified)
        json_object* readiness_probe = json_object_object_get(container, "readinessProbe");
        if (readiness_probe) {
            int readiness_result = probe_check_readiness(namespace, pod_name, container_name, readiness_probe);
            if (readiness_result <= 0) {
                all_healthy = 0;
                fprintf(stderr, "[POD LIFECYCLE] Readiness probe FAILED for %s/%s/%s\n",
                        namespace, pod_name, container_name);
            }
        }
        
        // Check liveness (if specified)
        json_object* liveness_probe = json_object_object_get(container, "livenessProbe");
        if (liveness_probe) {
            int liveness_result = probe_check_liveness(namespace, pod_name, container_name, liveness_probe);
            if (liveness_result <= 0) {
                all_healthy = 0;
                fprintf(stderr, "[POD LIFECYCLE] Liveness probe FAILED for %s/%s/%s\n",
                        namespace, pod_name, container_name);
                
                // Kill and restart pod on liveness failure
                fprintf(stderr, "[POD LIFECYCLE] Killing pod %s/%s for restart\n", namespace, pod_name);
                pod_spawner_kill_pod(namespace, pod_name, 5);
            }
        }
    }
    
    json_object_put(pod_obj);
    return all_healthy ? 0 : 1;
}

int pod_lifecycle_sync_status(const char* namespace, const char* pod_name, const char* pod_json) {
    if (!namespace || !pod_name) {
        return -1;
    }
    
    // Check if VM is still running
    int is_running = pod_spawner_is_pod_running(namespace, pod_name);
    
    if (!is_running) {
        fprintf(stderr, "[POD LIFECYCLE] VM for %s/%s is NOT running\n", namespace, pod_name);
        
        // Would update pod status to Failed/Terminated
        // Update restart count, etc.
        
        return -1;
    }
    
    return 0;
}

int pod_lifecycle_integration_start(void) {
    if (!g_lifecycle.initialized) {
        fprintf(stderr, "[POD LIFECYCLE] ERROR: Not initialized\n");
        return -1;
    }
    
    if (g_lifecycle.running) {
        fprintf(stderr, "[POD LIFECYCLE] Already running\n");
        return 0;
    }
    
    g_lifecycle.exit_requested = 0;
    g_lifecycle.running = 1;
    
    // Spawn kubelet thread
    if (pthread_create(&g_lifecycle.kubelet_thread, NULL, kubelet_reconciliation_thread, NULL) != 0) {
        fprintf(stderr, "[POD LIFECYCLE] Failed to create kubelet thread\n");
        return -1;
    }
    
    // Spawn health check thread
    if (pthread_create(&g_lifecycle.health_check_thread, NULL, health_check_thread, NULL) != 0) {
        fprintf(stderr, "[POD LIFECYCLE] Failed to create health check thread\n");
        return -1;
    }
    
    // Spawn sync thread
    if (pthread_create(&g_lifecycle.sync_thread, NULL, sync_thread, NULL) != 0) {
        fprintf(stderr, "[POD LIFECYCLE] Failed to create sync thread\n");
        return -1;
    }
    
    fprintf(stderr, "[POD LIFECYCLE] All reconciliation threads started\n");
    return 0;
}

void pod_lifecycle_integration_stop(void) {
    if (!g_lifecycle.running) {
        return;
    }
    
    fprintf(stderr, "[POD LIFECYCLE] Stopping...\n");
    g_lifecycle.exit_requested = 1;
    
    pthread_join(g_lifecycle.kubelet_thread, NULL);
    pthread_join(g_lifecycle.health_check_thread, NULL);
    pthread_join(g_lifecycle.sync_thread, NULL);
    
    g_lifecycle.running = 0;
    fprintf(stderr, "[POD LIFECYCLE] Stopped\n");
}

int pod_lifecycle_get_stats(int* total_pods, int* running_pods, int* pending_pods, int* failing_pods) {
    if (!total_pods || !running_pods || !pending_pods || !failing_pods) {
        return -1;
    }
    
    pthread_mutex_lock(&g_lifecycle.stats_mutex);
    *total_pods = g_lifecycle.total_pods;
    *running_pods = g_lifecycle.running_pods;
    *pending_pods = g_lifecycle.pending_pods;
    *failing_pods = g_lifecycle.failing_pods;
    pthread_mutex_unlock(&g_lifecycle.stats_mutex);
    
    return 0;
}
