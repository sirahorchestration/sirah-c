// internal/kubelet/metrics_collector.c
// Metrics collection implementation (Phase 4)

#include "metrics_collector.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include <time.h>

// ============================================================================
// Metrics Collection
// ============================================================================

metrics_collector_t* metrics_collector_create(const char* node_name) {
    metrics_collector_t* collector = (metrics_collector_t*)malloc(sizeof(metrics_collector_t));
    if (!collector) return NULL;
    
    collector->node_name = strdup(node_name ? node_name : "unknown");
    
    // Initialize node metrics
    collector->node_memory_bytes = 0;
    collector->node_memory_allocatable = 0;
    collector->node_memory_available = 0;
    collector->node_cpu_cores = 0;
    collector->node_disk_bytes = 0;
    collector->node_disk_available = 0;
    
    // Initialize counters
    collector->total_pods = 0;
    collector->running_pods = 0;
    collector->pending_pods = 0;
    collector->failed_pods = 0;
    collector->total_memory_requested = 0;
    collector->total_memory_limits = 0;
    collector->total_cpu_requested = 0;
    collector->total_cpu_limits = 0;
    
    // Initialize operation counters
    collector->total_pod_creations = 0;
    collector->failed_pod_creations = 0;
    collector->pod_creation_latency_avg_ms = 0;
    collector->total_pod_deletions = 0;
    
    // Initialize health metrics
    collector->unhealthy_pods = 0;
    collector->probe_executions = 0;
    collector->probe_failures = 0;
    
    // Initialize timing
    collector->last_sync_time = time(NULL);
    collector->uptime_seconds = 0;
    
    // Initialize pod list
    collector->pods = (pod_metrics_t*)malloc(sizeof(pod_metrics_t) * 256);
    collector->num_pods = 0;
    
    return collector;
}

void metrics_collector_free(metrics_collector_t* collector) {
    if (!collector) return;
    
    // Free node name
    if (collector->node_name) free(collector->node_name);
    
    // Free pods
    for (int i = 0; i < collector->num_pods; i++) {
        if (collector->pods[i].pod_name) free(collector->pods[i].pod_name);
        if (collector->pods[i].namespace) free(collector->pods[i].namespace);
        if (collector->pods[i].status) free(collector->pods[i].status);
        
        // Free containers
        for (int j = 0; j < collector->pods[i].num_containers; j++) {
            if (collector->pods[i].containers[j].container_name) {
                free(collector->pods[i].containers[j].container_name);
            }
        }
        if (collector->pods[i].containers) {
            free(collector->pods[i].containers);
        }
    }
    
    if (collector->pods) free(collector->pods);
    free(collector);
}

// ============================================================================
// Update Metrics
// ============================================================================

int metrics_update_node_info(metrics_collector_t* collector,
                            long memory_bytes, double cpu_cores, long disk_bytes) {
    if (!collector) return -1;
    
    collector->node_memory_bytes = memory_bytes;
    collector->node_memory_allocatable = (long)(memory_bytes * 0.9);  // 90% allocatable
    collector->node_memory_available = memory_bytes;
    collector->node_cpu_cores = cpu_cores;
    collector->node_disk_bytes = disk_bytes;
    collector->node_disk_available = disk_bytes;
    
    return 0;
}

int metrics_update_pod_metrics(metrics_collector_t* collector,
                               const char* pod_name, const char* namespace,
                               pod_metrics_t* metrics) {
    if (!collector || !pod_name || !namespace || !metrics) return -1;
    
    // Find existing pod or create new one
    int pod_index = -1;
    for (int i = 0; i < collector->num_pods; i++) {
        if (strcmp(collector->pods[i].pod_name, pod_name) == 0 &&
            strcmp(collector->pods[i].namespace, namespace) == 0) {
            pod_index = i;
            break;
        }
    }
    
    if (pod_index < 0) {
        // New pod
        if (collector->num_pods >= 256) return -1;  // Max pods reached
        
        pod_index = collector->num_pods++;
        collector->pods[pod_index].pod_name = strdup(pod_name);
        collector->pods[pod_index].namespace = strdup(namespace);
        collector->pods[pod_index].status = strdup("running");
        collector->pods[pod_index].created_at = time(NULL);
        collector->pods[pod_index].started_at = time(NULL);
        collector->pods[pod_index].containers = (container_metrics_t*)malloc(sizeof(container_metrics_t) * 10);
        collector->pods[pod_index].num_containers = 0;
        collector->pods[pod_index].restarts = 0;
    }
    
    // Update pod metrics
    pod_metrics_t* pod = &collector->pods[pod_index];
    pod->total_memory_bytes = metrics->total_memory_bytes;
    pod->total_cpu_millicores = metrics->total_cpu_millicores;
    pod->total_disk_read_bytes = metrics->total_disk_read_bytes;
    pod->total_disk_write_bytes = metrics->total_disk_write_bytes;
    pod->total_network_rx_bytes = metrics->total_network_rx_bytes;
    pod->total_network_tx_bytes = metrics->total_network_tx_bytes;
    pod->last_updated = time(NULL);
    
    return 0;
}

