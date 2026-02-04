// internal/controller/pod_eviction_controller.h
// Pod Eviction Controller
// Manages graceful pod eviction with disruption budget enforcement

#ifndef POD_EVICTION_CONTROLLER_H
#define POD_EVICTION_CONTROLLER_H

#include <time.h>
#include <json-c/json.h>
#include <pthread.h>

#define EVICTION_MAX_PODS 10000
#define EVICTION_MAX_PDBS 1024
#define EVICTION_GRACE_PERIOD_DEFAULT 30   // Default grace period (seconds)
#define EVICTION_POLL_INTERVAL 5           // Check eviction progress every 5 seconds

// Pod Disruption Budget (PDB) policy
typedef enum {
    PDB_POLICY_MIN_AVAILABLE,       // Minimum pods that must stay available
    PDB_POLICY_MAX_UNAVAILABLE      // Maximum pods that can be unavailable
} pdb_policy_type_t;

// PDB unhealthy pod policy
typedef enum {
    PDB_UNHEALTHY_POLICY_IF_HEALTHY_BUDGET_NOT_EXCEEDED,
    PDB_UNHEALTHY_POLICY_ALWAYS
} pdb_unhealthy_policy_t;

// Pod Disruption Budget
typedef struct {
    char name[256];
    char namespace[256];
    
    char selector[1024];            // Label selector for pods
    pdb_policy_type_t policy_type;
    int policy_value;               // Min available or max unavailable count
    
    pdb_unhealthy_policy_t unhealthy_policy;
    int temporary_disruptions_allowed;
    
    time_t created_at;
    int enabled;
} pod_disruption_budget_t;

// Eviction request
typedef struct {
    char pod_name[256];
    char pod_namespace[256];
    char pod_uid[128];
    
    char evicting_node[256];        // Node being drained
    int grace_period;               // Grace period in seconds
    char reason[256];               // Reason for eviction
    
    time_t eviction_requested_at;
    time_t eviction_deadline;
    int force_eviction;             // Force after grace period
    
    int evicted;                    // Eviction completed
    int failed;                     // Eviction failed
    char error_reason[256];
} eviction_request_t;

// PDB current status
typedef struct {
    char name[256];
    char namespace[256];
    
    int desired_healthy;            // Desired healthy pods
    int current_healthy;            // Currently healthy pods
    int disruptions_allowed;        // How many disruptions allowed
    int disruptions_active;         // Current disruptions
    
    time_t last_updated;
} pdb_status_t;

// Pod eviction controller
typedef struct {
    pod_disruption_budget_t pdbs[EVICTION_MAX_PDBS];
    int pdb_count;
    
    eviction_request_t evictions[EVICTION_MAX_PODS];
    int eviction_count;
    
    pdb_status_t pdb_statuses[EVICTION_MAX_PDBS];
    int pdb_status_count;
    
    pthread_mutex_t lock;
    int running;
    pthread_t eviction_thread;
    
    // Statistics
    int total_evictions;
    int successful_evictions;
    int failed_evictions;
} pod_eviction_controller_t;

// External API for controller manager
int pod_eviction_controller_init(void);
int pod_eviction_controller_run(void);
int pod_eviction_controller_shutdown(void);

// PDB management
int pod_eviction_create_pdb(const char* namespace, const char* name,
                           const char* selector, pdb_policy_type_t policy_type,
                           int policy_value);
int pod_eviction_update_pdb(const char* namespace, const char* name,
                           pdb_policy_type_t policy_type, int policy_value);
int pod_eviction_delete_pdb(const char* namespace, const char* name);
int pod_eviction_list_pdbs(const char* namespace, json_object** result);
int pod_eviction_get_pdb(const char* namespace, const char* name, json_object** result);

// PDB status queries
int pod_eviction_get_pdb_status(const char* namespace, const char* name, pdb_status_t* status);
int pod_eviction_can_disrupt_pod(const char* namespace, const char* pod_name);
int pod_eviction_get_disruptions_allowed(const char* namespace, const char* pdb_name,
                                        int* allowed);

// Eviction operations
int pod_eviction_request_eviction(const char* pod_namespace, const char* pod_name,
                                 const char* evicting_node, int grace_period,
                                 const char* reason);
int pod_eviction_execute_eviction(const char* pod_namespace, const char* pod_name,
                                 int grace_period);
int pod_eviction_cancel_eviction(const char* pod_namespace, const char* pod_name);
int pod_eviction_force_evict(const char* pod_namespace, const char* pod_name);

// Eviction status queries
int pod_eviction_get_eviction_status(const char* pod_namespace, const char* pod_name,
                                    eviction_request_t* request);
int pod_eviction_list_pending_evictions(json_object** result);
int pod_eviction_get_eviction_progress(int* total, int* evicted, int* failed);

// Batch operations
int pod_eviction_drain_node(const char* node_name, const char* reason,
                           int grace_period, int* eviction_count);
int pod_eviction_get_node_drain_status(const char* node_name,
                                      int* total_pods, int* evicted_pods,
                                      int* failed_pods);

// Background thread (internal)
void* pod_eviction_controller_thread(void* arg);

#endif // POD_EVICTION_CONTROLLER_H
