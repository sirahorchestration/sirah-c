// internal/controller/config_propagation_controller.h
// ConfigMap and Secret Propagation Controller
// Manages cross-namespace propagation of configurations

#ifndef CONFIG_PROPAGATION_CONTROLLER_H
#define CONFIG_PROPAGATION_CONTROLLER_H

#include <time.h>
#include <json-c/json.h>
#include <pthread.h>

#define CONFIG_MAX_PROPAGATIONS 10000
#define CONFIG_MAX_TARGETS 100
#define CONFIG_PROPAGATION_CHECK_INTERVAL 30  // Check every 30 seconds

// Propagation mode
typedef enum {
    CONFIG_PROPAGATE_DISABLED,
    CONFIG_PROPAGATE_COPY,            // Copy current values
    CONFIG_PROPAGATE_LINK,            // Link to original (watch for changes)
    CONFIG_PROPAGATE_MERGE             // Merge with existing
} config_propagation_mode_t;

// Propagation scope
typedef enum {
    CONFIG_SCOPE_NAMESPACE,           // Single namespace
    CONFIG_SCOPE_LABEL_SELECTED,      // Namespaces selected by labels
    CONFIG_SCOPE_CLUSTER              // All namespaces
} config_propagation_scope_t;

// Propagation target
typedef struct {
    char target_namespace[256];
    char target_name[256];            // Name in target namespace
    config_propagation_mode_t mode;
    int enabled;
    time_t last_sync;
    int sync_count;
    char last_error[256];
} config_target_t;

// Propagation rule record
typedef struct {
    char source_namespace[256];
    char source_name[256];
    char source_kind[64];             // "ConfigMap" or "Secret"
    
    config_propagation_scope_t scope;
    char scope_selector[1024];        // Label selector for scope
    
    config_target_t targets[CONFIG_MAX_TARGETS];
    int target_count;
    
    config_propagation_mode_t default_mode;
    int preserve_existing;            // Don't overwrite existing targets
    int auto_sync;                    // Automatically sync changes
    
    time_t created_at;
    time_t last_evaluated;
    int enabled;
} config_propagation_rule_t;

// Propagation event record
typedef struct {
    time_t timestamp;
    char source_namespace[256];
    char source_name[256];
    char target_namespace[256];
    char target_name[256];
    char event_type[32];              // "Created", "Updated", "Deleted", "Failed"
    char details[512];
} propagation_event_t;

// Propagation controller instance
typedef struct {
    config_propagation_rule_t rules[CONFIG_MAX_PROPAGATIONS];
    int rule_count;
    
    propagation_event_t events[CONFIG_MAX_PROPAGATIONS * 2];
    int event_count;
    
    pthread_mutex_t lock;
    int running;
    pthread_t propagation_thread;
    
    // Statistics
    int total_propagations;
    int successful_propagations;
    int failed_propagations;
    time_t last_evaluation;
} config_propagation_controller_t;

// External API for controller manager
int config_propagation_controller_init(void);
int config_propagation_controller_run(void);
int config_propagation_controller_shutdown(void);

// Propagation rule management
int config_propagation_create_rule(
    const char* source_namespace,
    const char* source_name,
    const char* source_kind,
    config_propagation_scope_t scope,
    const char* scope_selector,
    config_propagation_mode_t mode,
    int auto_sync
);

int config_propagation_delete_rule(
    const char* source_namespace,
    const char* source_name,
    const char* source_kind
);

int config_propagation_update_rule(
    const char* source_namespace,
    const char* source_name,
    const char* source_kind,
    config_propagation_scope_t scope,
    const char* scope_selector,
    config_propagation_mode_t mode
);

int config_propagation_get_rule(
    const char* source_namespace,
    const char* source_name,
    const char* source_kind,
    json_object** result
);

int config_propagation_list_rules(
    const char* source_namespace,
    json_object** result
);

// Propagation execution
int config_propagation_evaluate_rules(void);
int config_propagation_propagate_to_targets(
    const char* source_namespace,
    const char* source_name,
    const char* source_kind
);

int config_propagation_sync_target(
    const char* source_namespace,
    const char* source_name,
    const char* source_kind,
    const char* target_namespace,
    const char* target_name
);

// Target management
int config_propagation_add_target(
    const char* source_namespace,
    const char* source_name,
    const char* source_kind,
    const char* target_namespace,
    const char* target_name,
    config_propagation_mode_t mode
);

int config_propagation_remove_target(
    const char* source_namespace,
    const char* source_name,
    const char* source_kind,
    const char* target_namespace,
    const char* target_name
);

int config_propagation_list_targets(
    const char* source_namespace,
    const char* source_name,
    const char* source_kind,
    json_object** result
);

// Event tracking
int config_propagation_list_events(
    const char* source_namespace,
    json_object** result
);

int config_propagation_get_propagation_status(
    const char* source_namespace,
    const char* source_name,
    const char* source_kind,
    json_object** result
);

// Statistics
int config_propagation_get_statistics(
    int* total_rules,
    int* total_targets,
    int* successful_syncs,
    int* failed_syncs
);

// Background thread (internal)
void* config_propagation_controller_thread(void* arg);

#endif // CONFIG_PROPAGATION_CONTROLLER_H