int metrics_update_container_metrics(metrics_collector_t* collector,
                                     const char* pod_name, const char* namespace,
                                     const char* container_name,
                                     container_metrics_t* metrics) {
    if (!collector || !pod_name || !namespace || !container_name || !metrics) return -1;
    
    // Find pod
    int pod_index = -1;
    for (int i = 0; i < collector->num_pods; i++) {
        if (strcmp(collector->pods[i].pod_name, pod_name) == 0 &&
            strcmp(collector->pods[i].namespace, namespace) == 0) {
            pod_index = i;
            break;
        }
    }
    
    if (pod_index < 0) return -1;  // Pod not found
    
    // Find or create container
    pod_metrics_t* pod = &collector->pods[pod_index];
    int container_index = -1;
    
    for (int i = 0; i < pod->num_containers; i++) {
        if (strcmp(pod->containers[i].container_name, container_name) == 0) {
            container_index = i;
            break;
        }
    }
    
    if (container_index < 0) {
        if (pod->num_containers >= 10) return -1;  // Max containers
        container_index = pod->num_containers++;
        pod->containers[container_index].container_name = strdup(container_name);
    }
    
    // Update container metrics
    container_metrics_t* container = &pod->containers[container_index];
    container->memory_bytes = metrics->memory_bytes;
    container->memory_limit_bytes = metrics->memory_limit_bytes;
    container->cpu_millicores = metrics->cpu_millicores;
    container->cpu_limit_millicores = metrics->cpu_limit_millicores;
    container->disk_read_bytes = metrics->disk_read_bytes;
    container->disk_write_bytes = metrics->disk_write_bytes;
    container->disk_iops_read = metrics->disk_iops_read;
    container->disk_iops_write = metrics->disk_iops_write;
    container->network_rx_bytes = metrics->network_rx_bytes;
    container->network_tx_bytes = metrics->network_tx_bytes;
    container->network_rx_packets = metrics->network_rx_packets;
    container->network_tx_packets = metrics->network_tx_packets;
    container->network_rx_errors = metrics->network_rx_errors;
    container->network_tx_errors = metrics->network_tx_errors;
    container->process_count = metrics->process_count;
    container->thread_count = metrics->thread_count;
    container->measurement_time = time(NULL);
    
    return 0;
}

// ============================================================================
// Record Operations
// ============================================================================

int metrics_record_pod_creation(metrics_collector_t* collector, double latency_ms) {
    if (!collector) return -1;
    
    collector->total_pod_creations++;
    
    // Update rolling average latency
    double old_avg = collector->pod_creation_latency_avg_ms;
    int count = collector->total_pod_creations;
    collector->pod_creation_latency_avg_ms = (old_avg * (count - 1) + latency_ms) / count;
    
    return 0;
}

int metrics_record_pod_deletion(metrics_collector_t* collector) {
    if (!collector) return -1;
    collector->total_pod_deletions++;
    return 0;
}

int metrics_record_pod_failure(metrics_collector_t* collector) {
    if (!collector) return -1;
    collector->failed_pod_creations++;
    return 0;
}

int metrics_record_probe_execution(metrics_collector_t* collector, int success) {
    if (!collector) return -1;
    
    collector->probe_executions++;
    if (!success) {
        collector->probe_failures++;
    }
    
    return 0;
}

// ============================================================================
// Get Metrics
// ============================================================================

pod_metrics_t* metrics_get_pod(metrics_collector_t* collector,
                               const char* pod_name, const char* namespace) {
    if (!collector || !pod_name || !namespace) return NULL;
    
    for (int i = 0; i < collector->num_pods; i++) {
        if (strcmp(collector->pods[i].pod_name, pod_name) == 0 &&
            strcmp(collector->pods[i].namespace, namespace) == 0) {
            return &collector->pods[i];
        }
    }
    
    return NULL;
}

container_metrics_t* metrics_get_container(metrics_collector_t* collector,
                                           const char* pod_name, const char* namespace,
                                           const char* container_name) {
    if (!collector || !pod_name || !namespace || !container_name) return NULL;
    
    pod_metrics_t* pod = metrics_get_pod(collector, pod_name, namespace);
    if (!pod) return NULL;
    
    for (int i = 0; i < pod->num_containers; i++) {
        if (strcmp(pod->containers[i].container_name, container_name) == 0) {
            return &pod->containers[i];
        }
    }
    
    return NULL;
}

