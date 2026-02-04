// internal/apiserver/pod_lifecycle_integration.h
// Integration layer connecting pod API, scheduling, spawning, and health checks
// Phase 6: Bridges pod creation → scheduling → kubelet spawning → health monitoring

#ifndef SIRAH_POD_LIFECYCLE_INTEGRATION_H
#define SIRAH_POD_LIFECYCLE_INTEGRATION_H

#include <json-c/json.h>
#include "../../pkg/types/pod.h"

/**
 * Initialize pod lifecycle integration
 * Must be called after all subsystems initialized:
 * - etcd_manager_init()
 * - scheduler_integration_init()
 * - kubelet_init()
 * - pod_spawner initialized
 * - probe_executor initialized
 *
 * Returns:
 *   0 - Success
 *   -1 - Failure
 */
int pod_lifecycle_integration_init(const char* api_server_url);

/**
 * Process pod creation - triggered by POST /pods
 * This is called AFTER etcd persistence but BEFORE returning response
 * 
 * Lifecycle:
 *   1. Pod created in etcd with status.phase = "Pending"
 *   2. Scheduler discovers pod in background thread
 *   3. Scheduler assigns to node (spec.nodeName = "node-1")
 *   4. Kubelet discovers spec.nodeName assignment
 *   5. Kubelet spawns QEMU VM for pod
 *   6. Pod status transitions to "Running"
 *   7. Health checks begin
 *
 * @param namespace - Pod namespace (e.g., "default")
 * @param pod_json - Full pod JSON spec (from etcd)
 *
 * Returns:
 *   0 - Success (pod will be scheduled and spawned by background processes)
 *   -1 - Error
 */
int pod_lifecycle_on_creation(const char* namespace, const char* pod_json);

/**
 * Process pod deletion - triggered by DELETE /pods/{name}
 * This is called AFTER etcd deletion but BEFORE returning response
 *
 * Lifecycle:
 *   1. Pod marked for deletion (spec.deletionTimestamp set)
 *   2. Finalizers processed (e.g., volume cleanup)
 *   3. QEMU VM killed by kubelet
 *   4. Pod removed from etcd
 *   5. Status.phase transitioned to "Terminating" then removed
 *
 * @param namespace - Pod namespace
 * @param pod_name - Pod name
 * @param pod_json - Pod spec (optional, from GET before DELETE)
 *
 * Returns:
 *   0 - Success
 *   -1 - Error
 */
int pod_lifecycle_on_deletion(const char* namespace, const char* pod_name, const char* pod_json);

/**
 * Trigger scheduler to assign pending pods
 * Called periodically (normally done in background thread)
 * Can be called manually to force scheduling of specific pod
 *
 * @param namespace - Pod namespace (optional, NULL = all namespaces)
 * @param pod_name - Pod name (optional, NULL = all pending pods)
 *
 * Returns:
 *   Number of pods scheduled
 *   -1 - Error
 */
int pod_lifecycle_trigger_scheduler(const char* namespace, const char* pod_name);

/**
 * Trigger kubelet to spawn VM for pod with nodeName assigned
 * Called when pod spec.nodeName is set by scheduler
 *
 * @param namespace - Pod namespace
 * @param pod_name - Pod name
 * @param pod_json - Full pod spec (includes spec.nodeName)
 *
 * Returns:
 *   0 - Success (VM spawning in progress)
 *   -1 - Error
 */
int pod_lifecycle_trigger_kubelet_spawn(const char* namespace, const char* pod_name, const char* pod_json);

/**
 * Run health checks on pod
 * Called periodically for pods in "Running" status
 *
 * Performs:
 *   1. Startup probe (blocks readiness/liveness)
 *   2. Readiness probe (pod ready for traffic)
 *   3. Liveness probe (pod still alive)
 *
 * Updates pod status:
 *   - status.conditions[] with Ready/Initialized/ContainersReady
 *   - status.containerStatuses[].state
 *   - Triggers pod restart on liveness failure
 *
 * @param namespace - Pod namespace
 * @param pod_name - Pod name
 * @param pod_json - Full pod spec (includes probe config)
 *
 * Returns:
 *   0 - All checks passed
 *   1 - Some checks failed (pod may be marked not ready)
 *   -1 - Error
 */
int pod_lifecycle_check_health(const char* namespace, const char* pod_name, const char* pod_json);

/**
 * Sync pod status with actual state
 * Periodically called to reconcile API server pod state with actual VM/process state
 *
 * Handles:
 *   - Restart count increments (if VM died and restarted)
 *   - Container state transitions
 *   - Pod cleanup on VM crash
 *   - Orphaned pod detection
 *
 * @param namespace - Pod namespace
 * @param pod_name - Pod name
 * @param pod_json - Current pod spec from etcd
 *
 * Returns:
 *   0 - Sync completed
 *   -1 - Error
 */
int pod_lifecycle_sync_status(const char* namespace, const char* pod_name, const char* pod_json);

/**
 * Start pod lifecycle management
 * Spawns background threads for:
 *   - Scheduler loop (discovers and assigns pending pods)
 *   - Kubelet loop (spawns VMs for assigned pods)
 *   - Health check loop (runs probes on running pods)
 *   - Pod sync loop (reconciles state)
 *
 * Returns:
 *   0 - Success
 *   -1 - Error
 */
int pod_lifecycle_integration_start(void);

/**
 * Stop pod lifecycle management
 * Gracefully shuts down background threads
 * Does NOT kill running pods
 */
void pod_lifecycle_integration_stop(void);

/**
 * Get pod lifecycle statistics
 *
 * @param total_pods - Output: total pods managed
 * @param running_pods - Output: pods in Running state
 * @param pending_pods - Output: pods in Pending state
 * @param failing_pods - Output: pods with failed probes
 *
 * Returns:
 *   0 - Success
 *   -1 - Error
 */
int pod_lifecycle_get_stats(int* total_pods, int* running_pods, int* pending_pods, int* failing_pods);

#endif // SIRAH_POD_LIFECYCLE_INTEGRATION_H
