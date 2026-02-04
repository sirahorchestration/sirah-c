/*
 * control_plane_client.h
 * 
 * HTTP client for agent to communicate with control plane
 * 
 * Features:
 *   - Node registration (create node object in API server)
 *   - Heartbeat/keepalive (periodic status updates)
 *   - Pod assignment polling (watch for new pod assignments)
 *   - Pod status reporting (update pod status when container changes)
 *   - Connection retry with exponential backoff
 *   - Circuit breaker integration for resilience
 * 
 * Kubernetes v1.28 Conformance:
 *   - Implements kubelet → API server communication pattern
 *   - Node registration via Node API
 *   - Pod lifecycle status updates
 *   - Heartbeat mechanism for node health
 */

#ifndef SIRAH_CONTROL_PLANE_CLIENT_H
#define SIRAH_CONTROL_PLANE_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

/**
 * Node registration request
 */
typedef struct {
    char *node_name;                  // e.g., "worker-1"
    char *node_id;                    // UUID or host identifier
    uint32_t allocatable_cpu_cores;   // CPU cores in millicores (e.g., 2000)
    uint64_t allocatable_memory_bytes;// Memory in bytes (e.g., 4GB)
    char *pod_cidr;                   // Pod CIDR assigned to node (e.g., "10.244.1.0/24")
    char *labels_json;                // JSON object with node labels
    char *taints_json;                // JSON array with node taints
} node_registration_request_t;

/**
 * Heartbeat update request
 */
typedef struct {
    char *node_name;
    char *status;                     // "Ready", "NotReady", "Unknown"
    char *status_message;
    uint32_t pod_count;               // Current pods on node
    uint32_t allocatable_cpu_cores;
    uint64_t allocatable_memory_bytes;
    time_t timestamp;
} heartbeat_request_t;

/**
 * Pod assignment response from control plane
 */
typedef struct {
    char *pod_name;
    char *pod_namespace;
    char *pod_uid;
    char *image;                      // Container image
    char *image_pull_policy;          // "Always", "IfNotPresent", "Never"
    uint32_t memory_request_bytes;
    uint32_t cpu_request_millicores;
    uint32_t memory_limit_bytes;
    uint32_t cpu_limit_millicores;
    char *pod_spec_json;              // Full pod spec from API server
} pod_assignment_t;

/**
 * Control plane client configuration
 */
typedef struct {
    char *api_server_url;             // e.g., "http://localhost:6443"
    char *node_name;                  // This agent's node name
    uint32_t heartbeat_interval_seconds;  // How often to send heartbeat (e.g., 10)
    uint32_t pod_sync_interval_seconds;   // How often to check for new pods (e.g., 5)
    uint32_t initial_retry_delay_ms;      // Initial backoff (e.g., 100ms)
    uint32_t max_retry_delay_ms;          // Max backoff (e.g., 30000ms)
    uint32_t registration_timeout_seconds;// Time to complete registration (e.g., 60)
    bool use_circuit_breaker;             // Enable circuit breaker for resilience
} control_plane_client_config_t;

/**
 * Control plane client instance
 */
typedef struct {
    control_plane_client_config_t config;
    pthread_mutex_t mutex;
    
    // Registration state
    bool registered;
    time_t registration_time;
    
    // Heartbeat tracking
    time_t last_heartbeat;
    uint32_t failed_heartbeats;
    
    // Connection stats
    uint32_t total_requests;
    uint32_t successful_requests;
    uint32_t failed_requests;
    
    // Pod assignment tracking
    pod_assignment_t *assignments;
    uint32_t assignment_count;
    uint32_t assignment_capacity;
} control_plane_client_t;

/**
 * Create control plane client
 */
control_plane_client_t* control_plane_client_new(const control_plane_client_config_t *config);

/**
 * Free control plane client
 */
void control_plane_client_free(control_plane_client_t *client);

/**
 * Register this node with control plane
 * 
 * Creates Node object in API server with resource capacity and metadata
 * Blocks until registration succeeds or timeout
 * 
 * @param client Control plane client
 * @param req Registration request
 * @return true on success
 */
bool control_plane_client_register_node(control_plane_client_t *client,
                                       const node_registration_request_t *req);

/**
 * Send heartbeat to control plane
 * 
 * Updates node status and resource info
 * Should be called periodically (e.g., every 10 seconds)
 * 
 * @param client Control plane client
 * @param req Heartbeat request
 * @return true on success
 */
bool control_plane_client_send_heartbeat(control_plane_client_t *client,
                                         const heartbeat_request_t *req);

/**
 * Poll for new pod assignments
 * 
 * Queries API server for pods scheduled to this node
 * Returns newly assigned pods not yet seen
 * 
 * @param client Control plane client
 * @param out_assignments Output array of new assignments
 * @param out_count Number of new assignments
 * @return true on success (even if 0 assignments)
 */
bool control_plane_client_poll_assignments(control_plane_client_t *client,
                                           pod_assignment_t **out_assignments,
                                           uint32_t *out_count);

/**
 * Report pod status to control plane
 * 
 * Updates pod.status with container state changes
 * Called when container transitions (waiting → running → terminated)
 * 
 * @param client Control plane client
 * @param pod_name Pod name
 * @param pod_namespace Pod namespace
 * @param phase Pod phase (Pending, Running, Succeeded, Failed)
 * @param container_state JSON with container state info
 * @return true on success
 */
bool control_plane_client_update_pod_status(control_plane_client_t *client,
                                            const char *pod_name,
                                            const char *pod_namespace,
                                            const char *phase,
                                            const char *container_state);

/**
 * Watch for pod deletion requests
 * 
 * Polls for pods scheduled to this node that have been deleted
 * Agent should terminate the pod when deletion is detected
 * 
 * @param client Control plane client
 * @param out_pod_names Array of pod names to delete
 * @param out_count Number of pods to delete
 * @return true on success
 */
bool control_plane_client_check_deletions(control_plane_client_t *client,
                                          char **out_pod_names,
                                          uint32_t *out_count);

/**
 * Check if client is registered with control plane
 */
bool control_plane_client_is_registered(control_plane_client_t *client);

/**
 * Get connection statistics
 */
void control_plane_client_get_stats(control_plane_client_t *client,
                                    uint32_t *out_total,
                                    uint32_t *out_success,
                                    uint32_t *out_failures);

/**
 * Free pod assignment array
 */
void control_plane_client_free_assignments(pod_assignment_t *assignments, uint32_t count);

/**
 * Free pod name array
 */
void control_plane_client_free_pod_names(char **pod_names, uint32_t count);

#endif // SIRAH_CONTROL_PLANE_CLIENT_H
