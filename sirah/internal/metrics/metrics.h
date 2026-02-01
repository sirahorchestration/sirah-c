#ifndef K8S_METRICS_H
#define K8S_METRICS_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Metric types
typedef enum {
    K8S_METRIC_COUNTER = 0,
    K8S_METRIC_GAUGE = 1,
    K8S_METRIC_HISTOGRAM = 2,
    K8S_METRIC_SUMMARY = 3
} k8s_metric_type_t;

// Single metric data point
typedef struct {
    char* name;
    char* help;
    k8s_metric_type_t type;
    double value;
    char** labels;  // "key=value" pairs
    int num_labels;
    int64_t timestamp;
} k8s_metric_sample_t;

// Histogram bucket
typedef struct {
    double le;      // Less than or equal to (bucket boundary)
    uint64_t count;
} k8s_histogram_bucket_t;

// Histogram metric
typedef struct {
    char* name;
    double sum;
    uint64_t count;
    k8s_histogram_bucket_t* buckets;
    int num_buckets;
    char** labels;
    int num_labels;
} k8s_histogram_metric_t;

// Metrics collector/registry
typedef struct {
    k8s_metric_sample_t** samples;
    int num_samples;
    k8s_histogram_metric_t** histograms;
    int num_histograms;
    bool enabled;
} k8s_metrics_registry_t;

// ============ Registry Management ============

k8s_metrics_registry_t* k8s_metrics_registry_new();
void k8s_metrics_registry_free(k8s_metrics_registry_t* registry);

void k8s_metrics_registry_set_enabled(k8s_metrics_registry_t* registry, bool enabled);
bool k8s_metrics_registry_is_enabled(k8s_metrics_registry_t* registry);

// ============ Metric Recording ============

// Counter: monotonically increasing value
int k8s_metrics_counter_inc(k8s_metrics_registry_t* registry,
                             const char* name,
                             const char* help,
                             double amount);

int k8s_metrics_counter_inc_with_labels(k8s_metrics_registry_t* registry,
                                         const char* name,
                                         const char* help,
                                         double amount,
                                         const char** labels,
                                         int num_labels);

// Gauge: value that can increase or decrease
int k8s_metrics_gauge_set(k8s_metrics_registry_t* registry,
                          const char* name,
                          const char* help,
                          double value);

int k8s_metrics_gauge_set_with_labels(k8s_metrics_registry_t* registry,
                                       const char* name,
                                       const char* help,
                                       double value,
                                       const char** labels,
                                       int num_labels);

// Histogram: distribution of values
int k8s_metrics_histogram_observe(k8s_metrics_registry_t* registry,
                                  const char* name,
                                  const char* help,
                                  double value);

int k8s_metrics_histogram_observe_with_labels(k8s_metrics_registry_t* registry,
                                              const char* name,
                                              const char* help,
                                              double value,
                                              const char** labels,
                                              int num_labels);

// ============ Serialization (Prometheus Format) ============

// Get all metrics in Prometheus text format
char* k8s_metrics_to_prometheus_format(k8s_metrics_registry_t* registry);

// Get metrics as JSON
char* k8s_metrics_to_json(k8s_metrics_registry_t* registry);

// ============ Predefined API Metrics ============

// Track API request count and latency
int k8s_metrics_record_api_request(k8s_metrics_registry_t* registry,
                                    const char* method,
                                    const char* path,
                                    int status_code,
                                    double latency_ms);

// Track etcd operations
int k8s_metrics_record_etcd_operation(k8s_metrics_registry_t* registry,
                                      const char* operation,
                                      bool success,
                                      double latency_ms);

// Track resource counts
int k8s_metrics_record_resource_count(k8s_metrics_registry_t* registry,
                                      const char* resource_type,
                                      const char* namespace,
                                      int count);

// Track pod lifecycle events
int k8s_metrics_record_pod_event(k8s_metrics_registry_t* registry,
                                  const char* event_type,
                                  const char* namespace);

// ============ Global Registry ============

k8s_metrics_registry_t* k8s_metrics_registry_global();

#endif
