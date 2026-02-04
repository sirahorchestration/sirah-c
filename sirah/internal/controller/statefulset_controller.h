// internal/controller/statefulset_controller.h
// StatefulSet Controller - Ordered pod creation with stable identities
// Phase 5 Implementation

#ifndef SIRAH_STATEFULSET_CONTROLLER_H
#define SIRAH_STATEFULSET_CONTROLLER_H

#include <time.h>
#include <json-c/json.h>

// Forward declarations
typedef struct k8s_pod k8s_pod_t;
typedef struct k8s_statefulset k8s_statefulset_t;

// ============================================================================
// StatefulSet Structures
// ============================================================================

/**
 * Persistent Volume Claim binding for StatefulSet pod
 */
typedef struct {
    char name[256];                    // PVC name
    char storage_class[256];           // Storage class
    char capacity[64];                 // Capacity (e.g., "10Gi")
    char access_mode[64];              // ReadWriteOnce, ReadOnlyMany, ReadWriteMany
} statefulset_pvc_t;

/**
 * StatefulSet specification
 */
typedef struct {
    char name[256];                    // StatefulSet name
    char namespace[256];               // Namespace
    int replicas;                      // Desired replica count (0-1000)
    
    // Pod template spec
    char selector_label_key[256];      // Selector key (e.g., "app")
    char selector_label_value[256];    // Selector value (e.g., "myapp")
    
    // Container spec
    char container_name[256];          // First container name
    char container_image[512];         // Container image
    int container_port;                // Container port
    
    // StatefulSet-specific
    char service_name[256];            // Headless service name for stable DNS
    char update_strategy[64];          // OnDelete, RollingUpdate
    int pod_management_policy;         // 0=OrderedReady (default), 1=Parallel
    
    // Persistent volumes
    statefulset_pvc_t pvcs[16];        // Volume claims for each pod
    int num_pvcs;                      // Number of PVCs
    
    // Pod identity
    int ordinal_start;                 // Starting ordinal (usually 0)
} statefulset_spec_t;

/**
 * Pod revision information (tracks generation for updates)
 */
typedef struct {
    int ordinal;                       // Pod ordinal (0, 1, 2, ...)
    char pod_name[256];                // Generated pod name
    char pod_id[256];                  // Pod UID
    char service_dns[512];             // Service DNS hostname
    int revision;                      // StatefulSet revision
    time_t created_at;                 // Creation timestamp
    int ready;                         // 1 if pod ready, 0 otherwise
} statefulset_pod_entry_t;

/**
 * StatefulSet status tracking
 */
typedef struct {
    int replicas;                      // Current pod count
    int ready_replicas;                // Pods that passed readiness probe
    int updated_replicas;              // Pods with updated template
    int available_replicas;            // Pods running for > 30s
    
    time_t creation_time;              // When StatefulSet created
    time_t update_start;               // When update began (RollingUpdate)
    int update_revision;               // Current revision being rolled out
    
    statefulset_pod_entry_t pods[256]; // Tracked pods
    int num_pods;                      // Current pod count
} statefulset_status_t;

/**
 * StatefulSet entry
 */
typedef struct {
    statefulset_spec_t spec;
    statefulset_status_t status;
} k8s_statefulset_t;

// ============================================================================
// StatefulSet Controller Operations
// ============================================================================

/**
 * Initialize StatefulSet controller
 */
int statefulset_controller_init(const char* api_server_url);

/**
 * Main controller loop - reconciles StatefulSets with actual pods
 */
int statefulset_controller_run(void);

/**
 * Shutdown controller
 */
void statefulset_controller_shutdown(void);

/**
 * Create StatefulSet with ordered pod creation
 * 
 * Creates a StatefulSet and immediately begins ordered pod creation:
 * 1. Create PVCs for each pod ordinal
 * 2. Create pods sequentially: pod-0, pod-1, pod-2, ...
 * 3. Wait for each pod to be ready before creating next
 * 4. Assign stable DNS names: {pod-name}.{service-name}.{namespace}.svc.cluster.local
 * 
 * @param namespace - Kubernetes namespace
 * @param body - JSON StatefulSet specification
 * @param response_buffer - Response JSON buffer
 * @param response_code - HTTP response code (201, 400, 503, 500)
 * 
 * Returns: 0 on success, -1 on failure
 * 
 * Status Codes:
 *   201 - StatefulSet created successfully
 *   400 - Invalid specification
 *   503 - etcd unavailable
 *   500 - Storage error
 */
