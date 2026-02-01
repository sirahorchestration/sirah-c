#include "prometheus.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Global exporter instance
static k8s_prometheus_exporter_t* g_prometheus_exporter = NULL;

// ============ Configuration ============

k8s_prometheus_config_t* k8s_prometheus_config_new(int port, const char* endpoint) {
    if (port <= 0 || port > 65535 || !endpoint) return NULL;
    
    k8s_prometheus_config_t* config = calloc(1, sizeof(k8s_prometheus_config_t));
    if (!config) return NULL;
    
    config->port = port;
    config->endpoint = malloc(strlen(endpoint) + 1);
    config->tls_enabled = false;
    
    if (!config->endpoint) {
        free(config);
        return NULL;
    }
    
    strcpy(config->endpoint, endpoint);
    
    return config;
}

void k8s_prometheus_config_free(k8s_prometheus_config_t* config) {
    if (!config) return;
    
    free(config->endpoint);
    free(config->tls_cert_file);
    free(config->tls_key_file);
    free(config);
}

int k8s_prometheus_config_set_tls(k8s_prometheus_config_t* config,
                                   const char* cert_file,
                                   const char* key_file) {
    if (!config || !cert_file || !key_file) return -1;
    
    config->tls_cert_file = malloc(strlen(cert_file) + 1);
    config->tls_key_file = malloc(strlen(key_file) + 1);
    
    if (!config->tls_cert_file || !config->tls_key_file) {
        free(config->tls_cert_file);
        free(config->tls_key_file);
        return -1;
    }
    
    strcpy(config->tls_cert_file, cert_file);
    strcpy(config->tls_key_file, key_file);
    config->tls_enabled = true;
    
    return 0;
}

// ============ Exporter Management ============

k8s_prometheus_exporter_t* k8s_prometheus_exporter_new(k8s_prometheus_config_t* config,
                                                       k8s_metrics_registry_t* registry) {
    if (!config || !registry) return NULL;
    
    k8s_prometheus_exporter_t* exporter = calloc(1, sizeof(k8s_prometheus_exporter_t));
    if (!exporter) return NULL;
    
    exporter->config = config;
    exporter->registry = registry;
    exporter->running = false;
    
    return exporter;
}

void k8s_prometheus_exporter_free(k8s_prometheus_exporter_t* exporter) {
    if (!exporter) return;
    
    if (exporter->running) {
        k8s_prometheus_exporter_stop(exporter);
    }
    
    if (exporter->config) {
        k8s_prometheus_config_free(exporter->config);
    }
    
    free(exporter);
}

// ============ Server Operations ============

int k8s_prometheus_exporter_start(k8s_prometheus_exporter_t* exporter) {
    if (!exporter || !exporter->config) return -1;
    
    if (exporter->running) {
        return -1;  // Already running
    }
    
    // TODO: Start HTTP server on exporter->config->port
    // Register handler on exporter->config->endpoint
    // For now, mark as running
    exporter->running = true;
    
    return 0;
}

int k8s_prometheus_exporter_stop(k8s_prometheus_exporter_t* exporter) {
    if (!exporter) return -1;
    
    if (!exporter->running) {
        return -1;  // Not running
    }
    
    // TODO: Stop HTTP server
    exporter->running = false;
    
    return 0;
}

// ============ Request Handler ============

char* k8s_prometheus_handler(k8s_prometheus_exporter_t* exporter,
                              const char* path,
                              const char* query_string) {
    if (!exporter || !exporter->registry || !path) return NULL;
    
    // Check if path matches metrics endpoint
    if (strcmp(path, exporter->config->endpoint) != 0) {
        return NULL;  // Not our endpoint
    }
    
    // Get metrics in Prometheus format
    char* metrics = k8s_metrics_to_prometheus_format(exporter->registry);
    
    // Build HTTP response
    size_t response_size = strlen(metrics) + 256;
    char* response = malloc(response_size);
    
    if (response) {
        snprintf(response, response_size,
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/plain; version=0.0.4\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: close\r\n"
                 "\r\n"
                 "%s",
                 strlen(metrics), metrics);
    }
    
    free(metrics);
    return response;
}

// ============ Global Exporter ============

k8s_prometheus_exporter_t* k8s_prometheus_exporter_global() {
    if (!g_prometheus_exporter) {
        k8s_prometheus_config_t* config = k8s_prometheus_config_new(8080, "/metrics");
        if (!config) return NULL;
        
        k8s_metrics_registry_t* registry = k8s_metrics_registry_global();
        if (!registry) {
            k8s_prometheus_config_free(config);
            return NULL;
        }
        
        g_prometheus_exporter = k8s_prometheus_exporter_new(config, registry);
    }
    
    return g_prometheus_exporter;
}
