// internal/apiserver/controllers.h
// Phase 5: Controller coordination and reconciliation loops
// Manages all controllers: Deployment, Service, StatefulSet, Job, etc.

#ifndef SIRAH_CONTROLLERS_H
#define SIRAH_CONTROLLERS_H

#include <time.h>

// ============================================================================
// Controller Registry & Coordination
// ============================================================================

typedef enum {
    CONTROLLER_TYPE_DEPLOYMENT,
    CONTROLLER_TYPE_SERVICE,
    CONTROLLER_TYPE_STATEFULSET,
    CONTROLLER_TYPE_JOB,
    CONTROLLER_TYPE_DAEMONSET,
    CONTROLLER_TYPE_HPAUTOSCALER,
    CONTROLLER_TYPE_POD_EVICTION,
    CONTROLLER_TYPE_NODE_LIFECYCLE,
    CONTROLLER_TYPE_GARBAGE_COLLECTION,
    CONTROLLER_TYPE_COUNT
} controller_type_t;

typedef struct {
    controller_type_t type;
    const char* name;
    int enabled;
    int sync_interval_seconds;
    time_t last_sync;
    unsigned long iterations;
    unsigned long errors;
} controller_status_t;

// ============================================================================
// Controller Manager (Coordinator)
// ============================================================================

/**
 * Initialize controller manager
 * Starts background threads for all enabled controllers
 */
int controllers_init(const char* api_server_url);

/**
 * Run controller main loop
 * Continuously syncs all controllers
 */
int controllers_run(void);

/**
 * Shutdown all controllers gracefully
 */
void controllers_shutdown(void);

/**
 * Get status of a controller
 */
controller_status_t* controllers_get_status(controller_type_t type);

/**
 * Enable/disable a controller at runtime
 */
int controllers_set_enabled(controller_type_t type, int enabled);

/**
 * Trigger immediate sync for a controller
 */
int controllers_trigger_sync(controller_type_t type);

// ============================================================================
// StatefulSet Controller
// ============================================================================

/**
 * Reconcile StatefulSet: Create/update/delete pods to match desired replicas
 * Ensures:
 * - Pod ordering (pod-0, pod-1, pod-2, etc.)
 * - Sequential creation (pod-0 must exist before pod-1)
 * - VolumeClaimTemplate provisioning
 * - Status updates with ready replicas
 * 
 * Returns: 0 on success, -1 on error
 */
int statefulset_controller_sync(void);

/**
 * Reconcile single StatefulSet
 * namespace: StatefulSet namespace
 * name: StatefulSet name
 * Returns: 0 on success, -1 on error
 */
int statefulset_reconcile(const char* namespace, const char* name);

// ============================================================================
// Job Controller
// ============================================================================

/**
 * Reconcile Job: Create/delete pods based on parallelism and completions
 * Ensures:
 * - Pod parallelism (N pods running in parallel)
 * - Completion tracking (track successful completions)
 * - TTL enforcement (delete after TTL expires)
 * - Backoff on failures
 * - Status updates
 * 
 * Returns: 0 on success, -1 on error
 */
int job_controller_sync(void);

/**
 * Reconcile single Job
 * namespace: Job namespace
 * name: Job name
 * Returns: 0 on success, -1 on error
 */
int job_reconcile(const char* namespace, const char* name);

// ============================================================================
// Service Controller
// ============================================================================

/**
 * Reconcile Service: Update endpoints based on pod selector
 * Discovers pods matching service selector
 * Updates endpoint list when pods are added/removed
 * Returns: 0 on success, -1 on error
 */
int service_controller_sync(void);

/**
 * Reconcile single Service
 * Returns: 0 on success, -1 on error
 */
int service_reconcile(const char* namespace, const char* name);

// ============================================================================
// Deployment Controller (already implemented, reference below)
// ============================================================================

/**
 * Reconcile Deployment: Create/update ReplicaSets and rollout strategy
 * [Already implemented in phase-5 deployment controller]
 */
int deployment_controller_sync(void);

// ============================================================================
// Pod Eviction Controller (already implemented, reference below)
// ============================================================================

/**
 * Evict pods from failed nodes
 * [Already implemented in phase-5]
 */
int pod_eviction_controller_sync(void);

// ============================================================================
// Node Lifecycle Controller (already implemented, reference below)
// ============================================================================

/**
 * Monitor node health and update status
 * [Already implemented in phase-5]
 */
int node_lifecycle_controller_sync(void);

// ============================================================================
// Garbage Collection (already implemented, reference below)
// ============================================================================

/**
 * Clean up orphaned objects and finalizers
 * [Already implemented in phase-5]
 */
int garbage_collection_sync(void);

#endif // SIRAH_CONTROLLERS_H
