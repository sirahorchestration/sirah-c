// internal/apiserver/metrics.c
// Prometheus-format metrics collection implementation

#include "metrics.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// ============ Metrics Creation ============

metrics_collector_t* metrics_collector_new(int max_metrics) {
    if (max_metrics <= 0) max_metrics = 100;
    
    metrics_collector_t* collector = (metrics_collector_t*)malloc(sizeof(metrics_collector_t));
    if (!collector) return NULL;
    
    collector->metrics = (metric_t**)malloc(sizeof(metric_t*) * max_metrics);
    if (!collector->metrics) {
        free(collector);
        return NULL;
    }
    
    collector->metrics_count = 0;
    collector->max_metrics = max_metrics;
    collector->collection_time = time(NULL);
    
    return collector;
}

void metrics_collector_free(metrics_collector_t* collector) {
    if (!collector) return;
    
    for (int i = 0; i < collector->metrics_count; i++) {
        metric_free(collector->metrics[i]);
    }
    free(collector->metrics);
    free(collector);
}

metric_t* metric_new(const char* name, const char* help_text, metric_type_t type) {
    if (!name) return NULL;
    
    metric_t* metric = (metric_t*)malloc(sizeof(metric_t));
    if (!metric) return NULL;
    
    metric->name = (char*)malloc(strlen(name) + 1);
    strcpy(metric->name, name);
    
    metric->help_text = (char*)malloc(strlen(help_text ? help_text : "") + 1);
    strcpy(metric->help_text, help_text ? help_text : "");
    
    metric->type = type;
    metric->value = 0.0;
    metric->labels = NULL;
    metric->label_count = 0;
    metric->timestamp = time(NULL);
    
    return metric;
}

void metric_free(metric_t* metric) {
    if (!metric) return;
    free(metric->name);
    free(metric->help_text);
    if (metric->labels) {
        for (int i = 0; i < metric->label_count; i++) {
            free(metric->labels[i]);
        }
        free(metric->labels);
    }
    free(metric);
}

// ============ Recording Metrics ============

int metrics_record_counter(metrics_collector_t* collector, const char* name,
                          const char* help_text, double value) {
    if (!collector || !name || collector->metrics_count >= collector->max_metrics) return -1;
    
    // Find or create metric
    for (int i = 0; i < collector->metrics_count; i++) {
        if (strcmp(collector->metrics[i]->name, name) == 0) {
            collector->metrics[i]->value += value;  // Counters only increase
            collector->metrics[i]->timestamp = time(NULL);
            return 0;
        }
    }
    
    // Create new counter
    metric_t* metric = metric_new(name, help_text, METRIC_COUNTER);
    if (!metric) return -1;
    metric->value = value;
    
    collector->metrics[collector->metrics_count++] = metric;
    return 0;
}

int metrics_record_gauge(metrics_collector_t* collector, const char* name,
                        const char* help_text, double value) {
    if (!collector || !name || collector->metrics_count >= collector->max_metrics) return -1;
    
    // Find or create metric
    for (int i = 0; i < collector->metrics_count; i++) {
        if (strcmp(collector->metrics[i]->name, name) == 0) {
            collector->metrics[i]->value = value;  // Gauges can be set
            collector->metrics[i]->timestamp = time(NULL);
            return 0;
        }
    }
    
    // Create new gauge
    metric_t* metric = metric_new(name, help_text, METRIC_GAUGE);
    if (!metric) return -1;
    metric->value = value;
    
    collector->metrics[collector->metrics_count++] = metric;
    return 0;
}

int metrics_record_histogram(metrics_collector_t* collector, const char* name,
                            const char* help_text, double value) {
    if (!collector || !name || collector->metrics_count >= collector->max_metrics) return -1;
    
    // For simplicity, store as gauge (buckets would need more structure)
    return metrics_record_gauge(collector, name, help_text, value);
}

// ============ API Request Metrics ============

int metrics_api_request_total_inc(metrics_collector_t* collector) {
    return metrics_record_counter(collector, "sirah_api_requests_total",
                                "Total API requests received", 1.0);
}

