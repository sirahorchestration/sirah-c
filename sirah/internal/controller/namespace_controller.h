// internal/controller/namespace_controller.h
// Namespace Controller
// Manages namespace lifecycle, isolation, and quota enforcement

#ifndef NAMESPACE_CONTROLLER_H
#define NAMESPACE_CONTROLLER_H

#include <time.h>
#include <json-c/json.h>
#include <pthread.h>

#define NAMESPACE_MAX_NAMESPACES 1024
#define NAMESPACE_DEFAULT "default"

// Namespace phase states
typedef enum {
    NAMESPACE_PHASE_ACTIVE,
    NAMESPACE_PHASE_TERMINATING,
    NAMESPACE_PHASE_TERMINATED
} namespace_phase_t;

// Namespace isolation level
typedef enum {
    NAMESPACE_ISOLATION_NONE,
    NAMESPACE_ISOLATION_NETWORK,      // Network policies enforced
    NAMESPACE_ISOLATION_RESOURCE,     // Resource quotas enforced
    NAMESPACE_ISOLATION_FULL           // Network + Resource
} namespace_isolation_level_t;

// Network isolation rules
typedef struct {
    int ingress_restricted;         // Restrict ingress traffic
    int egress_restricted;          // Restrict egress traffic
    int allow_pod_to_pod;           // Allow pods to communicate
    int allow_external;             // Allow external traffic
} network_isolation_t;

// Namespace lifecycle record
typedef struct {
    char name[256];
    namespace_phase_t phase;
    namespace_isolation_level_t isolation_level;
    
    time_t created_at;
    time_t deleted_at;
    int deletion_grace_seconds;
    
    network_isolation_t network_isolation;
    
    // Resource tracking
    int pod_count;
    int service_count;
    int deployment_count;
    int configmap_count;
    int secret_count;
    
    // Labels and annotations
    char labels[1024];
    char annotations[2048];
    
    int owned_by_controller;
    int finalizers_present;
} namespace_record_t;

// Namespace initialization config
typedef struct {
    char name[256];
    char labels[1024];
    namespace_isolation_level_t isolation_level;
    int create_default_network_policy;
} namespace_config_t;

// Namespace controller instance
typedef struct {
    namespace_record_t namespaces[NAMESPACE_MAX_NAMESPACES];
    int namespace_count;
    
    pthread_mutex_t lock;
    int running;
    pthread_t cleanup_thread;
    
    // Statistics
    int total_created;
    int total_deleted;
    time_t last_cleanup;
} namespace_controller_t;

// External API for controller manager
int namespace_controller_init(void);
int namespace_controller_run(void);
int namespace_controller_shutdown(void);

// Namespace lifecycle
int namespace_controller_create(const namespace_config_t* config);
int namespace_controller_delete(const char* name);
int namespace_controller_get(const char* name, json_object** result);
int namespace_controller_list(json_object** result);
int namespace_controller_update(const char* name, const char* labels, const char* annotations);

// Namespace state management
int namespace_controller_get_phase(const char* name, namespace_phase_t* phase);
int namespace_controller_start_termination(const char* name, int grace_seconds);
int namespace_controller_finalize_deletion(const char* name);

// Isolation management
int namespace_controller_set_isolation(const char* name, namespace_isolation_level_t level);
int namespace_controller_get_isolation(const char* name, namespace_isolation_level_t* level);
int namespace_controller_configure_network_isolation(const char* name,
                                                    const network_isolation_t* isolation);

// Object counting
int namespace_controller_get_object_count(const char* name,
                                         int* pod_count,
                                         int* service_count,
                                         int* deployment_count,
                                         int* configmap_count,
                                         int* secret_count);

// Validation
int namespace_controller_validate_name(const char* name);
int namespace_controller_is_protected(const char* name);

// Resource cleanup
int namespace_controller_cleanup_resources(const char* name);
int namespace_controller_cleanup_orphaned_namespaces(int* cleanup_count);

// Background thread (internal)
void* namespace_controller_cleanup_thread(void* arg);

#endif // NAMESPACE_CONTROLLER_H