// ============================================================================
// Export Metrics - Prometheus Format
// ============================================================================

char* metrics_to_prometheus(metrics_collector_t* collector) {
    if (!collector) return NULL;
    
    // Allocate large buffer for Prometheus metrics
    char* output = (char*)malloc(65536);
    if (!output) return NULL;
    
    int offset = 0;
    
    // Node metrics
    offset += snprintf(output + offset, 65536 - offset,
        "# HELP sirah_node_memory_bytes Total memory available on node\n"
        "# TYPE sirah_node_memory_bytes gauge\n"
        "sirah_node_memory_bytes{node=\"%s\"} %ld\n\n"
        "# HELP sirah_node_memory_allocatable Allocatable memory on node\n"
        "# TYPE sirah_node_memory_allocatable gauge\n"
        "sirah_node_memory_allocatable{node=\"%s\"} %ld\n\n"
        "# HELP sirah_node_memory_available Available memory on node\n"
        "# TYPE sirah_node_memory_available gauge\n"
        "sirah_node_memory_available{node=\"%s\"} %ld\n\n"
        "# HELP sirah_node_cpu_cores Total CPU cores on node\n"
        "# TYPE sirah_node_cpu_cores gauge\n"
        "sirah_node_cpu_cores{node=\"%s\"} %.2f\n\n",
        collector->node_name, collector->node_memory_bytes,
        collector->node_name, collector->node_memory_allocatable,
        collector->node_name, collector->node_memory_available,
        collector->node_name, collector->node_cpu_cores);
    
    // Pod count metrics
    offset += snprintf(output + offset, 65536 - offset,
        "# HELP sirah_node_pods_total Total pods on node\n"
        "# TYPE sirah_node_pods_total gauge\n"
        "sirah_node_pods_total{node=\"%s\"} %d\n"
        "sirah_node_pods_total{node=\"%s\",state=\"running\"} %d\n"
        "sirah_node_pods_total{node=\"%s\",state=\"pending\"} %d\n"
        "sirah_node_pods_total{node=\"%s\",state=\"failed\"} %d\n\n",
        collector->node_name, collector->total_pods,
        collector->node_name, collector->running_pods,
        collector->node_name, collector->pending_pods,
        collector->node_name, collector->failed_pods);
    
    // Resource usage metrics
    offset += snprintf(output + offset, 65536 - offset,
        "# HELP sirah_node_memory_requested Total memory requested\n"
        "# TYPE sirah_node_memory_requested gauge\n"
        "sirah_node_memory_requested{node=\"%s\"} %ld\n\n"
        "# HELP sirah_node_memory_limits Total memory limits\n"
        "# TYPE sirah_node_memory_limits gauge\n"
        "sirah_node_memory_limits{node=\"%s\"} %ld\n\n"
        "# HELP sirah_node_cpu_requested Total CPU requested (millicores)\n"
        "# TYPE sirah_node_cpu_requested gauge\n"
        "sirah_node_cpu_requested{node=\"%s\"} %.0f\n\n"
        "# HELP sirah_node_cpu_limits Total CPU limits (millicores)\n"
        "# TYPE sirah_node_cpu_limits gauge\n"
        "sirah_node_cpu_limits{node=\"%s\"} %.0f\n\n",
        collector->node_name, collector->total_memory_requested,
        collector->node_name, collector->total_memory_limits,
        collector->node_name, collector->total_cpu_requested,
        collector->node_name, collector->total_cpu_limits);
    
    // Operation metrics
    offset += snprintf(output + offset, 65536 - offset,
        "# HELP sirah_node_pod_creations_total Total pod creations\n"
        "# TYPE sirah_node_pod_creations_total counter\n"
        "sirah_node_pod_creations_total{node=\"%s\"} %ld\n\n"
        "# HELP sirah_node_pod_creation_failures_total Failed pod creations\n"
        "# TYPE sirah_node_pod_creation_failures_total counter\n"
        "sirah_node_pod_creation_failures_total{node=\"%s\"} %ld\n\n"
        "# HELP sirah_node_pod_creation_latency_avg_ms Average pod creation latency\n"
        "# TYPE sirah_node_pod_creation_latency_avg_ms gauge\n"
        "sirah_node_pod_creation_latency_avg_ms{node=\"%s\"} %.2f\n\n"
        "# HELP sirah_node_pod_deletions_total Total pod deletions\n"
        "# TYPE sirah_node_pod_deletions_total counter\n"
        "sirah_node_pod_deletions_total{node=\"%s\"} %ld\n\n",
        collector->node_name, collector->total_pod_creations,
        collector->node_name, collector->failed_pod_creations,
        collector->node_name, collector->pod_creation_latency_avg_ms,
        collector->node_name, collector->total_pod_deletions);
    
    // Health metrics
    offset += snprintf(output + offset, 65536 - offset,
        "# HELP sirah_node_unhealthy_pods Pods with failed health checks\n"
        "# TYPE sirah_node_unhealthy_pods gauge\n"
        "sirah_node_unhealthy_pods{node=\"%s\"} %d\n\n"
        "# HELP sirah_node_probe_executions_total Total health probe executions\n"
        "# TYPE sirah_node_probe_executions_total counter\n"
        "sirah_node_probe_executions_total{node=\"%s\"} %d\n\n"
        "# HELP sirah_node_probe_failures_total Failed health probe executions\n"
        "# TYPE sirah_node_probe_failures_total counter\n"
        "sirah_node_probe_failures_total{node=\"%s\"} %d\n\n",
        collector->node_name, collector->unhealthy_pods,
        collector->node_name, collector->probe_executions,
        collector->node_name, collector->probe_failures);
    
    // Pod metrics
    for (int i = 0; i < collector->num_pods; i++) {
        pod_metrics_t* pod = &collector->pods[i];
        
        offset += snprintf(output + offset, 65536 - offset,
            "# Pod %s/%s metrics\n"
            "sirah_pod_memory_bytes{pod=\"%s\",namespace=\"%s\"} %ld\n"
            "sirah_pod_cpu_millicores{pod=\"%s\",namespace=\"%s\"} %.0f\n"
            "sirah_pod_restarts{pod=\"%s\",namespace=\"%s\"} %d\n\n",
            pod->namespace, pod->pod_name,
            pod->pod_name, pod->namespace, pod->total_memory_bytes,
            pod->pod_name, pod->namespace, pod->total_cpu_millicores,
            pod->pod_name, pod->namespace, pod->restarts);
    }
    
    return output;
}

