// internal/controller/daemonset_controller.h
// DaemonSet Controller - Run pod on every node
// Phase 5 Implementation

#ifndef SIRAH_DAEMONSET_CONTROLLER_H
#define SIRAH_DAEMONSET_CONTROLLER_H

#include <time.h>
#include <json-c/json.h>

// Forward declarations
typedef struct k8s_pod k8s_pod_t;

// ============================================================================
// DaemonSet Structures
// ============================================================================

/**
 * DaemonSet update strategy
 */
typedef enum {
    DAEMONSET_UPDATE_ON_DELETE = 0,      // Delete old pod, create new pod
    DAEMONSET_UPDATE_ROLLING = 1         // Replace one at a time
} daemonset_update_strategy_t;

/**
 * DaemonSet specification
 */
typedef struct {
    char name[256];                    // DaemonSet name
    char namespace[256];               // Namespace
    
    // Pod selector
    char selector_label_key[256];      // Selector key (e.g., "app")
    char selector_label_value[256];    // Selector value (e.g., "myapp")
    
    // Container spec
    char container_name[256];          // First container name
    char container_image[512];         // Container image
    int container_port;                // Container port (optional)
    
    // Update strategy
    daemonset_update_strategy_t update_strategy;  // How to update pods
    int max_unavailable;               // Max pods unavailable during rolling update
    int min_ready_seconds;             // Minimum seconds for pod to be considered ready
    
    // Node affinity (for targeted DaemonSet)
    int has_node_selector;             // Whether to use node selector
    char node_selector_key[256];       // Node selector key
    char node_selector_value[256];     // Node selector value
} daemonset_spec_t;

/**
 * Node pod mapping
 */
typedef struct {
    char node_name[256];               // Node name
    char pod_name[256];                // Pod running on this node (if any)
    char pod_uid[256];                 // Pod UID
    int ready;                         // 1 if pod ready, 0 otherwise
    int available;                     // 1 if pod available, 0 otherwise
    time_t pod_start_time;             // When pod started
} daemonset_node_entry_t;

/**
 * DaemonSet status tracking
 */
typedef struct {
    int desired_number_scheduled;      // Total nodes where pod should run
    int current_number_scheduled;      // Nodes where pod is present
    int number_ready;                  // Pods that passed readiness probe
    int number_available;              // Pods available for traffic
    int number_updated;                // Pods with latest template
    int number_misscheduled;           // Pods on nodes that shouldn't have them (tainted)
    
    time_t creation_time;              // When DaemonSet created
    time_t update_start;               // When rolling update began
    
    // Per-node tracking
    daemonset_node_entry_t nodes[256]; // Tracked nodes and their pods
    int num_nodes;                     // Current node count
} daemonset_status_t;

/**
 * DaemonSet entry
 */
typedef struct {
    daemonset_spec_t spec;
    daemonset_status_t status;
} k8s_daemonset_t;

// ============================================================================
// DaemonSet Controller Operations
// ============================================================================

/**
 * Initialize DaemonSet controller
 */
int daemonset_controller_init(const char* api_server_url);

/**
 * Main controller loop - ensures pod on every node
 */
int daemonset_controller_run(void);

/**
 * Shutdown controller
 */
void daemonset_controller_shutdown(void);

/**
 * Create DaemonSet
 * 
 * Creates a DaemonSet and immediately schedules pods on all available nodes:
 * 1. Query all nodes in cluster
 * 2. For each node (filtered by nodeSelector if present):
 *    - Check if node has NoSchedule taint (skip if so)
 *    - Create pod on node with nodeAffinity constraint
 * 3. Track pod per node in status
 * 
 * @param namespace - Kubernetes namespace
 * @param body - JSON DaemonSet specification
 * @param response_buffer - Response JSON buffer
 * @param response_code - HTTP response code (201, 400, 503, 500)
 * 
 * Returns: 0 on success, -1 on failure
 * 
 * Status Codes:
 *   201 - DaemonSet created successfully
 *   400 - Invalid specification
 *   503 - etcd unavailable
 *   500 - Storage error
 */
