// internal/apiserver/endpoints_etcd_integration.h
// Etcd-integrated pod endpoint handlers (Phase 2A Week 2)
// Provides pod CRUD operations backed by etcd

#ifndef SIRAH_ENDPOINTS_ETCD_INTEGRATION_H
#define SIRAH_ENDPOINTS_ETCD_INTEGRATION_H

// ============================================================================
// CREATE Pod - Store in etcd
// ============================================================================
/**
 * Create a new pod with etcd persistence
 * 
 * @param namespace - Kubernetes namespace (e.g., "default")
 * @param body - JSON pod specification from request
 * @param response_buffer - Response JSON buffer (16KB)
 * @param response_code - HTTP response code (201 Created, 400, 503, 500)
 * 
 * Returns:
 *   0 - Success (201 Created)
 *   -1 - Failure
 * 
 * Status Codes:
 *   201 - Pod created successfully with resourceVersion
 *   400 - Invalid JSON or missing required fields
 *   503 - etcd unavailable or circuit breaker open
 *   500 - Other storage errors
 * 
 * Example:
 *   char buffer[16384];
 *   int code;
 *   endpoint_create_pod_etcd("default", pod_json, buffer, &code);
 *   // buffer contains: {"apiVersion":"v1","kind":"Pod",...,"metadata":{..."resourceVersion":"1234"}}
 */
int endpoint_create_pod_etcd(const char* namespace, const char* body,
                             char* response_buffer, int* response_code);

// ============================================================================
// GET Pod - Retrieve from etcd
// ============================================================================
/**
 * Retrieve a pod from etcd
 * 
 * @param namespace - Kubernetes namespace
 * @param pod_name - Pod name
 * @param response_buffer - Response JSON buffer
 * @param response_code - HTTP response code (200, 404, 503, 500)
 * 
 * Returns:
 *   0 - Success (200 OK)
 *   -1 - Failure
 * 
 * Status Codes:
 *   200 - Pod found, returned with resourceVersion
 *   404 - Pod not found
 *   503 - etcd unavailable
 *   500 - Storage error
 * 
 * Example:
 *   char buffer[16384];
 *   int code;
 *   endpoint_get_pod_etcd("default", "my-pod", buffer, &code);
 *   if (code == 200) {
 *       // buffer contains: {"apiVersion":"v1","kind":"Pod",...}
 *   } else if (code == 404) {
 *       // Pod not found
 *   }
 */
int endpoint_get_pod_etcd(const char* namespace, const char* pod_name,
                          char* response_buffer, int* response_code);

// ============================================================================
// LIST Pods - Retrieve all pods in namespace from etcd
// ============================================================================
/**
 * List all pods in a namespace from etcd
 * 
 * @param namespace - Kubernetes namespace (or empty for default)
 * @param response_buffer - Response JSON buffer (PodList array)
 * @param response_code - HTTP response code (200, 503, 500)
 * 
 * Returns:
 *   0 - Success (200 OK)
 *   -1 - Failure
 * 
 * Status Codes:
 *   200 - Pod list returned
 *   503 - etcd unavailable
 *   500 - Storage error
 * 
 * Response Format:
 *   {
 *     "apiVersion": "v1",
 *     "kind": "PodList",
 *     "items": [
 *       {"metadata": {..., "resourceVersion": "1234"}, ...},
 *       {"metadata": {..., "resourceVersion": "1235"}, ...}
 *     ]
 *   }
 * 
 * Example:
 *   char buffer[16384];
 *   int code;
 *   endpoint_list_pods_etcd("default", buffer, &code);
 *   // buffer contains PodList with all pods in default namespace
 */
int endpoint_list_pods_etcd(const char* namespace, char* response_buffer, int* response_code);

