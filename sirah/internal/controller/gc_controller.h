// internal/controller/gc_controller.h
// Garbage Collection Controller
// Manages orphaned resource cleanup, finalizer support, and object deletion cascades

#ifndef GC_CONTROLLER_H
#define GC_CONTROLLER_H

#include <time.h>
#include <json-c/json.h>
#include <pthread.h>

#define GC_MAX_OBJECTS 10000
#define GC_MAX_OWNERSHIP_REFS 5
#define GC_SCAN_INTERVAL 30        // Scan every 30 seconds
#define GC_GRACE_PERIOD 60         // Grace period before cleanup (seconds)

// GC propagation policy
typedef enum {
    GC_PROPAGATION_ORPHAN,          // Leave child objects orphaned
    GC_PROPAGATION_BACKGROUND,      // Delete children in background
    GC_PROPAGATION_FOREGROUND       // Delete children synchronously
} gc_propagation_policy_t;

// GC deletion strategy
typedef enum {
    GC_DELETE_IMMEDIATE,
    GC_DELETE_WITH_GRACE_PERIOD
} gc_deletion_strategy_t;

// Ownership reference
typedef struct {
    char owner_kind[64];            // "Pod", "Deployment", "Service"
    char owner_name[256];
    char owner_namespace[256];
    char owner_uid[128];
    int controller;                 // Is this the controlling owner?
    int block_owner_deletion;       // Block owner deletion until children deleted
} gc_owner_ref_t;

// Object finalizer
typedef struct {
    char finalizer[256];            // e.g., "kubernetes.io/pvc-protection"
    int blocking;                   // Does this block deletion?
} gc_finalizer_t;

// GC object record
typedef struct {
    char kind[64];                  // "Pod", "Service", "Deployment", etc.
    char name[256];
    char namespace[256];
    char uid[128];
    
    time_t created_at;
    time_t deletion_timestamp;      // When delete was initiated
    int deletion_grace_seconds;
    gc_deletion_strategy_t deletion_strategy;
    
    gc_owner_ref_t owner_refs[GC_MAX_OWNERSHIP_REFS];
    int owner_count;
    
    gc_finalizer_t finalizers[10];
    int finalizer_count;
    
    int orphaned;                   // Has no valid owners
    int pending_deletion;           // Marked for deletion
    gc_propagation_policy_t propagation_policy;
} gc_object_record_t;

// GC controller instance
typedef struct {
    gc_object_record_t objects[GC_MAX_OBJECTS];
    int object_count;
    
    pthread_mutex_t lock;
    int running;
    pthread_t gc_thread;
    
    // Statistics
    int objects_scanned;
    int objects_cleaned;
    time_t last_scan;
} gc_controller_t;

// External API for controller manager
int gc_controller_init(void);
int gc_controller_run(void);
int gc_controller_shutdown(void);

// Object tracking
int gc_register_object(const char* kind, const char* namespace, const char* name, const char* uid);
int gc_unregister_object(const char* kind, const char* namespace, const char* name);
int gc_get_object_record(const char* kind, const char* namespace, const char* name,
                        gc_object_record_t* record);

// Ownership management
int gc_add_owner_reference(const char* kind, const char* namespace, const char* name,
                          const gc_owner_ref_t* owner_ref);
int gc_remove_owner_reference(const char* kind, const char* namespace, const char* name,
                             const char* owner_kind, const char* owner_name);
int gc_get_object_owners(const char* kind, const char* namespace, const char* name,
                        gc_owner_ref_t** owners, int* count);

// Finalizer support
int gc_add_finalizer(const char* kind, const char* namespace, const char* name,
                    const char* finalizer);
int gc_remove_finalizer(const char* kind, const char* namespace, const char* name,
                       const char* finalizer);
int gc_get_finalizers(const char* kind, const char* namespace, const char* name,
                     char*** finalizers, int* count);
int gc_has_finalizers(const char* kind, const char* namespace, const char* name);

// Deletion cascade operations
int gc_mark_for_deletion(const char* kind, const char* namespace, const char* name,
                        int grace_seconds, gc_propagation_policy_t propagation);
int gc_start_deletion_cascade(const char* kind, const char* namespace, const char* name,
                             json_object** cascade_list);
int gc_get_dependent_objects(const char* kind, const char* namespace, const char* name,
                            char*** dependent_kinds, char*** dependent_names, 
                            char*** dependent_namespaces, int* count);

// Orphaned object operations
int gc_get_orphaned_objects(const char* kind, 
                           json_object** result);
int gc_cleanup_orphaned_objects(const char* kind, int* cleanup_count);

// Propagation policy
int gc_set_propagation_policy(const char* kind, const char* namespace, const char* name,
                             gc_propagation_policy_t policy);
int gc_get_propagation_policy(const char* kind, const char* namespace, const char* name,
                             gc_propagation_policy_t* policy);

// Statistics
int gc_get_statistics(int* total_objects, int* orphaned_objects, 
                     int* pending_deletion, time_t* last_scan);

// Background thread (internal)
void* gc_controller_thread(void* arg);

#endif // GC_CONTROLLER_H