int endpoint_create_daemonset(const char* namespace, const char* body,
                              char* response_buffer, int* response_code);

/**
 * Get DaemonSet with node status
 * 
 * Returns DaemonSet spec plus status showing:
 * - desiredNumberScheduled: Total nodes matching criteria
 * - currentNumberScheduled: Nodes where pod is present
 * - numberReady: Nodes where pod is ready
 * - numberAvailable: Nodes where pod is available
 * - numberUpdated: Nodes with latest template
 * - numberMisscheduled: Nodes with tainted pods (should be 0)
 * 
 * @param namespace - Kubernetes namespace
 * @param name - DaemonSet name
 * @param response_buffer - Response JSON buffer
 * @param response_code - HTTP response code (200, 404, 503, 500)
 * 
 * Returns: 0 on success, -1 on failure
 */
int endpoint_get_daemonset(const char* namespace, const char* name,
                           char* response_buffer, int* response_code);

/**
 * List DaemonSets in namespace
 * 
 * @param namespace - Kubernetes namespace
 * @param response_buffer - Response JSON buffer (DaemonSetList)
 * @param response_code - HTTP response code (200, 503, 500)
 * 
 * Returns: 0 on success, -1 on failure
 */
int endpoint_list_daemonsets(const char* namespace, char* response_buffer,
                             int* response_code);

/**
 * Update DaemonSet specification
 * 
 * Updates pod template. Triggers:
 * - OnDelete: Admin must delete old pods, new ones created automatically
 * - RollingUpdate: Replace one pod at a time on each node
 * 
 * @param namespace - Kubernetes namespace
 * @param name - DaemonSet name
 * @param body - JSON patch (spec.template)
 * @param response_buffer - Response JSON buffer
 * @param response_code - HTTP response code
 * 
 * Returns: 0 on success, -1 on failure
 */
int endpoint_patch_daemonset(const char* namespace, const char* name,
                             const char* body, const char* content_type,
                             char* response_buffer, int* response_code);

/**
 * Delete DaemonSet
 * 
 * Terminates pods on all nodes. Set cascade=true to delete pods automatically.
 * 
 * @param namespace - Kubernetes namespace
 * @param name - DaemonSet name
 * @param response_buffer - Response buffer
 * @param response_code - HTTP response code (204, 404, 503, 500)
 * 
 * Returns: 0 on success, -1 on failure
 */
int endpoint_delete_daemonset(const char* namespace, const char* name,
                              char* response_buffer, int* response_code);

// ============================================================================
// DaemonSet Internal Functions
// ============================================================================

/**
 * Get list of all nodes in cluster
 * Returns array of node names
 */
char** daemonset_get_all_nodes(int* node_count);

/**
 * Check if node matches DaemonSet selector
 */
int daemonset_node_matches_selector(const char* node_name,
                                   const char* selector_key,
                                   const char* selector_value);

/**
 * Check if node is tainted (has NoSchedule taint)
 * Returns: 1 if tainted, 0 if not
 */
int daemonset_node_is_tainted(const char* node_name);

/**
 * Create pod on specific node
 */
int daemonset_create_pod_on_node(const char* namespace, const char* daemonset_name,
                                const char* node_name, k8s_pod_t* pod);

/**
 * Perform rolling update of DaemonSet
 * Replaces pods one at a time with new template
 */
int daemonset_perform_rolling_update(const char* namespace,
                                    const char* daemonset_name);

/**
 * Perform on-delete update
 * Marks old pods for deletion (admin deletes them)
 */
int daemonset_perform_ondelete_update(const char* namespace,
                                     const char* daemonset_name);

#endif // SIRAH_DAEMONSET_CONTROLLER_H
