/*
 * prometheus_exporter.c
 *
 * Prometheus format metrics export implementation
 */

#include "prometheus_exporter.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * Create exporter
 */
prometheus_exporter_t* prometheus_exporter_create(void) {
    prometheus_exporter_t *exporter = malloc(sizeof(prometheus_exporter_t));
    if (!exporter) return NULL;
    
    memset(exporter, 0, sizeof(prometheus_exporter_t));
    
    exporter->max_metrics = 100;
    exporter->metrics = calloc(100, sizeof(prometheus_metric_t));
    if (!exporter->metrics) {
        free(exporter);
        return NULL;
    }
    
    return exporter;
}

/**
 * Free exporter
 */
void prometheus_exporter_free(prometheus_exporter_t *exporter) {
    if (!exporter) return;
    
    for (uint32_t i = 0; i < exporter->metric_count; i++) {
        free(exporter->metrics[i].name);
        free(exporter->metrics[i].help);
        free(exporter->metrics[i].type);
        for (uint32_t j = 0; j < exporter->metrics[i].label_count; j++) {
            free(exporter->metrics[i].label_keys[j]);
            free(exporter->metrics[i].label_values[j]);
        }
        free(exporter->metrics[i].label_keys);
        free(exporter->metrics[i].label_values);
    }
    free(exporter->metrics);
    free(exporter);
}

/**
 * Add gauge metric
 */
bool prometheus_exporter_add_gauge(prometheus_exporter_t *exporter,
                                    const char *name,
                                    const char *help,
                                    double value,
                                    char **label_keys,
                                    char **label_values,
                                    uint32_t label_count) {
    if (!exporter || !name || !help) return false;
    
    if (exporter->metric_count >= exporter->max_metrics) {
        // Expand metrics array
        prometheus_metric_t *new_metrics = realloc(exporter->metrics,
                                                   sizeof(prometheus_metric_t) * (exporter->max_metrics * 2));
        if (!new_metrics) return false;
        
        exporter->metrics = new_metrics;
        exporter->max_metrics *= 2;
    }
    
    prometheus_metric_t *metric = &exporter->metrics[exporter->metric_count];
    
    metric->name = strdup(name);
    metric->help = strdup(help);
    metric->type = strdup("gauge");
    metric->value = value;
    
    if (label_count > 0 && label_keys && label_values) {
        metric->label_keys = malloc(sizeof(char*) * label_count);
        metric->label_values = malloc(sizeof(char*) * label_count);
        
        for (uint32_t i = 0; i < label_count; i++) {
            metric->label_keys[i] = strdup(label_keys[i]);
            metric->label_values[i] = strdup(label_values[i]);
        }
        metric->label_count = label_count;
    }
    
    exporter->metric_count++;
    return true;
}

/**
 * Add counter metric
 */
bool prometheus_exporter_add_counter(prometheus_exporter_t *exporter,
                                      const char *name,
                                      const char *help,
                                      uint64_t value,
                                      char **label_keys,
                                      char **label_values,
                                      uint32_t label_count) {
    return prometheus_exporter_add_gauge(exporter, name, help, (double)value, help,
                                        label_keys, label_values, label_count);
}

/**
 * Escape label value for Prometheus format
 */
static char* escape_label_value(const char *value) {
    if (!value) return strdup("");
    
    size_t len = strlen(value);
    char *escaped = malloc(len * 2 + 1);
    if (!escaped) return NULL;
    
    int j = 0;
    for (size_t i = 0; i < len; i++) {
        if (value[i] == '"') {
            escaped[j++] = '\\';
            escaped[j++] = '"';
        } else if (value[i] == '\n') {
            escaped[j++] = '\\';
            escaped[j++] = 'n';
        } else if (value[i] == '\\') {
            escaped[j++] = '\\';
            escaped[j++] = '\\';
        } else {
            escaped[j++] = value[i];
        }
    }
    escaped[j] = '\0';
    
    return escaped;
}

/**
 * Render metrics in Prometheus format
 */
char* prometheus_exporter_render(prometheus_exporter_t *exporter) {
    if (!exporter) return NULL;
    
    // Calculate needed buffer size
    size_t buffer_size = 1024;  // Start with 1KB
    char *output = malloc(buffer_size);
    if (!output) return NULL;
    
    size_t offset = 0;
    
    for (uint32_t i = 0; i < exporter->metric_count; i++) {
        prometheus_metric_t *metric = &exporter->metrics[i];
        
        // Write HELP and TYPE lines
        size_t needed = snprintf(NULL, 0,
                                "# HELP %s %s\n# TYPE %s %s\n",
                                metric->name, metric->help,
                                metric->name, metric->type) + 1;
        
        while (offset + needed > buffer_size) {
            buffer_size *= 2;
            char *new_output = realloc(output, buffer_size);
            if (!new_output) {
                free(output);
                return NULL;
            }
            output = new_output;
        }
        
        offset += snprintf(output + offset, buffer_size - offset,
                          "# HELP %s %s\n# TYPE %s %s\n",
                          metric->name, metric->help,
                          metric->name, metric->type);
        
        // Write metric line with labels
        if (metric->label_count > 0) {
            offset += snprintf(output + offset, buffer_size - offset, "%s{", metric->name);
            
            for (uint32_t j = 0; j < metric->label_count; j++) {
                char *escaped = escape_label_value(metric->label_values[j]);
                offset += snprintf(output + offset, buffer_size - offset,
                                  "%s%s=\"%s\"",
                                  (j > 0) ? "," : "",
                                  metric->label_keys[j],
                                  escaped);
                free(escaped);
            }
            offset += snprintf(output + offset, buffer_size - offset, "} %lf\n", metric->value);
        } else {
            offset += snprintf(output + offset, buffer_size - offset,
                              "%s %lf\n", metric->name, metric->value);
        }
    }
    
    return output;
}