// ============================================================================
// DELETE Pod - Remove from etcd
// ============================================================================
/**
 * Delete a pod from etcd
 * 
 * @param namespace - Kubernetes namespace
 * @param pod_name - Pod name
 * @param response_buffer - Response buffer (empty on success)
 * @param response_code - HTTP response code (204, 404, 503, 500)
 * 
 * Returns:
 *   0 - Success (204 No Content)
 *   -1 - Failure
 * 
 * Status Codes:
 *   204 - Pod deleted successfully (no response body)
 *   404 - Pod not found
 *   503 - etcd unavailable
 *   500 - Storage error
 * 
 * Example:
 *   char buffer[16384];
 *   int code;
 *   endpoint_delete_pod_etcd("default", "my-pod", buffer, &code);
 *   if (code == 204) {
 *       // Pod deleted successfully
 *   }
 */
int endpoint_delete_pod_etcd(const char* namespace, const char* pod_name,
                             char* response_buffer, int* response_code);

// ============================================================================
// PATCH Pod - Update with CAS (Compare-And-Swap)
// ============================================================================
/**
 * Update a pod in etcd using Compare-And-Swap (CAS/Optimistic Locking)
 * 
 * This implements Kubernetes' optimistic concurrency control:
 * - Retrieves current pod and its revision from etcd
 * - Merges patch into current pod
 * - Uses CAS to update only if revision hasn't changed
 * - Returns 409 Conflict if another client modified the pod
 * 
 * @param namespace - Kubernetes namespace
 * @param pod_name - Pod name
 * @param body - JSON patch (merged into current pod)
 * @param content_type - MIME type (for patch type detection)
 * @param response_buffer - Response JSON buffer
 * @param response_code - HTTP response code (200, 409, 404, 503, 500)
 * 
 * Returns:
 *   0 - Success (200 OK or 409 expected conflict)
 *   -1 - Failure
 * 
 * Status Codes:
 *   200 - Patch applied successfully with new resourceVersion
 *   409 - Conflict: pod was modified by another client
 *         Client should GET again and retry with updated resourceVersion
 *   404 - Pod not found
 *   503 - etcd unavailable
 *   500 - Storage error
 * 
 * CAS Semantics:
 *   1. Client GETs pod, receives resourceVersion (e.g., "1234")
 *   2. Client modifies pod spec
 *   3. Client PATCH with resourceVersion "1234"
 *   4. If pod unchanged → 200 OK with new resourceVersion (e.g., "1235")
 *   5. If pod changed by another client → 409 Conflict
 *      Client must retry: GET again, merge changes, PATCH with new version
 * 
 * Example:
 *   // Step 1: Get pod
 *   endpoint_get_pod_etcd("default", "my-pod", get_buffer, &code);
 *   // get_buffer contains resourceVersion: "1234"
 *   
 *   // Step 2: Create patch (with original resourceVersion for CAS)
 *   const char* patch = "{\"spec\":{\"replicas\":3},\"metadata\":{\"resourceVersion\":\"1234\"}}";
 *   
 *   // Step 3: Apply patch
 *   endpoint_patch_pod_etcd("default", "my-pod", patch, "application/json-patch+json", 
 *                           patch_buffer, &code);
 *   
 *   // Step 4: Handle result
 *   if (code == 200) {
 *       // Success! resourceVersion is now "1235"
 *   } else if (code == 409) {
 *       // Conflict - another client modified the pod
 *       // Retry: GET again and apply patch with new resourceVersion
 *   }
 */
int endpoint_patch_pod_etcd(const char* namespace, const char* pod_name,
                            const char* body, const char* content_type,
                            char* response_buffer, int* response_code);

// ============================================================================
// Integration with handler.c
// ============================================================================
// These functions are designed to replace the in-process pod_store calls
// in handler.c. They should be called after etcd_manager_init() completes.
//
// In handler.c, replace:
//   endpoint_create_pod() → endpoint_create_pod_etcd()
//   endpoint_get_pod() → endpoint_get_pod_etcd()
//   endpoint_list_pods() → endpoint_list_pods_etcd()
//   endpoint_delete_pod() → endpoint_delete_pod_etcd()
//   endpoint_patch_pod() → endpoint_patch_pod_etcd() (with resourceVersion check)
//
// Requires initialization in main.c or app startup:
//   etcd_manager_init("http://127.0.0.1:2379", NULL);
//   etcd_manager_wait_for_ready(60);  // 60 second timeout
//
// And shutdown:
//   etcd_manager_shutdown();
// ============================================================================

