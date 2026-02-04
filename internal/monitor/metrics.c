/*
 * metrics.c
 * 
 * Instance and node metrics collection implementation
 */

#include "metrics.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <time.h>

/**
 * Create instance metrics tracker
 */
instance_metrics_t* instance_metrics_create(const char *instance_id,
                                            const char *namespace) {
    if (!instance_id || !namespace) {
        return NULL;
    }
    
    instance_metrics_t *metrics = malloc(sizeof(instance_metrics_t));
    if (!metrics) return NULL;
    
    memset(metrics, 0, sizeof(instance_metrics_t));
    
    metrics->instance_id = strdup(instance_id);
    metrics->namespace = strdup(namespace);
    
    if (!metrics->instance_id || !metrics->namespace) {
        free(metrics->instance_id);
        free(metrics->namespace);
        free(metrics);
        return NULL;
    }
    
    metrics->collection_time = time(NULL);
    metrics->last_update_time = metrics->collection_time;
    
    return metrics;
}

/**
 * Free instance metrics
 */
void instance_metrics_free(instance_metrics_t *metrics) {
    if (!metrics) return;
    
    free(metrics->instance_id);
    free(metrics->namespace);
    free(metrics);
}

/**
 * Update instance metrics from QEMU process
 * In production, would read from /proc/{pid}/stat, cgroups, /sys/class/net, etc.
 */
bool instance_metrics_collect(instance_metrics_t *metrics,
                             uint64_t memory_limit_bytes) {
    if (!metrics) return false;
    
    // In a real implementation, would:
    // 1. Find QEMU process for this instance
    // 2. Read /proc/{pid}/stat for CPU metrics
    // 3. Read cgroup memory.usage_in_bytes for memory
    // 4. Read /proc/net/dev for network stats
    // 5. Calculate percentages based on node totals
    
    // For now, simulate reasonable values
    // CPU: 25-75% usage
    metrics->cpu_usage_percent = 25.0 + (rand() % 50);
    metrics->cpu_time_ms = (time(NULL) % 3600) * 1000;
    
    // Memory: use actual limit
    metrics->memory_limit_bytes = memory_limit_bytes;
    metrics->memory_usage_bytes = (memory_limit_bytes * (20 + rand() % 60)) / 100;
    metrics->memory_usage_percent = (metrics->memory_usage_bytes * 100.0) / memory_limit_bytes;
    metrics->memory_rss_bytes = metrics->memory_usage_bytes;
    
    // Network: simulated
    metrics->network_bytes_sent = 1000000 + (rand() % 5000000);
    metrics->network_bytes_received = 2000000 + (rand() % 10000000);
    metrics->network_packets_sent = 10000 + (rand() % 50000);
    metrics->network_packets_received = 20000 + (rand() % 100000);
    
    // Block I/O: simulated
    metrics->block_bytes_read = 100000 + (rand() % 1000000);
    metrics->block_bytes_written = 50000 + (rand() % 500000);
    metrics->block_read_ops = 100 + (rand() % 1000);
    metrics->block_write_ops = 50 + (rand() % 500);
    
    metrics->collection_time = time(NULL);
    metrics->last_update_time = metrics->collection_time;
    
    return true;
}

/**
 * Get CPU usage percentage
 */
double instance_metrics_get_cpu_percent(instance_metrics_t *metrics) {
    if (!metrics) return 0.0;
    return metrics->cpu_usage_percent;
}

/**
 * Get memory usage percentage
 */
double instance_metrics_get_memory_percent(instance_metrics_t *metrics) {
    if (!metrics) return 0.0;
    return metrics->memory_usage_percent;
}

/**
 * Get network I/O statistics
 */
void instance_metrics_get_network_stats(instance_metrics_t *metrics,
                                       uint64_t *out_bytes_sent,
                                       uint64_t *out_bytes_received,
                                       uint64_t *out_packets_sent,
                                       uint64_t *out_packets_received) {
    if (!metrics) return;
    
    if (out_bytes_sent) *out_bytes_sent = metrics->network_bytes_sent;
    if (out_bytes_received) *out_bytes_received = metrics->network_bytes_received;
    if (out_packets_sent) *out_packets_sent = metrics->network_packets_sent;
    if (out_packets_received) *out_packets_received = metrics->network_packets_received;
}

/**
 * Get block I/O statistics
 */
void instance_metrics_get_block_stats(instance_metrics_t *metrics,
                                     uint64_t *out_bytes_read,
                                     uint64_t *out_bytes_written,
                                     uint64_t *out_read_ops,
                                     uint64_t *out_write_ops) {
    if (!metrics) return;
    
    if (out_bytes_read) *out_bytes_read = metrics->block_bytes_read;
    if (out_bytes_written) *out_bytes_written = metrics->block_bytes_written;
    if (out_read_ops) *out_read_ops = metrics->block_read_ops;
    if (out_write_ops) *out_write_ops = metrics->block_write_ops;
}

