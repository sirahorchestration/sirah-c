#include "metrics.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// Global metrics registry
static k8s_metrics_registry_t* g_metrics_registry = NULL;

// Standard histogram buckets (latency in milliseconds)
static double HISTOGRAM_BUCKETS[] = {1, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000};
static int NUM_HISTOGRAM_BUCKETS = 12;

// ============ Initialization ============

k8s_metrics_registry_t* k8s_metrics_registry_new() {
    k8s_metrics_registry_t* registry = calloc(1, sizeof(k8s_metrics_registry_t));
    if (!registry) return NULL;
    
    registry->num_samples = 0;
    registry->num_histograms = 0;
    registry->enabled = true;
    
    return registry;
}

void k8s_metrics_registry_free(k8s_metrics_registry_t* registry) {
    if (!registry) return;
    
    for (int i = 0; i < registry->num_samples; i++) {
        k8s_metric_sample_t* sample = registry->samples[i];
        if (sample) {
            free(sample->name);
            free(sample->help);
            for (int j = 0; j < sample->num_labels; j++) {
                free(sample->labels[j]);
            }
            free(sample->labels);
            free(sample);
        }
    }
    free(registry->samples);
    
    for (int i = 0; i < registry->num_histograms; i++) {
        k8s_histogram_metric_t* hist = registry->histograms[i];
        if (hist) {
            free(hist->name);
            free(hist->buckets);
            for (int j = 0; j < hist->num_labels; j++) {
                free(hist->labels[j]);
            }
            free(hist->labels);
            free(hist);
        }
    }
    free(registry->histograms);
    
    free(registry);
}

void k8s_metrics_registry_set_enabled(k8s_metrics_registry_t* registry, bool enabled) {
    if (registry) {
        registry->enabled = enabled;
    }
}

bool k8s_metrics_registry_is_enabled(k8s_metrics_registry_t* registry) {
    if (!registry) return false;
    return registry->enabled;
}

// ============ Helper Functions ============

static k8s_metric_sample_t* k8s_metric_sample_new(const char* name, const char* help, k8s_metric_type_t type) {
    k8s_metric_sample_t* sample = calloc(1, sizeof(k8s_metric_sample_t));
    if (!sample) return NULL;
    
    sample->name = malloc(strlen(name) + 1);
    sample->help = malloc(strlen(help) + 1);
    
    if (!sample->name || !sample->help) {
        free(sample->name);
        free(sample->help);
        free(sample);
        return NULL;
    }
    
    strcpy(sample->name, name);
    strcpy(sample->help, help);
    sample->type = type;
    sample->timestamp = time(NULL);
    
    return sample;
}

static int add_label_to_sample(k8s_metric_sample_t* sample, const char* label) {
    if (!sample || !label) return -1;
    
    char** new_labels = realloc(sample->labels, (sample->num_labels + 1) * sizeof(char*));
    if (!new_labels) return -1;
    
    new_labels[sample->num_labels] = malloc(strlen(label) + 1);
    if (!new_labels[sample->num_labels]) {
        free(new_labels);
        return -1;
    }
    
    strcpy(new_labels[sample->num_labels], label);
    sample->labels = new_labels;
    sample->num_labels++;
    return 0;
}

static k8s_metric_sample_t* k8s_metrics_find_or_create_sample(k8s_metrics_registry_t* registry,
                                                              const char* name,
                                                              const char* help,
                                                              k8s_metric_type_t type) {
    if (!registry || !name) return NULL;
    
    // Search for existing metric
    for (int i = 0; i < registry->num_samples; i++) {
        k8s_metric_sample_t* sample = registry->samples[i];
        if (sample && strcmp(sample->name, name) == 0) {
            return sample;
        }
    }
    
    // Create new metric
    k8s_metric_sample_t* sample = k8s_metric_sample_new(name, help ? help : "", type);
    if (!sample) return NULL;
    
    k8s_metric_sample_t** new_samples = realloc(registry->samples,
                                                 (registry->num_samples + 1) * sizeof(k8s_metric_sample_t*));
    if (!new_samples) {
        free(sample->name);
        free(sample->help);
        free(sample);
        return NULL;
    }
    
    new_samples[registry->num_samples] = sample;
    registry->samples = new_samples;
    registry->num_samples++;
    
    return sample;
}

