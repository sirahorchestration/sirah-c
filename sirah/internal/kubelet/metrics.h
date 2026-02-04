// internal/kubelet/metrics.h
// Kubelet metrics collection and Prometheus export

#ifndef SIRAH_KUBELET_METRICS_H
#define SIRAH_KUBELET_METRICS_H

#include <time.h>

// Metric types
typedef struct {
    // Pod metrics
    unsigned long pods_total;
    unsigned long pods_running;
    unsigned long pods_succeeded;
    unsigned long pods_failed;
    unsigned long pods_pending;
    
    // Container metrics
    unsigned long containers_total;
    unsigned long containers_running;
    unsigned long container_restarts_total;
    
    // Probe metrics
    unsigned long probe_executions_total;
    unsigned long probe_successes_total;
    unsigned long probe_failures_total;
    unsigned long startup_probes_passed;
    unsigned long readiness_probes_passed;
    unsigned long liveness_probes_passed;
    
    // Volume metrics
    unsigned long volumes_total;
    unsigned long volumes_mounted;
    
    // System metrics
    unsigned long uptime_seconds;
    unsigned long sync_cycles;
    unsigned long sync_errors;
    unsigned long memory_bytes;
    unsigned long cpu_millicores;
} kubelet_metrics_t;

// Metric collector singleton
typedef struct {
    kubelet_metrics_t current;
    time_t collection_time;
    time_t process_start_time;
} metrics_collector_t;

// ============================================================================
// Metrics Collection
// ============================================================================

/**
 * Initialize metrics collector
 */
metrics_collector_t* metrics_collector_create(void);

/**
 * Free metrics collector
 */
void metrics_collector_free(metrics_collector_t* collector);

/**
 * Update metric counters
 */
void metrics_collector_increment(metrics_collector_t* collector, const char* metric_name);

/**
 * Get current metrics snapshot
 */
kubelet_metrics_t metrics_collector_get_snapshot(metrics_collector_t* collector);

/**
 * Record pod state transition
 */
void metrics_collector_pod_event(metrics_collector_t* collector, const char* event_type);

/**
 * Record container restart
 */
void metrics_collector_container_restart(metrics_collector_t* collector);

/**
 * Record probe execution
 */
void metrics_collector_probe_event(metrics_collector_t* collector, const char* probe_type, int success);

/**
 * Update memory usage (from system)
 */
void metrics_collector_set_memory(metrics_collector_t* collector, unsigned long bytes);

/**
 * Update CPU usage (from system)
 */
void metrics_collector_set_cpu(metrics_collector_t* collector, unsigned long millicores);

// ============================================================================
// Prometheus Export
// ============================================================================

/**
 * Generate Prometheus metrics text output
 * Format: text/plain with OpenMetrics format
 * Must be freed by caller
 */
char* metrics_to_prometheus_text(metrics_collector_t* collector);

/**
 * Export metrics to file
 * Returns 0 on success, -1 on failure
 */
int metrics_export_to_file(metrics_collector_t* collector, const char* filepath);

/**
 * Export metrics as JSON
 * Returns JSON string, must be freed by caller
 */
char* metrics_to_json(metrics_collector_t* collector);

// ============================================================================
// Diagnostics
// ============================================================================

/**
 * Get human-readable metrics summary
 */
char* metrics_summary(metrics_collector_t* collector, char* buf, int buf_len);

/**
 * Check if kubelet is healthy based on metrics
 */
int metrics_health_check(metrics_collector_t* collector);

#endif // SIRAH_KUBELET_METRICS_H