// ============================================================================
// STATEFULSET ETCD-BACKED ENDPOINTS (Phase 5)
// ============================================================================

int endpoint_create_statefulset_etcd(const char* namespace, const char* body,
                                     char* response_buffer, int* response_code);
int endpoint_get_statefulset_etcd(const char* namespace, const char* name,
                                  char* response_buffer, int* response_code);
int endpoint_list_statefulsets_etcd(const char* namespace, char* response_buffer,
                                    int* response_code);
int endpoint_delete_statefulset_etcd(const char* namespace, const char* name,
                                     char* response_buffer, int* response_code);

// ============================================================================
// JOB ETCD-BACKED ENDPOINTS (Phase 5)
// ============================================================================

int endpoint_create_job_etcd(const char* namespace, const char* body,
                             char* response_buffer, int* response_code);
int endpoint_get_job_etcd(const char* namespace, const char* name,
                          char* response_buffer, int* response_code);
int endpoint_list_jobs_etcd(const char* namespace, char* response_buffer,
                            int* response_code);
int endpoint_delete_job_etcd(const char* namespace, const char* name,
                             char* response_buffer, int* response_code);

// ============================================================================
// CONFIGMAP ETCD-BACKED ENDPOINTS (Phase 6A)
// ============================================================================

int endpoint_create_configmap_etcd(const char* namespace, const char* body,
                                   char* response_buffer, int* response_code);
int endpoint_get_configmap_etcd(const char* namespace, const char* name,
                                char* response_buffer, int* response_code);
int endpoint_list_configmaps_etcd(const char* namespace, char* response_buffer,
                                  int* response_code);
int endpoint_patch_configmap_etcd(const char* namespace, const char* name, const char* body,
                                  const char* content_type, char* response_buffer, int* response_code);
int endpoint_delete_configmap_etcd(const char* namespace, const char* name,
                                   char* response_buffer, int* response_code);

// ============================================================================
// SECRET ETCD-BACKED ENDPOINTS (Phase 6A)
// ============================================================================

int endpoint_create_secret_etcd(const char* namespace, const char* body,
                                char* response_buffer, int* response_code);
int endpoint_get_secret_etcd(const char* namespace, const char* name,
                             char* response_buffer, int* response_code);
int endpoint_list_secrets_etcd(const char* namespace, char* response_buffer,
                               int* response_code);
int endpoint_patch_secret_etcd(const char* namespace, const char* name, const char* body,
                               const char* content_type, char* response_buffer, int* response_code);
int endpoint_delete_secret_etcd(const char* namespace, const char* name,
                                char* response_buffer, int* response_code);

// ============================================================================
// PERSISTENTVOLUME ETCD-BACKED ENDPOINTS (Phase 6A)
// ============================================================================

int endpoint_create_pv_etcd(const char* body, char* response_buffer, int* response_code);
int endpoint_get_pv_etcd(const char* name, char* response_buffer, int* response_code);
int endpoint_patch_pv_etcd(const char* name, const char* body, const char* content_type,
                           char* response_buffer, int* response_code);
int endpoint_list_pv_etcd(char* response_buffer, int* response_code);
int endpoint_delete_pv_etcd(const char* name, char* response_buffer, int* response_code);

// ============================================================================
// PERSISTENTVOLUMECLAIM ETCD-BACKED ENDPOINTS (Phase 6A)
// ============================================================================

int endpoint_create_pvc_etcd(const char* namespace, const char* body,
                             char* response_buffer, int* response_code);
int endpoint_get_pvc_etcd(const char* namespace, const char* name,
                          char* response_buffer, int* response_code);
int endpoint_patch_pvc_etcd(const char* namespace, const char* name,
                             const char* body, const char* content_type,
                             char* response_buffer, int* response_code);
int endpoint_list_pvc_etcd(const char* namespace, char* response_buffer,
                           int* response_code);
int endpoint_delete_pvc_etcd(const char* namespace, const char* name,
                             char* response_buffer, int* response_code);

#endif // SIRAH_ENDPOINTS_ETCD_INTEGRATION_H
