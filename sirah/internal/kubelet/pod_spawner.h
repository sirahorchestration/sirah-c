// internal/kubelet/pod_spawner.h
// Pod spawning integration for kubelet
// Handles creation and lifecycle of unikernel VMs for pods

#ifndef SIRAH_POD_SPAWNER_H
#define SIRAH_POD_SPAWNER_H

#include <signal.h>

/**
 * Spawn a new pod as a QEMU VM
 * 
 * Extracts container image and resource requirements from pod JSON,
 * creates a QEMU VM configuration, and starts the VM.
 * 
 * @param namespace - Pod namespace
 * @param pod_name - Pod name
 * @param pod_json - Full pod JSON from API server
 * 
 * @return 0 on success, -1 on failure
 */
int pod_spawner_spawn_pod(const char* namespace, const char* pod_name,
                          const char* pod_json);

/**
 * Get the QEMU VM ID for a pod
 * 
 * @param namespace - Pod namespace
 * @param pod_name - Pod name
 * @param vm_id_out - Buffer to write VM ID (must be at least 256 bytes)
 * @param vm_id_len - Length of vm_id_out buffer
 * 
 * @return 0 if found, -1 if not found
 */
int pod_spawner_get_pod_vm_id(const char* namespace, const char* pod_name,
                              char* vm_id_out, int vm_id_len);

/**
 * Check if a pod's VM is currently running
 * 
 * @param namespace - Pod namespace
 * @param pod_name - Pod name
 * 
 * @return 1 if running, 0 if not running
 */
int pod_spawner_is_pod_running(const char* namespace, const char* pod_name);

/**
 * Kill a pod's VM and clean up
 * 
 * @param namespace - Pod namespace
 * @param pod_name - Pod name
 * @param timeout_sec - Seconds to wait for graceful shutdown before SIGKILL
 * 
 * @return 0 on success, -1 on failure
 */
int pod_spawner_kill_pod(const char* namespace, const char* pod_name, int timeout_sec);

/**
 * Clean up all spawned pods (usually called at shutdown)
 * 
 * @return 0 on success
 */
int pod_spawner_cleanup(void);

/**
 * Get statistics about spawned pods
 * 
 * @param total_spawned - Pointer to store total spawned pods (all time)
 * @param total_running - Pointer to store currently running pods
 * 
 * @return 0 on success
 */
int pod_spawner_get_stats(int* total_spawned, int* total_running);

#endif // SIRAH_POD_SPAWNER_H
