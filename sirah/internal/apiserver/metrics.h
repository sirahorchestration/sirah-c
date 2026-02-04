// internal/apiserver/metrics.h
// Prometheus-format metrics collection

#ifndef K8S_API_METRICS_H
#define K8S_API_METRICS_H

#include <time.h>

// Metric types
typedef enum {
    METRIC_COUNTER = 1,      // Monotonically increasing
    METRIC_GAUGE = 2,        // Can increase or decrease
    METRIC_HISTOGRAM = 3,    // Distribution of values
    METRIC_SUMMARY = 4       // Percentile summaries
} metric_type_t;

// Single metric data point
typedef struct {
    char* name;
    char* help_text;
    metric_type_t type;
    double value;
    char** labels;
    int label_count;
    time_t timestamp;
} metric_t;

// Metrics collector
typedef struct {
    metric_t** metrics;
    int metrics_count;
    int max_metrics;
    time_t collection_time;
} metrics_collector_t;

// ============ Metrics Creation/Management ============

metrics_collector_t* metrics_collector_new(int max_metrics);
void metrics_collector_free(metrics_collector_t* collector);

metric_t* metric_new(const char* name, const char* help_text, metric_type_t type);
void metric_free(metric_t* metric);

// ============ Recording Metrics ============

int metrics_record_counter(metrics_collector_t* collector, const char* name, 
                          const char* help_text, double value);

int metrics_record_gauge(metrics_collector_t* collector, const char* name,
                         const char* help_text, double value);

int metrics_record_histogram(metrics_collector_t* collector, const char* name,
                            const char* help_text, double value);

// ============ API Request Metrics ============

int metrics_api_request_total_inc(metrics_collector_t* collector);
int metrics_api_request_errors_inc(metrics_collector_t* collector);
int metrics_api_request_duration_observe(metrics_collector_t* collector, long duration_ms);

// ============ Pod Metrics ============

int metrics_pods_total_inc(metrics_collector_t* collector, const char* namespace);
int metrics_pods_total_dec(metrics_collector_t* collector, const char* namespace);
int metrics_pods_running_set(metrics_collector_t* collector, const char* namespace, int count);
int metrics_pods_pending_set(metrics_collector_t* collector, const char* namespace, int count);

// ============ Prometheus Format Export ============

int metrics_to_prometheus_format(metrics_collector_t* collector, 
                                 char* output_buffer, int buffer_size);

#endif
