#ifndef K8S_HPA_H
#define K8S_HPA_H

#include <stdbool.h>
#include <time.h>

// HPA metric type
typedef enum {
    K8S_HPA_METRIC_CPU = 1,
    K8S_HPA_METRIC_MEMORY = 2,
    K8S_HPA_METRIC_CUSTOM = 3
} k8s_hpa_metric_type_t;

// Single metric specification
typedef struct {
    k8s_hpa_metric_type_t type;
    char* name;
    int target_value;  // Target percentage for CPU/Memory
    int current_value; // Current value for comparison
} k8s_hpa_metric_spec_t;

// HPA specification
typedef struct {
    char* scale_target_ref;      // Deployment, StatefulSet, etc.
    int min_replicas;
    int max_replicas;
    int target_cpu_utilization;
    int target_memory_utilization;
    
    k8s_hpa_metric_spec_t** custom_metrics;
    int num_custom_metrics;
    
    // Scaling behavior
    int upscale_window_seconds;
    int downscale_window_seconds;
    int upscale_policy_percent;
    int downscale_policy_percent;
} k8s_hpa_spec_t;

// HPA status
typedef struct {
    int current_replicas;
    int desired_replicas;
    int current_cpu_utilization;
    int current_memory_utilization;
    
    time_t last_scale_time;
    int last_scale_replicas;
    
    char* conditions;
    time_t last_update_time;
} k8s_hpa_status_t;

// HPA condition
typedef struct {
    char* type;       // Ready, ScalingInProgress, etc.
    char* status;     // True, False, Unknown
    time_t last_transition_time;
    char* reason;
    char* message;
} k8s_hpa_condition_t;

// Full HPA object
typedef struct {
    struct {
        char* name;
        char* namespace;
        char* uid;
        char** labels;
        int num_labels;
        time_t creation_timestamp;
    } metadata;
    
    k8s_hpa_spec_t* spec;
    k8s_hpa_status_t* status;
    k8s_hpa_condition_t** conditions;
    int num_conditions;
} k8s_hpa_t;

// ============ HPA Functions ============

// Spec lifecycle
k8s_hpa_spec_t* k8s_hpa_spec_new();
void k8s_hpa_spec_free(k8s_hpa_spec_t* spec);

// Status lifecycle
k8s_hpa_status_t* k8s_hpa_status_new();
void k8s_hpa_status_free(k8s_hpa_status_t* status);

// HPA lifecycle
k8s_hpa_t* k8s_hpa_new(const char* name, const char* namespace);
void k8s_hpa_free(k8s_hpa_t* hpa);

// Configuration
int k8s_hpa_set_scale_target(k8s_hpa_t* hpa, const char* target_ref);
int k8s_hpa_set_replicas(k8s_hpa_t* hpa, int min_replicas, int max_replicas);
int k8s_hpa_set_target_cpu(k8s_hpa_t* hpa, int target_percent);
int k8s_hpa_set_target_memory(k8s_hpa_t* hpa, int target_percent);
int k8s_hpa_add_custom_metric(k8s_hpa_t* hpa, const char* name, k8s_hpa_metric_type_t type, int target_value);

// Behavior configuration
int k8s_hpa_set_upscale_behavior(k8s_hpa_t* hpa, int window_seconds, int percent);
int k8s_hpa_set_downscale_behavior(k8s_hpa_t* hpa, int window_seconds, int percent);

// Status tracking
int k8s_hpa_update_current_metrics(k8s_hpa_t* hpa, int cpu_util, int mem_util);
int k8s_hpa_set_replicas_count(k8s_hpa_t* hpa, int current, int desired);
int k8s_hpa_mark_scaled(k8s_hpa_t* hpa, int replica_count);

// Condition management
int k8s_hpa_add_condition(k8s_hpa_t* hpa, const char* type, const char* status, const char* reason, const char* message);
int k8s_hpa_clear_conditions(k8s_hpa_t* hpa);

// Serialization
char* k8s_hpa_to_json(k8s_hpa_t* hpa);
k8s_hpa_t* k8s_hpa_from_json(const char* json_str);

// Utility
bool k8s_hpa_can_scale_up(k8s_hpa_t* hpa);
bool k8s_hpa_can_scale_down(k8s_hpa_t* hpa);
int k8s_hpa_calculate_desired_replicas(k8s_hpa_t* hpa, int current_utilization, int target_utilization);

#endif
