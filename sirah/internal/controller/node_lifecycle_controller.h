// internal/controller/node_lifecycle_controller.h
// Node Lifecycle Controller
// Manages node join/leave detection, health monitoring, and pod eviction from unhealthy nodes

#ifndef NODE_LIFECYCLE_CONTROLLER_H
#define NODE_LIFECYCLE_CONTROLLER_H

#include <time.h>
#include <json-c/json.h>
#include <pthread.h>

// Maximum nodes and health check samples
#define NODE_MAX_NODES 1024
#define NODE_HEALTH_CHECK_INTERVAL 10      // 10 seconds
#define NODE_UNHEALTHY_THRESHOLD 3         // 3 failed checks = unhealthy
#define NODE_HEARTBEAT_TIMEOUT 30          // 30 seconds without heartbeat = offline

// Node health status
typedef enum {
    NODE_HEALTH_UNKNOWN,
    NODE_HEALTH_HEALTHY,
    NODE_HEALTH_UNHEALTHY,
    NODE_HEALTH_OFFLINE
} node_health_status_t;

// Node lifecycle state
typedef enum {
    NODE_STATE_UNKNOWN,
    NODE_STATE_INITIALIZING,
    NODE_STATE_READY,
    NODE_STATE_UNHEALTHY,
    NODE_STATE_DRAINING,
    NODE_STATE_DRAINED,
    NODE_STATE_REMOVED
} node_state_t;

// Node health metric
typedef struct {
    time_t timestamp;
    node_health_status_t status;
    int cpu_available_percent;      // Percent CPU available (0-100)
    int memory_available_percent;   // Percent memory available (0-100)
    int disk_available_percent;     // Percent disk available (0-100)
    int ready_pod_count;
    int total_pod_count;
    char reason[256];               // Reason for health status
} node_health_metric_t;

// Node lifecycle record
typedef struct {
    char name[256];
    node_state_t state;
    node_health_status_t health_status;
    
    int consecutive_unhealthy_checks;
    int total_pods;
    int ready_pods;
    
    time_t joined_at;
    time_t last_heartbeat;
    time_t health_changed_at;
    time_t drain_started_at;
    
    node_health_metric_t health_metrics[10];
    int metric_count;
    
    int cordoned;                   // Node is cordoned (no new pods)
    int draining;                   // Node is draining (evicting pods)
    char eviction_reason[256];
} node_lifecycle_record_t;

// Node lifecycle controller instance
typedef struct {
    node_lifecycle_record_t nodes[NODE_MAX_NODES];
    int node_count;
    
    pthread_mutex_t lock;
    int running;
    pthread_t health_check_thread;
    pthread_t eviction_thread;
} node_lifecycle_controller_t;

// External API for controller manager
int node_lifecycle_controller_init(void);
int node_lifecycle_controller_run(void);
int node_lifecycle_controller_shutdown(void);

// Node detection and registration
int node_lifecycle_register_node(const char* name);
int node_lifecycle_unregister_node(const char* name);
int node_lifecycle_node_heartbeat(const char* name);
int node_lifecycle_list_nodes(json_object** result);

// Node health monitoring
int node_lifecycle_update_health(const char* name, 
                                int cpu_available_pct,
                                int memory_available_pct,
                                int disk_available_pct);
int node_lifecycle_get_health(const char* name, node_health_status_t* status, char* reason, int reason_len);
int node_lifecycle_get_node_status(const char* name, json_object** result);

// Node lifecycle state management
int node_lifecycle_cordon_node(const char* name);
int node_lifecycle_uncordon_node(const char* name);
int node_lifecycle_drain_node(const char* name, const char* reason);
int node_lifecycle_get_node_state(const char* name, node_state_t* state);

// Pod eviction operations
int node_lifecycle_get_pods_for_eviction(const char* node_name, 
                                        char*** pod_names, char*** pod_namespaces, 
                                        int* count);
int node_lifecycle_evict_pod(const char* node_name, const char* pod_namespace, 
                            const char* pod_name);
int node_lifecycle_get_eviction_progress(const char* node_name, 
                                        int* total_pods, int* evicted_pods);

// Health monitoring queries
int node_lifecycle_list_unhealthy_nodes(char*** node_names, int* count);
int node_lifecycle_list_offline_nodes(char*** node_names, int* count);

// Background thread functions (internal)
void* node_lifecycle_health_check_thread(void* arg);
void* node_lifecycle_eviction_thread(void* arg);

#endif // NODE_LIFECYCLE_CONTROLLER_H
