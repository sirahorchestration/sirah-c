/*
 * metrics_collector.h
 *
 * Per-instance metrics collection (CPU, memory, network I/O)
 */

#ifndef SIRAH_METRICS_COLLECTOR_H
#define SIRAH_METRICS_COLLECTOR_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

/**
 * Instance metrics snapshot
 */
typedef struct {
    char *instance_id;
    char *pod_name;
    char *namespace;
    
    // CPU metrics
    uint32_t cpu_usage_percent;      // 0-100
    uint64_t cpu_time_ms;             // Total CPU time in milliseconds
    
    // Memory metrics
    uint64_t memory_usage_bytes;       // Current memory usage
    uint64_t memory_limit_bytes;       // Memory limit
    uint32_t memory_usage_percent;     // % of limit
    
    // Network metrics
    uint64_t network_rx_bytes;         // Received bytes
    uint64_t network_tx_bytes;         // Transmitted bytes
    uint64_t network_rx_packets;       // Received packets
    uint64_t network_tx_packets;       // Transmitted packets
    
    // Timestamps
    time_t collection_time;            // When metrics were collected
    uint64_t uptime_seconds;           // Uptime since start
    
    // Status
    bool is_running;                   // Instance running state
    uint32_t restart_count;            // Number of restarts
    
} instance_metrics_t;

/**
 * Historical metrics (for trending)
 */
typedef struct {
    instance_metrics_t *snapshots;      // Array of metrics snapshots
    uint32_t snapshot_count;            // Current number of snapshots
    uint32_t max_snapshots;             // Maximum to keep (e.g., 60 for 1-minute history)
    uint32_t index;                     // Current write position (circular buffer)
} metrics_history_t;

/**
 * Metrics collector
 */
typedef struct {
    char *instance_id;
    char *pod_name;
    char *namespace;
    uint32_t pid;                       // Process ID for QEMU
    
    instance_metrics_t current_metrics;
    metrics_history_t history;
    
    // Sampling configuration
    uint32_t sample_interval_seconds;
    time_t last_collection;
    
    // Memory limits (from pod spec)
    uint64_t memory_limit_bytes;
    uint64_t memory_request_bytes;
    uint64_t cpu_limit_millicores;
    uint64_t cpu_request_millicores;
    
    // Previous values (for delta calculation)
    uint64_t prev_cpu_time_ms;
    uint64_t prev_network_rx_bytes;
    uint64_t prev_network_tx_bytes;
    
    pthread_mutex_t mutex;
    
} metrics_collector_t;

/**
 * Create metrics collector for instance
 */
metrics_collector_t* metrics_collector_create(const char *instance_id,
                                              const char *pod_name,
                                              const char *namespace,
                                              uint32_t pid,
                                              uint64_t memory_limit,
                                              uint64_t cpu_limit_millicores);

/**
 * Free metrics collector
 */
void metrics_collector_free(metrics_collector_t *collector);

/**
 * Collect current metrics from QEMU process
 */
bool metrics_collector_collect(metrics_collector_t *collector);

/**
 * Get current metrics snapshot
 */
bool metrics_collector_get_current(metrics_collector_t *collector,
                                    instance_metrics_t *out_metrics);

/**
 * Get metrics history (for graphing/trending)
 */
bool metrics_collector_get_history(metrics_collector_t *collector,
                                    instance_metrics_t **out_history,
                                    uint32_t *out_count);

/**
 * Get average metrics over time window
 */
bool metrics_collector_get_average(metrics_collector_t *collector,
                                    uint32_t time_window_seconds,
                                    instance_metrics_t *out_average);

/**
 * Check if metrics exceed thresholds
 */
bool metrics_collector_exceeds_memory(metrics_collector_t *collector);
bool metrics_collector_exceeds_cpu(metrics_collector_t *collector);
bool metrics_collector_exceeds_network(metrics_collector_t *collector, uint64_t threshold_bytes);

/**
 * Free metrics snapshot
 */
void metrics_collector_free_metrics(instance_metrics_t *metrics);

#endif // SIRAH_METRICS_COLLECTOR_H
