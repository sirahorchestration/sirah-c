#ifndef SIRAH_SCHEDULER_H
#define SIRAH_SCHEDULER_H

#include <time.h>
#include <pthread.h>

// Pod resource request
typedef struct {
    int cpu_millicores;     // 100, 1000, 2000, etc.
    long memory_bytes;      // 128Mi, 512Mi, 1Gi, etc.
    long ephemeral_storage; // Optional
} pod_request_t;

// Node resource state
typedef struct {
    char* node_name;
    char* status;           // Ready, NotReady, Cordoned, Draining
    int allocatable_cpus;   // millicores
    long allocatable_memory; // bytes
    int allocated_cpus;     // Currently allocated
    long allocated_memory;  // Currently allocated
    int pod_count;          // Current pods on node
    int max_pods;           // Capacity (default 110)
    
    // Taints
    char** taints;
    int taint_count;
    
    // Labels
    char** label_keys;
    char** label_values;
    int label_count;
    
    time_t last_heartbeat;
} node_state_t;

// Pod scheduling information
typedef struct {
    char* pod_name;
    char* namespace;
    pod_request_t request;
    
    // Affinity rules
    char** pod_affinity_keys;      // Pod must be with pods labeled with these
    int pod_affinity_count;
    char** pod_anti_affinity_keys; // Pod must NOT be with pods labeled with these
    int pod_anti_affinity_count;
    
    // Node affinity
    char** required_node_labels;   // Must have these labels
    int required_count;
    char** preferred_node_labels;  // Prefer these labels
    int preferred_count;
    
    // Tolerations for taints
    char** tolerated_taints;
    int toleration_count;
    
    int priority;                  // Higher priority scheduled first
    int preemption_enabled;        // Can preempt lower priority pods
} pod_spec_t;

// Scoring result for a pod-node pair
typedef struct {
    char* node_name;
    int score;                     // 0-100
    int feasible;                  // 1 if node meets constraints, 0 otherwise
    char* infeasibility_reason;    // Why node is not feasible
} node_score_t;

// Scheduler state
typedef struct {
    node_state_t* nodes;
    int node_count;
    int node_capacity;
    
    // Pod assignments: pod_name -> node_name
    char** assigned_pods;
    char** assigned_nodes;
    int assignment_count;
    int assignment_capacity;
    
    int running;
    pthread_mutex_t lock;
    
    // Configuration
    int enable_bin_packing;      // Default: 1 (enabled)
    int enable_pod_spread;       // Default: 1 (enabled)
    int enable_affinity;         // Default: 1 (enabled)
    int enable_topology_spread;  // Default: 1 (enabled)
    
    // Scoring weights
    int weight_bin_packing;      // How much to prefer full nodes
    int weight_pod_spread;       // How much to spread pods
    int weight_affinity;         // How much to prefer affinity matches
    
} scheduler_t;

// Scheduler lifecycle
scheduler_t* scheduler_new(void);
void scheduler_free(scheduler_t* scheduler);
int scheduler_init(scheduler_t* scheduler);
int scheduler_run(scheduler_t* scheduler);
void scheduler_shutdown(scheduler_t* scheduler);

// Node management
int scheduler_add_node(scheduler_t* scheduler, const char* node_name, 
                       int cpu_millicores, long memory_bytes);
int scheduler_update_node_status(scheduler_t* scheduler, const char* node_name, 
                                  const char* status);
int scheduler_add_node_label(scheduler_t* scheduler, const char* node_name, 
                             const char* key, const char* value);
int scheduler_add_node_taint(scheduler_t* scheduler, const char* node_name, 
                            const char* taint);
int scheduler_remove_node_taint(scheduler_t* scheduler, const char* node_name, 
                               const char* taint);
int scheduler_cordon_node(scheduler_t* scheduler, const char* node_name);
int scheduler_uncordon_node(scheduler_t* scheduler, const char* node_name);
int scheduler_drain_node(scheduler_t* scheduler, const char* node_name);

// Pod scheduling
int scheduler_schedule_pod(scheduler_t* scheduler, pod_spec_t* pod);
char* scheduler_find_best_node(scheduler_t* scheduler, pod_spec_t* pod);
node_score_t* scheduler_score_nodes(scheduler_t* scheduler, pod_spec_t* pod, 
                                    int* score_count);
int scheduler_bind_pod(scheduler_t* scheduler, const char* pod_name, 
                       const char* node_name);
int scheduler_evict_pod(scheduler_t* scheduler, const char* pod_name);

// Node queries
node_state_t* scheduler_get_node(scheduler_t* scheduler, const char* node_name);
int scheduler_get_node_utilization(scheduler_t* scheduler, const char* node_name, 
                                   int* cpu_percent, int* memory_percent);
node_state_t* scheduler_get_all_nodes(scheduler_t* scheduler, int* count);

// Resource checking
int scheduler_check_resource_fit(scheduler_t* scheduler, const char* node_name, 
                                 pod_request_t* request);
int scheduler_check_affinity_match(scheduler_t* scheduler, const char* node_name, 
                                   pod_spec_t* pod);
int scheduler_check_toleration_match(scheduler_t* scheduler, const char* node_name, 
                                     pod_spec_t* pod);

#endif // SIRAH_SCHEDULER_H