/**
 * Create node metrics tracker
 */
node_metrics_t* node_metrics_create(const char *node_name) {
    if (!node_name) {
        return NULL;
    }
    
    node_metrics_t *metrics = malloc(sizeof(node_metrics_t));
    if (!metrics) return NULL;
    
    memset(metrics, 0, sizeof(node_metrics_t));
    
    metrics->node_name = strdup(node_name);
    if (!metrics->node_name) {
        free(metrics);
        return NULL;
    }
    
    metrics->collection_time = time(NULL);
    metrics->last_update_time = metrics->collection_time;
    
    // Default values
    metrics->cpu_cores = 4;  // Assume 4 cores
    metrics->cpu_allocatable_millis = 4000;  // 4 cores × 1000 millis per core
    metrics->pod_count_limit = 110;  // Kubernetes default
    
    return metrics;
}

/**
 * Free node metrics
 */
void node_metrics_free(node_metrics_t *metrics) {
    if (!metrics) return;
    
    free(metrics->node_name);
    free(metrics);
}

/**
 * Update node metrics from all instances on node
 */
bool node_metrics_collect(node_metrics_t *metrics,
                         instance_metrics_t **instances,
                         uint32_t instance_count,
                         uint64_t total_memory_bytes,
                         uint32_t max_pods) {
    if (!metrics) return false;
    
    metrics->pod_count = instance_count;
    metrics->pod_count_limit = max_pods;
    metrics->pod_count_allocatable = max_pods - instance_count;
    metrics->memory_total_bytes = total_memory_bytes;
    metrics->memory_allocatable_bytes = total_memory_bytes;
    
    // Aggregate metrics from all instances
    double total_cpu_percent = 0.0;
    uint64_t total_memory_used = 0;
    uint64_t total_network_sent = 0;
    uint64_t total_network_received = 0;
    
    if (instances) {
        for (uint32_t i = 0; i < instance_count; i++) {
            if (instances[i]) {
                total_cpu_percent += instances[i]->cpu_usage_percent;
                total_memory_used += instances[i]->memory_usage_bytes;
                total_network_sent += instances[i]->network_bytes_sent;
                total_network_received += instances[i]->network_bytes_received;
            }
        }
    }
    
    // Calculate aggregates
    metrics->total_cpu_usage_percent = total_cpu_percent;
    metrics->cpu_used_millis = (uint64_t)(total_cpu_percent * 10);  // Convert to millicores
    
    metrics->memory_used_bytes = total_memory_used;
    metrics->memory_available_bytes = total_memory_bytes - total_memory_used;
    metrics->total_memory_usage_percent = (total_memory_used * 100.0) / total_memory_bytes;
    
    metrics->node_network_bytes_sent = total_network_sent;
    metrics->node_network_bytes_received = total_network_received;
    
    metrics->collection_time = time(NULL);
    metrics->last_update_time = metrics->collection_time;
    
    return true;
}

/**
 * Get node CPU usage percentage
 */
double node_metrics_get_cpu_percent(node_metrics_t *metrics) {
    if (!metrics) return 0.0;
    return metrics->total_cpu_usage_percent;
}

/**
 * Get node memory usage percentage
 */
double node_metrics_get_memory_percent(node_metrics_t *metrics) {
    if (!metrics) return 0.0;
    return metrics->total_memory_usage_percent;
}

/**
 * Get pod capacity on node
 */
void node_metrics_get_pod_capacity(node_metrics_t *metrics,
                                  uint32_t *out_running,
                                  uint32_t *out_limit,
                                  uint32_t *out_available) {
    if (!metrics) return;
    
    if (out_running) *out_running = metrics->pod_count;
    if (out_limit) *out_limit = metrics->pod_count_limit;
    if (out_available) *out_available = metrics->pod_count_allocatable;
}

/**
 * Check if node has available capacity
 */
bool node_metrics_has_capacity(node_metrics_t *metrics) {
    if (!metrics) return false;
    
    // Check pod capacity
    if (metrics->pod_count >= metrics->pod_count_limit) {
        return false;
    }
    
    // Check memory capacity (need 100MB minimum available)
    if (metrics->memory_available_bytes < 100 * 1024 * 1024) {
        return false;
    }
    
    // Check CPU capacity (need some headroom)
    if (metrics->total_cpu_usage_percent >= 90.0) {
        return false;
    }
    
    return true;
}

/**
 * Metrics collection thread
 */
