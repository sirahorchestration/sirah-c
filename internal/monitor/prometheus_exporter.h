/*
 * prometheus_exporter.h
 *
 * Prometheus format metrics export
 */

#ifndef SIRAH_PROMETHEUS_EXPORTER_H
#define SIRAH_PROMETHEUS_EXPORTER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Prometheus metric
 */
typedef struct {
    char *name;              // Metric name
    char *help;              // Help text
    char *type;              // gauge, counter, histogram, summary
    
    // Label keys and values
    char **label_keys;
    char **label_values;
    uint32_t label_count;
    
    // Value
    double value;
    
} prometheus_metric_t;

/**
 * Prometheus exporter
 */
typedef struct {
    prometheus_metric_t *metrics;
    uint32_t metric_count;
    uint32_t max_metrics;
    
} prometheus_exporter_t;

/**
 * Create Prometheus exporter
 */
prometheus_exporter_t* prometheus_exporter_create(void);

/**
 * Free exporter
 */
void prometheus_exporter_free(prometheus_exporter_t *exporter);

/**
 * Add gauge metric
 */
bool prometheus_exporter_add_gauge(prometheus_exporter_t *exporter,
                                    const char *name,
                                    const char *help,
                                    double value,
                                    char **label_keys,
                                    char **label_values,
                                    uint32_t label_count);

/**
 * Add counter metric
 */
bool prometheus_exporter_add_counter(prometheus_exporter_t *exporter,
                                      const char *name,
                                      const char *help,
                                      uint64_t value,
                                      char **label_keys,
                                      char **label_values,
                                      uint32_t label_count);

/**
 * Export metrics in Prometheus format
 */
char* prometheus_exporter_render(prometheus_exporter_t *exporter);

/**
 * Clear all metrics
 */
void prometheus_exporter_clear(prometheus_exporter_t *exporter);

/**
 * Export instance metrics to Prometheus format
 */
bool prometheus_export_instance_metrics(const char *instance_id,
                                        const char *pod_name,
                                        const char *namespace,
                                        uint32_t cpu_percent,
                                        uint64_t memory_bytes,
                                        uint64_t memory_limit_bytes,
                                        char **out_metrics);

/**
 * Export node metrics to Prometheus format
 */
bool prometheus_export_node_metrics(const char *node_name,
                                    uint32_t cpu_percent,
                                    uint64_t memory_used,
                                    uint64_t memory_total,
                                    uint32_t pod_count,
                                    char **out_metrics);

#endif // SIRAH_PROMETHEUS_EXPORTER_H