/**
 * Clear all metrics
 */
void prometheus_exporter_clear(prometheus_exporter_t *exporter) {
    if (!exporter) return;
    
    for (uint32_t i = 0; i < exporter->metric_count; i++) {
        free(exporter->metrics[i].name);
        free(exporter->metrics[i].help);
        free(exporter->metrics[i].type);
        for (uint32_t j = 0; j < exporter->metrics[i].label_count; j++) {
            free(exporter->metrics[i].label_keys[j]);
            free(exporter->metrics[i].label_values[j]);
        }
        free(exporter->metrics[i].label_keys);
        free(exporter->metrics[i].label_values);
    }
    
    exporter->metric_count = 0;
}

/**
 * Export instance metrics to Prometheus format
 */
bool prometheus_export_instance_metrics(const char *instance_id,
                                        const char *pod_name,
                                        const char *namespace,
                                        uint32_t cpu_percent,
                                        uint64_t memory_bytes,
                                        uint64_t memory_limit_bytes,
                                        char **out_metrics) {
    if (!instance_id || !pod_name || !namespace || !out_metrics) return false;
    
    prometheus_exporter_t *exporter = prometheus_exporter_create();
    if (!exporter) return false;
    
    // Add metrics
    char *cpu_labels[] = {"pod", "namespace"};
    char *cpu_values[] = {(char*)pod_name, (char*)namespace};
    prometheus_exporter_add_gauge(exporter, "pod_cpu_usage_percent",
                                  "CPU usage percentage",
                                  cpu_percent, cpu_labels, cpu_values, 2);
    
    char *mem_labels[] = {"pod", "namespace"};
    char *mem_values[] = {(char*)pod_name, (char*)namespace};
    prometheus_exporter_add_gauge(exporter, "pod_memory_usage_bytes",
                                  "Memory usage in bytes",
                                  (double)memory_bytes, mem_labels, mem_values, 2);
    
    prometheus_exporter_add_gauge(exporter, "pod_memory_limit_bytes",
                                  "Memory limit in bytes",
                                  (double)memory_limit_bytes, mem_labels, mem_values, 2);
    
    uint32_t mem_percent = (memory_limit_bytes > 0) ?
                           (uint32_t)((memory_bytes * 100) / memory_limit_bytes) : 0;
    prometheus_exporter_add_gauge(exporter, "pod_memory_usage_percent",
                                  "Memory usage percentage",
                                  mem_percent, mem_labels, mem_values, 2);
    
    *out_metrics = prometheus_exporter_render(exporter);
    prometheus_exporter_free(exporter);
    
    return true;
}

/**
 * Export node metrics to Prometheus format
 */
bool prometheus_export_node_metrics(const char *node_name,
                                    uint32_t cpu_percent,
                                    uint64_t memory_used,
                                    uint64_t memory_total,
                                    uint32_t pod_count,
                                    char **out_metrics) {
    if (!node_name || !out_metrics) return false;
    
    prometheus_exporter_t *exporter = prometheus_exporter_create();
    if (!exporter) return false;
    
    // Add metrics
    char *node_labels[] = {"node"};
    char *node_values[] = {(char*)node_name};
    
    prometheus_exporter_add_gauge(exporter, "node_cpu_usage_percent",
                                  "Node CPU usage percentage",
                                  cpu_percent, node_labels, node_values, 1);
    
    prometheus_exporter_add_gauge(exporter, "node_memory_usage_bytes",
                                  "Node memory usage in bytes",
                                  (double)memory_used, node_labels, node_values, 1);
    
    prometheus_exporter_add_gauge(exporter, "node_memory_total_bytes",
                                  "Node total memory in bytes",
                                  (double)memory_total, node_labels, node_values, 1);
    
    uint32_t mem_percent = (memory_total > 0) ?
                           (uint32_t)((memory_used * 100) / memory_total) : 0;
    prometheus_exporter_add_gauge(exporter, "node_memory_usage_percent",
                                  "Node memory usage percentage",
                                  mem_percent, node_labels, node_values, 1);
    
    prometheus_exporter_add_gauge(exporter, "node_pod_count",
                                  "Number of pods on node",
                                  pod_count, node_labels, node_values, 1);
    
    *out_metrics = prometheus_exporter_render(exporter);
    prometheus_exporter_free(exporter);
    
    return true;
}
