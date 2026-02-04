/*
 * metrics.h
 * 
 * Per-instance and per-node metrics collection
 * Tracks CPU, memory, network I/O, and pod statistics
 */

#ifndef SIRAH_METRICS_H
#define SIRAH_METRICS_H

#include <stdint.h>
#include <time.h>
#include <stdbool.h>
#include <pthread.h>

/**
 * Instance metrics (per VM/pod)
 */
typedef struct {
    char *instance_id;          // Pod name
    char *namespace;            // Pod namespace
    
    // CPU metrics
    double cpu_usage_percent;   // CPU usage percentage (0-100)
    uint64_t cpu_time_ms;       // Total CPU time in milliseconds
    uint64_t cpu_user_time_ms;  // User space CPU time
    uint64_t cpu_system_time_ms; // System CPU time
    
    // Memory metrics
    double memory_usage_percent; // Memory usage percentage (0-100)
    uint64_t memory_usage_bytes; // Memory used in bytes
    uint64_t memory_limit_bytes; // Memory limit in bytes
    uint64_t memory_rss_bytes;   // Resident set size in bytes
    
    // Network I/O metrics
    uint64_t network_bytes_sent;     // Bytes sent over network
    uint64_t network_bytes_received; // Bytes received over network
    uint64_t network_packets_sent;   // Packets sent
    uint64_t network_packets_received; // Packets received
    uint64_t network_errors_sent;    // Send errors
    uint64_t network_errors_received; // Receive errors
    
    // Block I/O metrics
    uint64_t block_bytes_read;   // Bytes read from disk
    uint64_t block_bytes_written; // Bytes written to disk
    uint64_t block_read_ops;     // Read operations count
    uint64_t block_write_ops;    // Write operations count
    
    // Timestamps
    time_t collection_time;      // When metrics were collected
    time_t last_update_time;     // Last update timestamp
    
} instance_metrics_t;

/**
 * Node metrics (per node/host)
 */
typedef struct {
    char *node_name;             // Node identifier
    
    // Node-level CPU
    double total_cpu_usage_percent; // Sum of all container CPU usage
    uint64_t cpu_cores;            // Number of CPU cores available
    uint64_t cpu_allocatable_millis; // Total allocatable CPU in millicores
    uint64_t cpu_used_millis;       // Used CPU in millicores
    
    // Node-level memory
    double total_memory_usage_percent; // Sum of all container memory usage
    uint64_t memory_total_bytes;    // Total available memory
    uint64_t memory_allocatable_bytes; // Allocatable memory after reservations
    uint64_t memory_used_bytes;     // Used memory by all containers
    uint64_t memory_available_bytes; // Available memory
    
    // Node-level pod metrics
    uint32_t pod_count;           // Total pods running on node
    uint32_t pod_count_limit;     // Max pods per node
    uint32_t pod_count_allocatable; // Allocatable pod slots
    
    // Node health
    uint32_t container_count;     // Total containers running
    uint32_t container_restarts;  // Total container restarts
    
    // Network I/O on node
    uint64_t node_network_bytes_sent;
    uint64_t node_network_bytes_received;
    
    // Timestamps
    time_t collection_time;       // When metrics were collected
    time_t last_update_time;      // Last update timestamp
    
} node_metrics_t;

/**
 * Metrics collection interval (seconds)
 */
#define METRICS_COLLECTION_INTERVAL 10

/**
 * Create instance metrics tracker
 */
instance_metrics_t* instance_metrics_create(const char *instance_id,
                                            const char *namespace);

/**
 * Free instance metrics
 */
void instance_metrics_free(instance_metrics_t *metrics);

/**
 * Update instance metrics from QEMU process
 */
bool instance_metrics_collect(instance_metrics_t *metrics,
                             uint64_t memory_limit_bytes);

/**
 * Get CPU usage percentage (0-100)
 */
double instance_metrics_get_cpu_percent(instance_metrics_t *metrics);

/**
 * Get memory usage percentage (0-100)
 */
double instance_metrics_get_memory_percent(instance_metrics_t *metrics);

/**
 * Get network I/O statistics
 */
void instance_metrics_get_network_stats(instance_metrics_t *metrics,
                                       uint64_t *out_bytes_sent,
                                       uint64_t *out_bytes_received,
                                       uint64_t *out_packets_sent,
                                       uint64_t *out_packets_received);

/**
 * Get block I/O statistics
 */
void instance_metrics_get_block_stats(instance_metrics_t *metrics,
                                     uint64_t *out_bytes_read,
                                     uint64_t *out_bytes_written,
                                     uint64_t *out_read_ops,
                                     uint64_t *out_write_ops);

/**
 * Create node metrics tracker
 */
node_metrics_t* node_metrics_create(const char *node_name);

/**
 * Free node metrics
 */
void node_metrics_free(node_metrics_t *metrics);

/**
 * Update node metrics from all instances on node
 */
bool node_metrics_collect(node_metrics_t *metrics,
                         instance_metrics_t **instances,
                         uint32_t instance_count,
                         uint64_t total_memory_bytes,
                         uint32_t max_pods);

/**
 * Get node CPU usage percentage
 */
double node_metrics_get_cpu_percent(node_metrics_t *metrics);

/**
 * Get node memory usage percentage
 */
double node_metrics_get_memory_percent(node_metrics_t *metrics);

/**
 * Get pod capacity on node
 */
void node_metrics_get_pod_capacity(node_metrics_t *metrics,
                                  uint32_t *out_running,
                                  uint32_t *out_limit,
                                  uint32_t *out_available);

/**
 * Check if node has available capacity for new pod
 */
bool node_metrics_has_capacity(node_metrics_t *metrics);

/**
 * Metrics collector thread manager
 */
typedef struct {
    pthread_t collection_thread;
    bool running;
    uint32_t interval_seconds;
    
    instance_metrics_t **instances;
    uint32_t instance_count;
    
    node_metrics_t *node_metrics;
    
    pthread_mutex_t mutex;
} metrics_collector_t;

/**
 * Create metrics collector
 */
metrics_collector_t* metrics_collector_create(const char *node_name,
                                             uint32_t interval_seconds);

/**
 * Free metrics collector
 */
void metrics_collector_free(metrics_collector_t *collector);

/**
 * Add instance to collection
 */
bool metrics_collector_add_instance(metrics_collector_t *collector,
                                   const char *instance_id,
                                   const char *namespace,
                                   uint64_t memory_limit_bytes);

/**
 * Remove instance from collection
 */
bool metrics_collector_remove_instance(metrics_collector_t *collector,
                                      const char *instance_id);

/**
 * Start metrics collection thread
 */
bool metrics_collector_start(metrics_collector_t *collector);

/**
 * Stop metrics collection thread
 */
bool metrics_collector_stop(metrics_collector_t *collector);

/**
 * Get current node metrics
 */
node_metrics_t* metrics_collector_get_node_metrics(metrics_collector_t *collector);

/**
 * Get instance metrics by ID
 */
instance_metrics_t* metrics_collector_get_instance_metrics(metrics_collector_t *collector,
                                                           const char *instance_id);

/**
 * Export metrics in Prometheus format
 */
char* metrics_to_prometheus_format(metrics_collector_t *collector);

#endif /* SIRAH_METRICS_H */
