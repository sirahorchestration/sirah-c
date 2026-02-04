// internal/kubelet/metrics_collector.h
// Metrics collection for kubelet and pods (Phase 4)

#ifndef SIRAH_METRICS_COLLECTOR_H
#define SIRAH_METRICS_COLLECTOR_H

#include <time.h>

// Metrics counter
typedef struct {
    char* name;
    char* description;
    char* type;         // "counter", "gauge", "histogram", "summary"
    double value;
    int count;
    char* labels;       // Prometheus label string (e.g., pod="nginx", ns="default")
} metric_t;

// Container metrics
typedef struct {
    char* container_name;
    
    // Resource usage
    long memory_bytes;          // Current memory usage
    long memory_limit_bytes;    // Memory limit
    double cpu_millicores;      // CPU usage in millicores
    long cpu_limit_millicores;  // CPU limit
    
    // I/O metrics
    long disk_read_bytes;       // Total bytes read
    long disk_write_bytes;      // Total bytes written
    long disk_iops_read;        // I/O operations per second (read)
    long disk_iops_write;       // I/O operations per second (write)
    
    // Network metrics
    long network_rx_bytes;      // Bytes received
    long network_tx_bytes;      // Bytes transmitted
    long network_rx_packets;    // Packets received
    long network_tx_packets;    // Packets transmitted
    long network_rx_errors;     // RX errors
    long network_tx_errors;     // TX errors
    
    // Process metrics
    int process_count;          // Number of processes
    int thread_count;           // Number of threads
    
    // Timing
    time_t measurement_time;
} container_metrics_t;

// Pod metrics
typedef struct {
    char* pod_name;
    char* namespace;
    
    // Aggregated metrics from all containers
    long total_memory_bytes;
    double total_cpu_millicores;
    long total_disk_read_bytes;
    long total_disk_write_bytes;
    long total_network_rx_bytes;
    long total_network_tx_bytes;
    
    // Container metrics
    container_metrics_t* containers;
    int num_containers;
    
    // Pod timing
    time_t created_at;
    time_t started_at;
    time_t last_updated;
    
    // Pod status
    int restarts;
    char* status;
} pod_metrics_t;

// Node (kubelet) metrics
typedef struct {
    char* node_name;
    
    // System metrics
    long node_memory_bytes;         // Total node memory
    long node_memory_allocatable;   // Allocatable memory
    long node_memory_available;     // Available memory
    double node_cpu_cores;          // Total CPU cores
    long node_disk_bytes;           // Total disk space
    long node_disk_available;       // Available disk space
    
    // Kubelet metrics
    int total_pods;
    int running_pods;
    int pending_pods;
    int failed_pods;
    long total_memory_requested;    // Sum of memory requests
    long total_memory_limits;       // Sum of memory limits
    double total_cpu_requested;     // Sum of CPU requests (millicores)
    double total_cpu_limits;        // Sum of CPU limits (millicores)
    
    // Operations metrics
    long total_pod_creations;
    long failed_pod_creations;
    double pod_creation_latency_avg_ms;
    long total_pod_deletions;
    
    // Health metrics
    int unhealthy_pods;             // Pods with failed health checks
    int probe_executions;
    int probe_failures;
    
    // Timing
    time_t last_sync_time;
    time_t uptime_seconds;
    
    // Pod metrics
    pod_metrics_t* pods;
    int num_pods;
} metrics_collector_t;

// Metrics operations
metrics_collector_t* metrics_collector_create(const char* node_name);
void metrics_collector_free(metrics_collector_t* collector);

// Update metrics
int metrics_update_node_info(metrics_collector_t* collector, 
                            long memory_bytes, double cpu_cores, long disk_bytes);
int metrics_update_pod_metrics(metrics_collector_t* collector,
                               const char* pod_name, const char* namespace,
                               pod_metrics_t* metrics);
int metrics_update_container_metrics(metrics_collector_t* collector,
                                     const char* pod_name, const char* namespace,
                                     const char* container_name,
                                     container_metrics_t* metrics);

// Record operations
int metrics_record_pod_creation(metrics_collector_t* collector, double latency_ms);
int metrics_record_pod_deletion(metrics_collector_t* collector);
int metrics_record_pod_failure(metrics_collector_t* collector);
int metrics_record_probe_execution(metrics_collector_t* collector, int success);

// Get specific metrics
pod_metrics_t* metrics_get_pod(metrics_collector_t* collector,
                               const char* pod_name, const char* namespace);
container_metrics_t* metrics_get_container(metrics_collector_t* collector,
                                           const char* pod_name, const char* namespace,
                                           const char* container_name);

// Export metrics
/**
 * Generate Prometheus-format metrics output
 * Returns dynamically allocated string
 */
char* metrics_to_prometheus(metrics_collector_t* collector);

/**
 * Generate JSON-format metrics output
 * Returns dynamically allocated string
 */
char* metrics_to_json(metrics_collector_t* collector);

// Get summary statistics
void metrics_print_summary(metrics_collector_t* collector);

#endif // SIRAH_METRICS_COLLECTOR_H
