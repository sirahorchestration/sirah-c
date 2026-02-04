/*
 * node_metrics.h
 *
 * Node-level metrics collection
 */

#ifndef SIRAH_NODE_METRICS_H
#define SIRAH_NODE_METRICS_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

/**
 * Node metrics snapshot
 */
typedef struct {
    char *node_name;
    
    // CPU metrics
    uint32_t total_cpu_percent;       // Total CPU usage across all cores
    uint32_t available_cpu_percent;   // Available CPU
    uint64_t cpu_cores;               // Number of CPU cores
    
    // Memory metrics
    uint64_t total_memory_bytes;       // Total system memory
    uint64_t used_memory_bytes;        // Used memory
    uint64_t available_memory_bytes;   // Available memory
    uint32_t memory_usage_percent;     // % of total
    
    // Network metrics
    uint64_t network_rx_bytes;         // Total received
    uint64_t network_tx_bytes;         // Total transmitted
    
    // Pod metrics
    uint32_t pod_count;                // Total pods on node
    uint32_t running_pods;             // Running pods
    uint32_t failed_pods;              // Failed pods
    
    // Timestamps
    time_t collection_time;            // When collected
    
} node_metrics_t;

/**
 * Node metrics collector
 */
typedef struct {
    char *node_name;
    
    node_metrics_t current_metrics;
    
    // Sampling configuration
    uint32_t sample_interval_seconds;
    time_t last_collection;
    
    // Previous values (for delta calculation)
    uint64_t prev_cpu_ticks;
    uint64_t prev_total_cpu_ticks;
    
    pthread_mutex_t mutex;
    
} node_metrics_collector_t;

/**
 * Create node metrics collector
 */
node_metrics_collector_t* node_metrics_create(const char *node_name);

/**
 * Free node metrics collector
 */
void node_metrics_free(node_metrics_collector_t *collector);

/**
 * Collect current node metrics from /proc
 */
bool node_metrics_collect(node_metrics_collector_t *collector);

/**
 * Get current node metrics
 */
bool node_metrics_get_current(node_metrics_collector_t *collector,
                               node_metrics_t *out_metrics);

/**
 * Update pod count on node
 */
bool node_metrics_update_pod_count(node_metrics_collector_t *collector,
                                    uint32_t running_count,
                                    uint32_t failed_count);

#endif // SIRAH_NODE_METRICS_H