static void* metrics_collection_thread(void *arg) {
    metrics_collector_t *collector = (metrics_collector_t *)arg;
    
    while (collector->running) {
        pthread_mutex_lock(&collector->mutex);
        
        // Collect metrics from all instances
        if (collector->instances && collector->node_metrics) {
            for (uint32_t i = 0; i < collector->instance_count; i++) {
                if (collector->instances[i]) {
                    instance_metrics_collect(collector->instances[i], 512 * 1024 * 1024);
                }
            }
            
            // Update node metrics
            node_metrics_collect(collector->node_metrics,
                               collector->instances,
                               collector->instance_count,
                               4 * 1024 * 1024 * 1024,  // 4GB total
                               110);  // Default pod limit
        }
        
        pthread_mutex_unlock(&collector->mutex);
        
        // Sleep for interval
        sleep(collector->interval_seconds);
    }
    
    return NULL;
}

/**
 * Create metrics collector
 */
metrics_collector_t* metrics_collector_create(const char *node_name,
                                             uint32_t interval_seconds) {
    if (!node_name) {
        return NULL;
    }
    
    metrics_collector_t *collector = malloc(sizeof(metrics_collector_t));
    if (!collector) return NULL;
    
    memset(collector, 0, sizeof(metrics_collector_t));
    
    collector->interval_seconds = interval_seconds > 0 ? interval_seconds : METRICS_COLLECTION_INTERVAL;
    collector->node_metrics = node_metrics_create(node_name);
    collector->instances = NULL;
    collector->instance_count = 0;
    collector->running = false;
    
    pthread_mutex_init(&collector->mutex, NULL);
    
    if (!collector->node_metrics) {
        free(collector);
        return NULL;
    }
    
    return collector;
}

/**
 * Free metrics collector
 */
void metrics_collector_free(metrics_collector_t *collector) {
    if (!collector) return;
    
    if (collector->running) {
        metrics_collector_stop(collector);
    }
    
    // Free all instance metrics
    if (collector->instances) {
        for (uint32_t i = 0; i < collector->instance_count; i++) {
            if (collector->instances[i]) {
                instance_metrics_free(collector->instances[i]);
            }
        }
        free(collector->instances);
    }
    
    if (collector->node_metrics) {
        node_metrics_free(collector->node_metrics);
    }
    
    pthread_mutex_destroy(&collector->mutex);
    free(collector);
}

/**
 * Add instance to collection
 */
bool metrics_collector_add_instance(metrics_collector_t *collector,
                                   const char *instance_id,
                                   const char *namespace,
                                   uint64_t memory_limit_bytes) {
    if (!collector || !instance_id || !namespace) {
        return false;
    }
    
    pthread_mutex_lock(&collector->mutex);
    
    // Create new instance metrics
    instance_metrics_t *metrics = instance_metrics_create(instance_id, namespace);
    if (!metrics) {
        pthread_mutex_unlock(&collector->mutex);
        return false;
    }
    
    metrics->memory_limit_bytes = memory_limit_bytes;
    
    // Expand instances array
    instance_metrics_t **new_instances = realloc(collector->instances,
                                                 sizeof(instance_metrics_t *) * (collector->instance_count + 1));
    if (!new_instances) {
        instance_metrics_free(metrics);
        pthread_mutex_unlock(&collector->mutex);
        return false;
    }
    
    collector->instances = new_instances;
    collector->instances[collector->instance_count] = metrics;
    collector->instance_count++;
    
    pthread_mutex_unlock(&collector->mutex);
    return true;
}

/**
 * Remove instance from collection
 */
bool metrics_collector_remove_instance(metrics_collector_t *collector,
                                      const char *instance_id) {
    if (!collector || !instance_id) {
        return false;
    }
    
    pthread_mutex_lock(&collector->mutex);
    
    // Find and remove instance
    for (uint32_t i = 0; i < collector->instance_count; i++) {
        if (collector->instances[i] && strcmp(collector->instances[i]->instance_id, instance_id) == 0) {
            instance_metrics_free(collector->instances[i]);
            
            // Shift remaining instances
            for (uint32_t j = i; j < collector->instance_count - 1; j++) {
                collector->instances[j] = collector->instances[j + 1];
            }
            
            collector->instance_count--;
            
            // Shrink array if needed
            if (collector->instance_count == 0) {
                free(collector->instances);
                collector->instances = NULL;
            } else {
                instance_metrics_t **new_instances = realloc(collector->instances,
                                                            sizeof(instance_metrics_t *) * collector->instance_count);
                if (new_instances) {
                    collector->instances = new_instances;
                }
            }
            
            pthread_mutex_unlock(&collector->mutex);
            return true;
        }
    }
    
    pthread_mutex_unlock(&collector->mutex);
    return false;
}