int metrics_api_request_errors_inc(metrics_collector_t* collector) {
    return metrics_record_counter(collector, "sirah_api_request_errors_total",
                                "Total API request errors", 1.0);
}

int metrics_api_request_duration_observe(metrics_collector_t* collector, long duration_ms) {
    char metric_name[128];
    snprintf(metric_name, sizeof(metric_name), "sirah_api_request_duration_ms");
    return metrics_record_histogram(collector, metric_name,
                                  "API request duration in milliseconds", (double)duration_ms);
}

// ============ Pod Metrics ============

int metrics_pods_total_inc(metrics_collector_t* collector, const char* namespace) {
    char metric_name[128];
    snprintf(metric_name, sizeof(metric_name), "sirah_pods_total{namespace=\"%s\"}",
            namespace ? namespace : "default");
    return metrics_record_counter(collector, metric_name,
                                "Total pods created", 1.0);
}

int metrics_pods_total_dec(metrics_collector_t* collector, const char* namespace) {
    char metric_name[128];
    snprintf(metric_name, sizeof(metric_name), "sirah_pods_deleted_total{namespace=\"%s\"}",
            namespace ? namespace : "default");
    return metrics_record_counter(collector, metric_name,
                                "Total pods deleted", 1.0);
}

int metrics_pods_running_set(metrics_collector_t* collector, const char* namespace, int count) {
    char metric_name[128];
    snprintf(metric_name, sizeof(metric_name), "sirah_pods_running{namespace=\"%s\"}",
            namespace ? namespace : "default");
    return metrics_record_gauge(collector, metric_name,
                               "Currently running pods", (double)count);
}

int metrics_pods_pending_set(metrics_collector_t* collector, const char* namespace, int count) {
    char metric_name[128];
    snprintf(metric_name, sizeof(metric_name), "sirah_pods_pending{namespace=\"%s\"}",
            namespace ? namespace : "default");
    return metrics_record_gauge(collector, metric_name,
                               "Currently pending pods", (double)count);
}

// ============ Prometheus Format Export ============

int metrics_to_prometheus_format(metrics_collector_t* collector,
                                char* output_buffer, int buffer_size) {
    if (!collector || !output_buffer) return -1;
    
    int offset = 0;
    
    // Header
    offset += snprintf(output_buffer + offset, buffer_size - offset,
                      "# HELP process_uptime_seconds Process uptime in seconds\n");
    offset += snprintf(output_buffer + offset, buffer_size - offset,
                      "# TYPE process_uptime_seconds gauge\n");
    
    // Export all metrics
    for (int i = 0; i < collector->metrics_count && offset < buffer_size - 256; i++) {
        metric_t* m = collector->metrics[i];
        
        // Prometheus comment lines
        if (m->help_text && strlen(m->help_text) > 0) {
            offset += snprintf(output_buffer + offset, buffer_size - offset,
                             "# HELP %s %s\n", m->name, m->help_text);
        }
        
        // Type line
        const char* type_str = "gauge";
        switch (m->type) {
            case METRIC_COUNTER: type_str = "counter"; break;
            case METRIC_GAUGE: type_str = "gauge"; break;
            case METRIC_HISTOGRAM: type_str = "histogram"; break;
            case METRIC_SUMMARY: type_str = "summary"; break;
        }
        
        offset += snprintf(output_buffer + offset, buffer_size - offset,
                         "# TYPE %s %s\n", m->name, type_str);
        
        // Metric value
        offset += snprintf(output_buffer + offset, buffer_size - offset,
                         "%s %f %ld\n",
                         m->name,
                         m->value,
                         (long)m->timestamp * 1000);  // Convert to milliseconds
    }
    
    // Add a simple uptime metric (seconds since epoch)
    time_t now = time(NULL);
    offset += snprintf(output_buffer + offset, buffer_size - offset,
                     "process_uptime_seconds %ld\n", now);
    
    return offset < buffer_size ? 0 : -1;
}