int endpoint_create_statefulset(const char* namespace, const char* body,
                                char* response_buffer, int* response_code);

/**
 * Get StatefulSet with pod replica status
 * 
 * @param namespace - Kubernetes namespace
 * @param name - StatefulSet name
 * @param response_buffer - Response JSON buffer
 * @param response_code - HTTP response code (200, 404, 503, 500)
 * 
 * Returns: 0 on success, -1 on failure
 */
int endpoint_get_statefulset(const char* namespace, const char* name,
                             char* response_buffer, int* response_code);

/**
 * List StatefulSets in namespace
 * 
 * @param namespace - Kubernetes namespace
 * @param response_buffer - Response JSON buffer (StatefulSetList)
 * @param response_code - HTTP response code (200, 503, 500)
 * 
 * Returns: 0 on success, -1 on failure
 */
int endpoint_list_statefulsets(const char* namespace, char* response_buffer,
                               int* response_code);

/**
 * Update StatefulSet specification
 * 
 * Updates replicas or pod template. Triggers:
 * - Replica scaling (add/remove pods)
 * - Rolling update (replace pods sequentially in reverse ordinal order)
 * 
 * @param namespace - Kubernetes namespace
 * @param name - StatefulSet name
 * @param body - JSON patch (spec.replicas or spec.template)
 * @param response_buffer - Response JSON buffer
 * @param response_code - HTTP response code
 * 
 * Returns: 0 on success, -1 on failure
 */
int endpoint_patch_statefulset(const char* namespace, const char* name,
                               const char* body, const char* content_type,
                               char* response_buffer, int* response_code);

/**
 * Delete StatefulSet
 * 
 * Terminates pods in reverse ordinal order (pod-N, pod-N-1, ..., pod-0).
 * Preserves PVCs by default (orphan deletion policy).
 * 
 * @param namespace - Kubernetes namespace
 * @param name - StatefulSet name
 * @param response_buffer - Response buffer
 * @param response_code - HTTP response code (204, 404, 503, 500)
 * 
 * Returns: 0 on success, -1 on failure
 */
int endpoint_delete_statefulset(const char* namespace, const char* name,
                                char* response_buffer, int* response_code);

// ============================================================================
// StatefulSet Internal Functions
// ============================================================================

/**
 * Generate stable pod name from StatefulSet and ordinal
 * Format: {statefulset-name}-{ordinal}
 */
char* statefulset_generate_pod_name(const char* ss_name, int ordinal);

/**
 * Generate service DNS hostname for pod
 * Format: {pod-name}.{service-name}.{namespace}.svc.cluster.local
 */
char* statefulset_generate_service_dns(const char* pod_name,
                                       const char* service_name,
                                       const char* namespace);

/**
 * Create PVC for pod ordinal
 */
int statefulset_create_pvc(const char* namespace, const char* statefulset_name,
                          int ordinal, statefulset_pvc_t* pvc_spec);

/**
 * Create pod with stable identity
 */
int statefulset_create_pod(const char* namespace, const char* ss_name,
                          int ordinal, k8s_pod_t* pod);

/**
 * Wait for pod to be ready (readiness probe passes)
 */
int statefulset_wait_pod_ready(const char* namespace, const char* pod_name,
                              int timeout_seconds);

/**
 * Perform rolling update on StatefulSet
 * Replaces pods in reverse ordinal order with new template
 */
int statefulset_perform_rolling_update(const char* namespace,
                                      const char* ss_name);

/**
 * Scale StatefulSet to new replica count
 * Adds or removes pods as needed
 */
int statefulset_scale(const char* namespace, const char* ss_name,
                     int new_replicas);

#endif // SIRAH_STATEFULSET_CONTROLLER_H