// ============ Counter Operations ============

int k8s_metrics_counter_inc(k8s_metrics_registry_t* registry,
                             const char* name,
                             const char* help,
                             double amount) {
    if (!registry || !name || amount < 0) return -1;
    if (!registry->enabled) return 0;
    
    k8s_metric_sample_t* sample = k8s_metrics_find_or_create_sample(registry, name, help, K8S_METRIC_COUNTER);
    if (!sample) return -1;
    
    sample->value += amount;
    sample->timestamp = time(NULL);
    
    return 0;
}

int k8s_metrics_counter_inc_with_labels(k8s_metrics_registry_t* registry,
                                         const char* name,
                                         const char* help,
                                         double amount,
                                         const char** labels,
                                         int num_labels) {
    if (!registry || !name || amount < 0 || !labels || num_labels <= 0) return -1;
    if (!registry->enabled) return 0;
    
    // For labeled metrics, create a unique metric name
    char full_name[256];
    snprintf(full_name, sizeof(full_name), "%s{", name);
    
    for (int i = 0; i < num_labels; i++) {
        strncat(full_name, labels[i], sizeof(full_name) - strlen(full_name) - 1);
        if (i < num_labels - 1) {
            strncat(full_name, ",", sizeof(full_name) - strlen(full_name) - 1);
        }
    }
    strncat(full_name, "}", sizeof(full_name) - strlen(full_name) - 1);
    
    k8s_metric_sample_t* sample = k8s_metrics_find_or_create_sample(registry, full_name, help, K8S_METRIC_COUNTER);
    if (!sample) return -1;
    
    sample->value += amount;
    sample->timestamp = time(NULL);
    
    return 0;
}

// ============ Gauge Operations ============

int k8s_metrics_gauge_set(k8s_metrics_registry_t* registry,
                          const char* name,
                          const char* help,
                          double value) {
    if (!registry || !name) return -1;
    if (!registry->enabled) return 0;
    
    k8s_metric_sample_t* sample = k8s_metrics_find_or_create_sample(registry, name, help, K8S_METRIC_GAUGE);
    if (!sample) return -1;
    
    sample->value = value;
    sample->timestamp = time(NULL);
    
    return 0;
}

int k8s_metrics_gauge_set_with_labels(k8s_metrics_registry_t* registry,
                                       const char* name,
                                       const char* help,
                                       double value,
                                       const char** labels,
                                       int num_labels) {
    if (!registry || !name || !labels || num_labels <= 0) return -1;
    if (!registry->enabled) return 0;
    
    char full_name[256];
    snprintf(full_name, sizeof(full_name), "%s{", name);
    
    for (int i = 0; i < num_labels; i++) {
        strncat(full_name, labels[i], sizeof(full_name) - strlen(full_name) - 1);
        if (i < num_labels - 1) {
            strncat(full_name, ",", sizeof(full_name) - strlen(full_name) - 1);
        }
    }
    strncat(full_name, "}", sizeof(full_name) - strlen(full_name) - 1);
    
    k8s_metric_sample_t* sample = k8s_metrics_find_or_create_sample(registry, full_name, help, K8S_METRIC_GAUGE);
    if (!sample) return -1;
    
    sample->value = value;
    sample->timestamp = time(NULL);
    
    return 0;
}

// ============ Histogram Operations ============

