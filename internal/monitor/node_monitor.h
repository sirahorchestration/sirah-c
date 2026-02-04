/*
 * node_monitor.h
 * 
 * Node failure detection and recovery
 * 
 * Monitors node health via heartbeats and implements:
 *   - Heartbeat collection from nodes
 *   - Offline node detection (node not responding)
 *   - Pod eviction from failed nodes
 *   - Node status updates
 * 
 * Kubernetes v1.28 Conformance:
 *   - Implements kubelet heartbeat/NodeStatus updates
 *   - Pod eviction controller logic
 *   - Node lifecycle management
 * 
 * References:
 *   - https://kubernetes.io/docs/concepts/architecture/nodes/
 *   - https://kubernetes.io/docs/tasks/administer-cluster/safely-drain-a-node/
 */

#ifndef SIRAH_NODE_MONITOR_H
#define SIRAH_NODE_MONITOR_H

#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * Node health status
 */
typedef enum {
    NODE_STATUS_READY = 0,
    NODE_STATUS_NOTREADY = 1,
    NODE_STATUS_UNKNOWN = 2
} node_status_t;

/**
 * Node heartbeat information
 */
typedef struct {
    char *node_name;
    time_t last_heartbeat;
    node_status_t status;
    char *status_message;
    
    // Resource info from heartbeat
    uint32_t allocatable_cpu_millicores;
    uint64_t allocatable_memory_bytes;
    uint32_t active_pod_count;
} node_heartbeat_t;

/**
 * Node monitor configuration
 */
typedef struct {
    uint32_t heartbeat_timeout_seconds;      // Time before marking node offline (e.g., 40)
    uint32_t eviction_timeout_seconds;       // Time before evicting pods from offline node (e.g., 300)
    uint32_t check_interval_seconds;         // How often to check for offline nodes (e.g., 5)
    bool     enable_pod_eviction;            // Automatically evict pods from failed nodes
    bool     enable_graceful_eviction;       // Try graceful termination first
} node_monitor_config_t;

/**
 * Node monitor instance
 */
typedef struct {
    node_monitor_config_t config;
    pthread_mutex_t mutex;
    
    // Heartbeats by node name
    node_heartbeat_t *heartbeats;
    uint32_t heartbeat_count;
    uint32_t heartbeat_capacity;
    
    // Monitor thread
    pthread_t monitor_thread;
    bool monitor_running;
    
    // Callback functions for API server integration
    void (*on_node_offline)(const char *node_name, void *userdata);
    void (*on_node_recovered)(const char *node_name, void *userdata);
    void (*evict_pods_from_node)(const char *node_name, void *userdata);
    void *callback_userdata;
} node_monitor_t;

/**
 * Create a new node monitor
 */
node_monitor_t* node_monitor_new(const node_monitor_config_t *config);

/**
 * Free node monitor
 */
void node_monitor_free(node_monitor_t *monitor);

/**
 * Start monitoring nodes
 * 
 * Spawns background thread that checks for offline nodes periodically
 * 
 * @param monitor Node monitor instance
 * @return true on success
 */
bool node_monitor_start(node_monitor_t *monitor);

/**
 * Stop monitoring nodes
 * 
 * Waits for monitoring thread to exit
 */
void node_monitor_stop(node_monitor_t *monitor);

/**
 * Register a heartbeat from a node
 * 
 * Call this when a node sends a heartbeat to the control plane
 * Updates last_heartbeat timestamp and node status
 * 
 * @param monitor Node monitor
 * @param node_name Name of node
 * @param status Current node status (READY, NOTREADY, UNKNOWN)
 * @param status_msg Status message or NULL
 * @param allocatable_cpu CPU millicores available
 * @param allocatable_memory Memory bytes available
 * @param pod_count Current pod count on node
 */
void node_monitor_register_heartbeat(node_monitor_t *monitor,
                                     const char *node_name,
                                     node_status_t status,
                                     const char *status_msg,
                                     uint32_t allocatable_cpu,
                                     uint64_t allocatable_memory,
                                     uint32_t pod_count);

/**
 * Get node status
 * 
 * @param monitor Node monitor
 * @param node_name Name of node
 * @return Current status, or NODE_STATUS_UNKNOWN if not found
 */
node_status_t node_monitor_get_status(node_monitor_t *monitor, const char *node_name);

/**
 * Check if node is offline (no heartbeat within timeout)
 */
bool node_monitor_is_offline(node_monitor_t *monitor, const char *node_name);

/**
 * Get node heartbeat info
 * 
 * Returns heartbeat data for node, or NULL if not found
 * Caller must free returned struct
 * 
 * @param monitor Node monitor
 * @param node_name Name of node
 * @return Heartbeat info, or NULL if not found
 */
node_heartbeat_t* node_monitor_get_heartbeat(node_monitor_t *monitor, const char *node_name);

/**
 * Free heartbeat struct
 */
void node_monitor_free_heartbeat(node_heartbeat_t *hb);

/**
 * Register callback for when node comes online
 */
void node_monitor_set_online_callback(node_monitor_t *monitor,
                                      void (*callback)(const char *node_name, void *userdata),
                                      void *userdata);

/**
 * Register callback for when node goes offline
 */
void node_monitor_set_offline_callback(node_monitor_t *monitor,
                                       void (*callback)(const char *node_name, void *userdata),
                                       void *userdata);

/**
 * Register callback for pod eviction from failed nodes
 */
void node_monitor_set_eviction_callback(node_monitor_t *monitor,
                                        void (*callback)(const char *node_name, void *userdata),
                                        void *userdata);

/**
 * Manually trigger eviction of pods from node
 * 
 * Used when node is detected as failed
 */
void node_monitor_evict_node_pods(node_monitor_t *monitor, const char *node_name);

#endif // SIRAH_NODE_MONITOR_H
