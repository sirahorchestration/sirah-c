// internal/kubelet/metrics.c
// Kubelet metrics collection and Prometheus export implementation

#include "metrics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <json-c/json.h>

// ============================================================================
// Metrics Collector Management
// ============================================================================

metrics_collector_t* metrics_collector_create(void) {
    metrics_collector_t* collector = (metrics_collector_t*)malloc(sizeof(metrics_collector_t));
    if (!collector) return NULL;
    
    memset(collector, 0, sizeof(metrics_collector_t));
    collector->process_start_time = time(NULL);
    collector->collection_time = time(NULL);
    
    return collector;
}

void metrics_collector_free(metrics_collector_t* collector) {
    if (!collector) return;
    free(collector);
}

kubelet_metrics_t metrics_collector_get_snapshot(metrics_collector_t* collector) {
    if (collector) {
        return collector->current;
    }
    
    kubelet_metrics_t empty = {0};
    return empty;
}

void metrics_collector_increment(metrics_collector_t* collector, const char* metric_name) {
    if (!collector || !metric_name) return;
    
    #define MATCH_METRIC(name) strcmp(metric_name, name) == 0
    
    if (MATCH_METRIC("pods_total")) {
        collector->current.pods_total++;
    } else if (MATCH_METRIC("pods_running")) {
        collector->current.pods_running++;
    } else if (MATCH_METRIC("pods_succeeded")) {
        collector->current.pods_succeeded++;
    } else if (MATCH_METRIC("pods_failed")) {
        collector->current.pods_failed++;
    } else if (MATCH_METRIC("pods_pending")) {
        collector->current.pods_pending++;
    } else if (MATCH_METRIC("containers_total")) {
        collector->current.containers_total++;
    } else if (MATCH_METRIC("containers_running")) {
        collector->current.containers_running++;
    } else if (MATCH_METRIC("probe_executions")) {
        collector->current.probe_executions_total++;
    } else if (MATCH_METRIC("probe_successes")) {
        collector->current.probe_successes_total++;
    } else if (MATCH_METRIC("probe_failures")) {
        collector->current.probe_failures_total++;
    } else if (MATCH_METRIC("sync_cycles")) {
        collector->current.sync_cycles++;
    } else if (MATCH_METRIC("sync_errors")) {
        collector->current.sync_errors++;
    } else if (MATCH_METRIC("volumes")) {
        collector->current.volumes_total++;
    } else if (MATCH_METRIC("volumes_mounted")) {
        collector->current.volumes_mounted++;
    }
    
    #undef MATCH_METRIC
    
    collector->collection_time = time(NULL);
}

void metrics_collector_pod_event(metrics_collector_t* collector, const char* event_type) {
    if (!collector || !event_type) return;
    
    if (strcmp(event_type, "created") == 0) {
        collector->current.pods_total++;
        collector->current.pods_pending++;
    } else if (strcmp(event_type, "running") == 0) {
        if (collector->current.pods_pending > 0) {
            collector->current.pods_pending--;
        }
        collector->current.pods_running++;
    } else if (strcmp(event_type, "succeeded") == 0) {
        if (collector->current.pods_running > 0) {
            collector->current.pods_running--;
        }
        collector->current.pods_succeeded++;
    } else if (strcmp(event_type, "failed") == 0) {
        if (collector->current.pods_running > 0) {
            collector->current.pods_running--;
        }
        if (collector->current.pods_pending > 0) {
            collector->current.pods_pending--;
        }
        collector->current.pods_failed++;
    }
    
    collector->collection_time = time(NULL);
}

void metrics_collector_container_restart(metrics_collector_t* collector) {
    if (!collector) return;
    
    collector->current.container_restarts_total++;
    collector->collection_time = time(NULL);
}

void metrics_collector_probe_event(metrics_collector_t* collector, const char* probe_type, int success) {
    if (!collector) return;
    
    collector->current.probe_executions_total++;
    
    if (success) {
        collector->current.probe_successes_total++;
        
        if (probe_type) {
            if (strcmp(probe_type, "startup") == 0) {
                collector->current.startup_probes_passed++;
            } else if (strcmp(probe_type, "readiness") == 0) {
                collector->current.readiness_probes_passed++;
            } else if (strcmp(probe_type, "liveness") == 0) {
                collector->current.liveness_probes_passed++;
            }
        }
    } else {
        collector->current.probe_failures_total++;
    }
    
    collector->collection_time = time(NULL);
}