int k8s_metrics_histogram_observe(k8s_metrics_registry_t* registry,
                                  const char* name,
                                  const char* help,
                                  double value) {
    if (!registry || !name || value < 0) return -1;
    if (!registry->enabled) return 0;
    
    // Find or create histogram
    k8s_histogram_metric_t* hist = NULL;
    for (int i = 0; i < registry->num_histograms; i++) {
        if (registry->histograms[i] && strcmp(registry->histograms[i]->name, name) == 0) {
            hist = registry->histograms[i];
            break;
        }
    }
    
    if (!hist) {
        hist = calloc(1, sizeof(k8s_histogram_metric_t));
        if (!hist) return -1;
        
        hist->name = malloc(strlen(name) + 1);
        if (!hist->name) {
            free(hist);
            return -1;
        }
        strcpy(hist->name, name);
        
        hist->num_buckets = NUM_HISTOGRAM_BUCKETS;
        hist->buckets = calloc(NUM_HISTOGRAM_BUCKETS, sizeof(k8s_histogram_bucket_t));
        if (!hist->buckets) {
            free(hist->name);
            free(hist);
            return -1;
        }
        
        for (int i = 0; i < NUM_HISTOGRAM_BUCKETS; i++) {
            hist->buckets[i].le = HISTOGRAM_BUCKETS[i];
            hist->buckets[i].count = 0;
        }
        
        k8s_histogram_metric_t** new_hists = realloc(registry->histograms,
                                                      (registry->num_histograms + 1) * sizeof(k8s_histogram_metric_t*));
        if (!new_hists) {
            free(hist->buckets);
            free(hist->name);
            free(hist);
            return -1;
        }
        
        new_hists[registry->num_histograms] = hist;
        registry->histograms = new_hists;
        registry->num_histograms++;
    }
    
    // Update histogram
    hist->sum += value;
    hist->count++;
    
    for (int i = 0; i < hist->num_buckets; i++) {
        if (value <= hist->buckets[i].le) {
            hist->buckets[i].count++;
        }
    }
    
    return 0;
}

int k8s_metrics_histogram_observe_with_labels(k8s_metrics_registry_t* registry,
                                              const char* name,
                                              const char* help,
                                              double value,
                                              const char** labels,
                                              int num_labels) {
    if (!registry || !name || value < 0 || !labels || num_labels <= 0) return -1;
    if (!registry->enabled) return 0;
    
    char full_name[256];
    snprintf(full_name, sizeof(full_name), "%s{", name);
    
    for (int i = 0; i < num_labels; i++) {
        strncat(full_name, labels[i], sizeof(full_name) - strlen(full_name) - 1);
        if (i < num_labels - 1) {
            strncat(full_name, ",", sizeof(full_name) - strlen(full_name) - 1);
        }
    }
    strncat(full_name, "}", sizeof(full_name) - strlen(full_name) - 1);
    
    return k8s_metrics_histogram_observe(registry, full_name, help, value);
}

// ============ Serialization ============

char* k8s_metrics_to_prometheus_format(k8s_metrics_registry_t* registry) {
    if (!registry) return NULL;
    
    size_t buffer_size = 4096;
    char* buffer = malloc(buffer_size);
    if (!buffer) return NULL;
    
    int offset = 0;
    
    // Write counters and gauges
    for (int i = 0; i < registry->num_samples; i++) {
        k8s_metric_sample_t* sample = registry->samples[i];
        if (!sample) continue;
        
        // Help line
        int written = snprintf(buffer + offset, buffer_size - offset,
                               "# HELP %s %s\n", sample->name, sample->help);
        if (written < 0) break;
        offset += written;
        
        // Type line
        const char* type_str = sample->type == K8S_METRIC_COUNTER ? "counter" : "gauge";
        written = snprintf(buffer + offset, buffer_size - offset,
                           "# TYPE %s %s\n", sample->name, type_str);
        if (written < 0) break;
        offset += written;
        
        // Value line
        written = snprintf(buffer + offset, buffer_size - offset,
                           "%s %.6f\n", sample->name, sample->value);
        if (written < 0) break;
        offset += written;
    }
    
    // Write histograms
    for (int i = 0; i < registry->num_histograms; i++) {
        k8s_histogram_metric_t* hist = registry->histograms[i];
        if (!hist) continue;
        
        // Help and type
        int written = snprintf(buffer + offset, buffer_size - offset,
                               "# HELP %s Histogram metric\n", hist->name);
        if (written < 0) break;
        offset += written;
        
        written = snprintf(buffer + offset, buffer_size - offset,
                           "# TYPE %s histogram\n", hist->name);
        if (written < 0) break;
        offset += written;
        
        // Buckets
        for (int j = 0; j < hist->num_buckets; j++) {
            written = snprintf(buffer + offset, buffer_size - offset,
                               "%s_bucket{le=\"%.1f\"} %lu\n",
                               hist->name, hist->buckets[j].le, hist->buckets[j].count);
            if (written < 0) break;
            offset += written;
        }
        
        // +Inf bucket
        int total_count = hist->num_buckets > 0 ? hist->buckets[hist->num_buckets - 1].count : 0;
        written = snprintf(buffer + offset, buffer_size - offset,
                           "%s_bucket{le=\"+Inf\"} %lu\n",
                           hist->name, total_count);
        if (written < 0) break;
        offset += written;
        
        // Sum and count
        written = snprintf(buffer + offset, buffer_size - offset,
                           "%s_sum %.6f\n%s_count %lu\n",
                           hist->name, hist->sum, hist->name, hist->count);
        if (written < 0) break;
        offset += written;
    }
    
    return buffer;
}