/**
 * Start metrics collection thread
 */
bool metrics_collector_start(metrics_collector_t *collector) {
    if (!collector) return false;
    
    if (collector->running) return true;  // Already running
    
    collector->running = true;
    
    int ret = pthread_create(&collector->collection_thread, NULL,
                            metrics_collection_thread, collector);
    if (ret != 0) {
        collector->running = false;
        return false;
    }
    
    return true;
}

/**
 * Stop metrics collection thread
 */
bool metrics_collector_stop(metrics_collector_t *collector) {
    if (!collector) return false;
    
    if (!collector->running) return true;
    
    collector->running = false;
    pthread_join(collector->collection_thread, NULL);
    
    return true;
}

/**
 * Get current node metrics
 */
node_metrics_t* metrics_collector_get_node_metrics(metrics_collector_t *collector) {
    if (!collector) return NULL;
    return collector->node_metrics;
}

/**
 * Get instance metrics by ID
 */
instance_metrics_t* metrics_collector_get_instance_metrics(metrics_collector_t *collector,
                                                           const char *instance_id) {
    if (!collector || !instance_id) return NULL;
    
    pthread_mutex_lock(&collector->mutex);
    
    for (uint32_t i = 0; i < collector->instance_count; i++) {
        if (collector->instances[i] && 
            strcmp(collector->instances[i]->instance_id, instance_id) == 0) {
            instance_metrics_t *result = collector->instances[i];
            pthread_mutex_unlock(&collector->mutex);
            return result;
        }
    }
    
    pthread_mutex_unlock(&collector->mutex);
    return NULL;
}

/**
 * Export metrics in Prometheus format
 */
char* metrics_to_prometheus_format(metrics_collector_t *collector) {
    if (!collector) return NULL;
    
    // Allocate large buffer for all metrics
    char *output = malloc(64 * 1024);  // 64KB
    if (!output) return NULL;
    
    size_t offset = 0;
    char buffer[1024];
    
    pthread_mutex_lock(&collector->mutex);
    
    // Node metrics
    offset += snprintf(output + offset, 64 * 1024 - offset,
                      "# HELP node_cpu_usage_percent Node CPU usage percentage\n"
                      "# TYPE node_cpu_usage_percent gauge\n"
                      "node_cpu_usage_percent{node=\"%s\"} %.2f\n",
                      collector->node_metrics->node_name,
                      collector->node_metrics->total_cpu_usage_percent);
    
    offset += snprintf(output + offset, 64 * 1024 - offset,
                      "# HELP node_memory_usage_percent Node memory usage percentage\n"
                      "# TYPE node_memory_usage_percent gauge\n"
                      "node_memory_usage_percent{node=\"%s\"} %.2f\n",
                      collector->node_metrics->node_name,
                      collector->node_metrics->total_memory_usage_percent);
    
    offset += snprintf(output + offset, 64 * 1024 - offset,
                      "# HELP node_pod_count Current pod count on node\n"
                      "# TYPE node_pod_count gauge\n"
                      "node_pod_count{node=\"%s\"} %d\n",
                      collector->node_metrics->node_name,
                      collector->node_metrics->pod_count);
    
    // Instance metrics
    offset += snprintf(output + offset, 64 * 1024 - offset,
                      "# HELP pod_cpu_usage_percent Pod CPU usage percentage\n"
                      "# TYPE pod_cpu_usage_percent gauge\n");
    
    for (uint32_t i = 0; i < collector->instance_count; i++) {
        if (collector->instances[i]) {
            offset += snprintf(output + offset, 64 * 1024 - offset,
                              "pod_cpu_usage_percent{pod=\"%s\",namespace=\"%s\"} %.2f\n",
                              collector->instances[i]->instance_id,
                              collector->instances[i]->namespace,
                              collector->instances[i]->cpu_usage_percent);
        }
    }
    
    offset += snprintf(output + offset, 64 * 1024 - offset,
                      "# HELP pod_memory_usage_percent Pod memory usage percentage\n"
                      "# TYPE pod_memory_usage_percent gauge\n");
    
    for (uint32_t i = 0; i < collector->instance_count; i++) {
        if (collector->instances[i]) {
            offset += snprintf(output + offset, 64 * 1024 - offset,
                              "pod_memory_usage_percent{pod=\"%s\",namespace=\"%s\"} %.2f\n",
                              collector->instances[i]->instance_id,
                              collector->instances[i]->namespace,
                              collector->instances[i]->memory_usage_percent);
        }
    }
    
    pthread_mutex_unlock(&collector->mutex);
    
    return output;
}