void metrics_collector_set_memory(metrics_collector_t* collector, unsigned long bytes) {
    if (!collector) return;
    
    collector->current.memory_bytes = bytes;
}

void metrics_collector_set_cpu(metrics_collector_t* collector, unsigned long millicores) {
    if (!collector) return;
    
    collector->current.cpu_millicores = millicores;
}

// ============================================================================
// Prometheus Export
// ============================================================================

char* metrics_to_prometheus_text(metrics_collector_t* collector) {
    if (!collector) return NULL;
    
    char* output = (char*)malloc(8192);
    if (!output) return NULL;
    
    int offset = 0;
    time_t uptime = time(NULL) - collector->process_start_time;
    
    // Prometheus format: TYPE comments, then HELP comments, then metrics
    
    offset += snprintf(output + offset, 8192 - offset,
        "# HELP kubelet_pods_total Total pods on this node\n"
        "# TYPE kubelet_pods_total counter\n"
        "kubelet_pods_total %lu\n",
        collector->current.pods_total);
    
    offset += snprintf(output + offset, 8192 - offset,
        "# HELP kubelet_pods_running Number of running pods\n"
        "# TYPE kubelet_pods_running gauge\n"
        "kubelet_pods_running %lu\n",
        collector->current.pods_running);
    
    offset += snprintf(output + offset, 8192 - offset,
        "# HELP kubelet_pods_succeeded Number of succeeded pods\n"
        "# TYPE kubelet_pods_succeeded counter\n"
        "kubelet_pods_succeeded %lu\n",
        collector->current.pods_succeeded);
    
    offset += snprintf(output + offset, 8192 - offset,
        "# HELP kubelet_pods_failed Number of failed pods\n"
        "# TYPE kubelet_pods_failed counter\n"
        "kubelet_pods_failed %lu\n",
        collector->current.pods_failed);
    
    offset += snprintf(output + offset, 8192 - offset,
        "# HELP kubelet_pods_pending Number of pending pods\n"
        "# TYPE kubelet_pods_pending gauge\n"
        "kubelet_pods_pending %lu\n",
        collector->current.pods_pending);
    
    offset += snprintf(output + offset, 8192 - offset,
        "# HELP kubelet_container_restarts_total Total container restarts\n"
        "# TYPE kubelet_container_restarts_total counter\n"
        "kubelet_container_restarts_total %lu\n",
        collector->current.container_restarts_total);
    
    offset += snprintf(output + offset, 8192 - offset,
        "# HELP kubelet_probe_executions_total Total probe executions\n"
        "# TYPE kubelet_probe_executions_total counter\n"
        "kubelet_probe_executions_total %lu\n",
        collector->current.probe_executions_total);
    
    offset += snprintf(output + offset, 8192 - offset,
        "# HELP kubelet_probe_successes_total Total successful probes\n"
        "# TYPE kubelet_probe_successes_total counter\n"
        "kubelet_probe_successes_total %lu\n",
        collector->current.probe_successes_total);
    
    offset += snprintf(output + offset, 8192 - offset,
        "# HELP kubelet_probe_failures_total Total failed probes\n"
        "# TYPE kubelet_probe_failures_total counter\n"
        "kubelet_probe_failures_total %lu\n",
        collector->current.probe_failures_total);
    
    offset += snprintf(output + offset, 8192 - offset,
        "# HELP kubelet_sync_cycles_total Total sync cycles\n"
        "# TYPE kubelet_sync_cycles_total counter\n"
        "kubelet_sync_cycles_total %lu\n",
        collector->current.sync_cycles);
    
    offset += snprintf(output + offset, 8192 - offset,
        "# HELP kubelet_sync_errors_total Total sync errors\n"
        "# TYPE kubelet_sync_errors_total counter\n"
        "kubelet_sync_errors_total %lu\n",
        collector->current.sync_errors);
    
    offset += snprintf(output + offset, 8192 - offset,
        "# HELP kubelet_volumes_total Total volumes\n"
        "# TYPE kubelet_volumes_total gauge\n"
        "kubelet_volumes_total %lu\n",
        collector->current.volumes_total);
    
    offset += snprintf(output + offset, 8192 - offset,
        "# HELP kubelet_volumes_mounted Total mounted volumes\n"
        "# TYPE kubelet_volumes_mounted gauge\n"
        "kubelet_volumes_mounted %lu\n",
        collector->current.volumes_mounted);
    
    offset += snprintf(output + offset, 8192 - offset,
        "# HELP kubelet_uptime_seconds Uptime of kubelet process\n"
        "# TYPE kubelet_uptime_seconds gauge\n"
        "kubelet_uptime_seconds %ld\n",
        uptime);
    
    offset += snprintf(output + offset, 8192 - offset,
        "# HELP kubelet_memory_bytes Memory used by kubelet\n"
        "# TYPE kubelet_memory_bytes gauge\n"
        "kubelet_memory_bytes %lu\n",
        collector->current.memory_bytes);
    
    offset += snprintf(output + offset, 8192 - offset,
        "# HELP kubelet_cpu_millicores CPU millicores allocated\n"
        "# TYPE kubelet_cpu_millicores gauge\n"
        "kubelet_cpu_millicores %lu\n",
        collector->current.cpu_millicores);
    
    return output;
}