char* k8s_metrics_to_json(k8s_metrics_registry_t* registry) {
    if (!registry) return NULL;
    
    // TODO: Implement JSON serialization
    // For now, return empty JSON object
    char* json = malloc(3);
    if (json) strcpy(json, "{}");
    return json;
}

// ============ API Metrics ============

int k8s_metrics_record_api_request(k8s_metrics_registry_t* registry,
                                    const char* method,
                                    const char* path,
                                    int status_code,
                                    double latency_ms) {
    if (!registry || !method || !path) return -1;
    
    // Record request count
    char counter_name[64];
    snprintf(counter_name, sizeof(counter_name), "apiserver_request_count");
    k8s_metrics_counter_inc(registry, counter_name, "API server request count", 1.0);
    
    // Record latency histogram
    char hist_name[64];
    snprintf(hist_name, sizeof(hist_name), "apiserver_request_latency_ms");
    k8s_metrics_histogram_observe(registry, hist_name, "API request latency", latency_ms);
    
    // Record status code
    char status_gauge[64];
    snprintf(status_gauge, sizeof(status_gauge), "apiserver_request_status_%d", status_code);
    k8s_metrics_counter_inc(registry, status_gauge, "Request status code count", 1.0);
    
    return 0;
}

int k8s_metrics_record_etcd_operation(k8s_metrics_registry_t* registry,
                                      const char* operation,
                                      bool success,
                                      double latency_ms) {
    if (!registry || !operation) return -1;
    
    // Record operation count
    char counter_name[64];
    const char* status = success ? "success" : "failure";
    snprintf(counter_name, sizeof(counter_name), "etcd_operation_%s", status);
    k8s_metrics_counter_inc(registry, counter_name, "etcd operation count", 1.0);
    
    // Record latency
    char hist_name[64];
    snprintf(hist_name, sizeof(hist_name), "etcd_latency_ms");
    k8s_metrics_histogram_observe(registry, hist_name, "etcd operation latency", latency_ms);
    
    return 0;
}

int k8s_metrics_record_resource_count(k8s_metrics_registry_t* registry,
                                      const char* resource_type,
                                      const char* namespace,
                                      int count) {
    if (!registry || !resource_type || !namespace) return -1;
    
    char gauge_name[128];
    snprintf(gauge_name, sizeof(gauge_name), "kubernetes_resource_count{resource=\"%s\",namespace=\"%s\"}",
             resource_type, namespace);
    
    k8s_metrics_gauge_set(registry, gauge_name, "Resource count", (double)count);
    
    return 0;
}

int k8s_metrics_record_pod_event(k8s_metrics_registry_t* registry,
                                  const char* event_type,
                                  const char* namespace) {
    if (!registry || !event_type || !namespace) return -1;
    
    char counter_name[128];
    snprintf(counter_name, sizeof(counter_name), "pod_event{type=\"%s\",namespace=\"%s\"}",
             event_type, namespace);
    
    k8s_metrics_counter_inc(registry, counter_name, "Pod lifecycle events", 1.0);
    
    return 0;
}

// ============ Global Registry ============

k8s_metrics_registry_t* k8s_metrics_registry_global() {
    if (!g_metrics_registry) {
        g_metrics_registry = k8s_metrics_registry_new();
    }
    return g_metrics_registry;
}
