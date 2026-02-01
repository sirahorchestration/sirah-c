#ifndef K8S_PROMETHEUS_H
#define K8S_PROMETHEUS_H

#include "metrics.h"
#include <stdbool.h>

// Prometheus exporter configuration
typedef struct {
    int port;
    char* endpoint;  // e.g., "/metrics"
    bool tls_enabled;
    char* tls_cert_file;
    char* tls_key_file;
} k8s_prometheus_config_t;

// Prometheus HTTP endpoint
typedef struct {
    k8s_prometheus_config_t* config;
    k8s_metrics_registry_t* registry;
    bool running;
} k8s_prometheus_exporter_t;

// ============ Configuration ============

k8s_prometheus_config_t* k8s_prometheus_config_new(int port, const char* endpoint);
void k8s_prometheus_config_free(k8s_prometheus_config_t* config);

int k8s_prometheus_config_set_tls(k8s_prometheus_config_t* config,
                                   const char* cert_file,
                                   const char* key_file);

// ============ Exporter Management ============

k8s_prometheus_exporter_t* k8s_prometheus_exporter_new(k8s_prometheus_config_t* config,
                                                       k8s_metrics_registry_t* registry);

void k8s_prometheus_exporter_free(k8s_prometheus_exporter_t* exporter);

// Start HTTP server for metrics endpoint
int k8s_prometheus_exporter_start(k8s_prometheus_exporter_t* exporter);
int k8s_prometheus_exporter_stop(k8s_prometheus_exporter_t* exporter);

// Handle HTTP request to /metrics
char* k8s_prometheus_handler(k8s_prometheus_exporter_t* exporter,
                              const char* path,
                              const char* query_string);

// ============ Global Exporter ============

k8s_prometheus_exporter_t* k8s_prometheus_exporter_global();

#endif