// ============================================================================
// Export Metrics - JSON Format
// ============================================================================

char* metrics_to_json(metrics_collector_t* collector) {
    if (!collector) return NULL;
    
    json_object* root = json_object_new_object();
    
    // Node info
    json_object* node = json_object_new_object();
    json_object_object_add(node, "name", json_object_new_string(collector->node_name));
    json_object_object_add(node, "memory_bytes", json_object_new_int64(collector->node_memory_bytes));
    json_object_object_add(node, "cpu_cores", json_object_new_double(collector->node_cpu_cores));
    json_object_object_add(node, "disk_bytes", json_object_new_int64(collector->node_disk_bytes));
    
    // Pod summary
    json_object* pod_summary = json_object_new_object();
    json_object_object_add(pod_summary, "total", json_object_new_int(collector->total_pods));
    json_object_object_add(pod_summary, "running", json_object_new_int(collector->running_pods));
    json_object_object_add(pod_summary, "pending", json_object_new_int(collector->pending_pods));
    json_object_object_add(pod_summary, "failed", json_object_new_int(collector->failed_pods));
    json_object_object_add(node, "pods", pod_summary);
    
    json_object_object_add(root, "node", node);
    
    char* output = strdup(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY));
    json_object_put(root);
    
    return output;
}

// ============================================================================
// Summary Statistics
// ============================================================================

void metrics_print_summary(metrics_collector_t* collector) {
    if (!collector) return;
    
    printf("\n");
    printf("=== Kubelet Metrics Summary ===\n");
    printf("Node: %s\n", collector->node_name);
    printf("Memory: %ld bytes total, %ld bytes available\n",
           collector->node_memory_bytes, collector->node_memory_available);
    printf("CPU: %.2f cores\n", collector->node_cpu_cores);
    printf("\nPod Status:\n");
    printf("  Total: %d\n", collector->total_pods);
    printf("  Running: %d\n", collector->running_pods);
    printf("  Pending: %d\n", collector->pending_pods);
    printf("  Failed: %d\n", collector->failed_pods);
    printf("\nOperations:\n");
    printf("  Pod Creations: %ld (%.0f failures)\n",
           collector->total_pod_creations, (double)collector->failed_pod_creations);
    printf("  Pod Creation Latency: %.2f ms average\n",
           collector->pod_creation_latency_avg_ms);
    printf("  Pod Deletions: %ld\n", collector->total_pod_deletions);
    printf("\nHealth:\n");
    printf("  Unhealthy Pods: %d\n", collector->unhealthy_pods);
    printf("  Probe Executions: %d (%d failures)\n",
           collector->probe_executions, collector->probe_failures);
    printf("\n");
}