int metrics_export_to_file(metrics_collector_t* collector, const char* filepath) {
    if (!collector || !filepath) return -1;
    
    char* metrics_text = metrics_to_prometheus_text(collector);
    if (!metrics_text) return -1;
    
    FILE* f = fopen(filepath, "w");
    if (!f) {
        free(metrics_text);
        return -1;
    }
    
    int written = fprintf(f, "%s", metrics_text);
    fclose(f);
    
    free(metrics_text);
    
    return (written > 0) ? 0 : -1;
}

char* metrics_to_json(metrics_collector_t* collector) {
    if (!collector) return NULL;
    
    json_object* root = json_object_new_object();
    time_t uptime = time(NULL) - collector->process_start_time;
    
    json_object_object_add(root, "pods_total", 
        json_object_new_int64(collector->current.pods_total));
    json_object_object_add(root, "pods_running",
        json_object_new_int64(collector->current.pods_running));
    json_object_object_add(root, "pods_succeeded",
        json_object_new_int64(collector->current.pods_succeeded));
    json_object_object_add(root, "pods_failed",
        json_object_new_int64(collector->current.pods_failed));
    json_object_object_add(root, "pods_pending",
        json_object_new_int64(collector->current.pods_pending));
    
    json_object_object_add(root, "container_restarts_total",
        json_object_new_int64(collector->current.container_restarts_total));
    
    json_object_object_add(root, "probe_executions_total",
        json_object_new_int64(collector->current.probe_executions_total));
    json_object_object_add(root, "probe_successes_total",
        json_object_new_int64(collector->current.probe_successes_total));
    json_object_object_add(root, "probe_failures_total",
        json_object_new_int64(collector->current.probe_failures_total));
    
    json_object_object_add(root, "volumes_total",
        json_object_new_int64(collector->current.volumes_total));
    json_object_object_add(root, "volumes_mounted",
        json_object_new_int64(collector->current.volumes_mounted));
    
    json_object_object_add(root, "uptime_seconds",
        json_object_new_int64(uptime));
    json_object_object_add(root, "memory_bytes",
        json_object_new_int64(collector->current.memory_bytes));
    json_object_object_add(root, "cpu_millicores",
        json_object_new_int64(collector->current.cpu_millicores));
    
    char* json_str = strdup(json_object_to_json_string(root));
    json_object_put(root);
    
    return json_str;
}

// ============================================================================
// Diagnostics
// ============================================================================

char* metrics_summary(metrics_collector_t* collector, char* buf, int buf_len) {
    if (!collector || !buf) return NULL;
    
    time_t uptime = time(NULL) - collector->process_start_time;
    
    snprintf(buf, buf_len,
            "Pods: %lu total, %lu running, %lu failed | "
            "Probes: %lu executed, %lu passed | "
            "Restarts: %lu | "
            "Uptime: %ld seconds",
            collector->current.pods_total,
            collector->current.pods_running,
            collector->current.pods_failed,
            collector->current.probe_executions_total,
            collector->current.probe_successes_total,
            collector->current.container_restarts_total,
            uptime);
    
    return buf;
}

int metrics_health_check(metrics_collector_t* collector) {
    if (!collector) return 0;  // Unhealthy if no collector
    
    // Check for excessive errors
    if (collector->current.probe_executions_total > 0) {
        unsigned long fail_rate = (collector->current.probe_failures_total * 100) /
                                  collector->current.probe_executions_total;
        if (fail_rate > 50) {
            return 0;  // Too many probe failures
        }
    }
    
    // Check restart count
    if (collector->current.container_restarts_total > 10) {
        return 0;  // Too many restarts
    }
    
    return 1;  // Healthy
}
