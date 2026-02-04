// internal/kubelet/probe_executor.c
// Health probe execution engine
// Implements startup, readiness, and liveness probes per Kubernetes semantics

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include "probe_handler.h"
#include "pod_spawner.h"

// ============================================================================
// Probe State Management
// ============================================================================

typedef struct {
    char pod_name[256];
    char namespace[256];
    char container_name[256];
    probe_state_t startup_probe;
    probe_state_t readiness_probe;
    probe_state_t liveness_probe;
} container_probe_state_t;

#define MAX_CONTAINER_PROBES 512
static container_probe_state_t g_probes[MAX_CONTAINER_PROBES];
static int g_probe_count = 0;

// ============================================================================
// Probe Handlers
// ============================================================================

/**
 * Execute HTTP GET probe
 */
static probe_result_t probe_http_handler(const char* pod_name, const char* namespace,
                                        const char* container_name, probe_config_t* config) {
    if (!config->http_enabled || !config->http_host || config->http_port <= 0) {
        return PROBE_UNKNOWN;
    }
    
    CURL* curl = curl_easy_init();
    if (!curl) return PROBE_UNKNOWN;
    
    char url[512];
    const char* scheme = config->http_scheme ? config->http_scheme : "http";
    const char* path = config->http_path ? config->http_path : "/";
    
    snprintf(url, sizeof(url), "%s://%s:%d%s",
             scheme, config->http_host, config->http_port, path);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)config->timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);  // HEAD request
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);
    
    if (res == CURLE_OK && response_code >= 200 && response_code < 300) {
        return PROBE_SUCCESS;
    }
    
    return PROBE_FAILURE;
}

/**
 * Execute TCP socket probe
 */
static probe_result_t probe_tcp_handler(const char* pod_name, const char* namespace,
                                       const char* container_name, probe_config_t* config) {
    if (!config->tcp_enabled || !config->tcp_host || config->tcp_port <= 0) {
        return PROBE_UNKNOWN;
    }
    
    // In a real implementation, would attempt TCP connection
    // For MVP, assume TCP probe passes if we can reach the container
    fprintf(stderr, "[PROBE] TCP probe: %s:%d (not implemented, assuming success)\n",
            config->tcp_host, config->tcp_port);
    
    return PROBE_SUCCESS;
}

/**
 * Execute exec probe (run command in container)
 */
static probe_result_t probe_exec_handler(const char* pod_name, const char* namespace,
                                        const char* container_name, probe_config_t* config) {
    if (!config->exec_enabled || !config->exec_command || config->exec_command_count <= 0) {
        return PROBE_UNKNOWN;
    }
    
    // In a real implementation, would exec command in container
    // For MVP, assume exec probe passes
    fprintf(stderr, "[PROBE] Exec probe: %s (not implemented, assuming success)\n",
            config->exec_command[0]);
    
    return PROBE_SUCCESS;
}

/**
 * Execute a single probe
 */
static probe_result_t probe_execute(const char* pod_name, const char* namespace,
                                   const char* container_name, probe_config_t* config) {
    if (!config) return PROBE_UNKNOWN;
    
    // Check if enough time has passed since last probe
    time_t now = time(NULL);
    // (probe frequency check would go here)
    
    // Execute the configured handler
    if (config->http_enabled) {
        return probe_http_handler(pod_name, namespace, container_name, config);
    }
    
    if (config->tcp_enabled) {
        return probe_tcp_handler(pod_name, namespace, container_name, config);
    }
    
    if (config->exec_enabled) {
        return probe_exec_handler(pod_name, namespace, container_name, config);
    }
    
    return PROBE_UNKNOWN;
}

// ============================================================================
// Probe State Updates
// ============================================================================

static void update_probe_state(probe_state_t* probe_state, probe_result_t result) {
    if (!probe_state) return;
    
    probe_state->last_execution_time = time(NULL);
    probe_state->last_result = result;
    
    if (result == PROBE_SUCCESS) {
        probe_state->consecutive_successes++;
        probe_state->consecutive_failures = 0;
        
        // Check if reached success threshold
        if (probe_state->consecutive_successes >= probe_state->config.success_threshold) {
            probe_state->is_successful = 1;
        }
    } else if (result == PROBE_FAILURE) {
        probe_state->consecutive_failures++;
        probe_state->consecutive_successes = 0;
        
        // Check if reached failure threshold
        if (probe_state->consecutive_failures >= probe_state->config.failure_threshold) {
            probe_state->is_successful = 0;
        }
    }
}

// ============================================================================
// Registration and Lookup
// ============================================================================

int probe_register_container(const char* namespace, const char* pod_name,
                            const char* container_name) {
    if (g_probe_count >= MAX_CONTAINER_PROBES) {
        fprintf(stderr, "[PROBE] Max container probes reached\n");
        return -1;
    }
    
    container_probe_state_t* probe = &g_probes[g_probe_count++];
    memset(probe, 0, sizeof(*probe));
    
    strncpy(probe->namespace, namespace, sizeof(probe->namespace) - 1);
    strncpy(probe->pod_name, pod_name, sizeof(probe->pod_name) - 1);
    strncpy(probe->container_name, container_name, sizeof(probe->container_name) - 1);
    
    // Initialize probe configurations
    // (Defaults: no probes enabled until configured)
    probe->startup_probe.config.success_threshold = 1;
    probe->startup_probe.config.failure_threshold = 3;
    probe->startup_probe.config.timeout_seconds = 1;
    
    probe->readiness_probe.config.success_threshold = 1;
    probe->readiness_probe.config.failure_threshold = 3;
    probe->readiness_probe.config.timeout_seconds = 1;
    
    probe->liveness_probe.config.success_threshold = 1;
    probe->liveness_probe.config.failure_threshold = 3;
    probe->liveness_probe.config.timeout_seconds = 1;
    
    fprintf(stderr, "[PROBE] Registered container probes: %s/%s/%s\n",
            namespace, pod_name, container_name);
    
    return 0;
}

static container_probe_state_t* find_probe_state(const char* namespace,
                                                  const char* pod_name,
                                                  const char* container_name) {
    for (int i = 0; i < g_probe_count; i++) {
        if (strcmp(g_probes[i].namespace, namespace) == 0 &&
            strcmp(g_probes[i].pod_name, pod_name) == 0 &&
            strcmp(g_probes[i].container_name, container_name) == 0) {
            return &g_probes[i];
        }
    }
    return NULL;
}

// ============================================================================
// Public Probe API
// ============================================================================

int probe_check_startup(const char* namespace, const char* pod_name,
                       const char* container_name, probe_config_t* config) {
    if (!config) return 0;  // No probe configured
    
    container_probe_state_t* state = find_probe_state(namespace, pod_name, container_name);
    if (!state) {
        probe_register_container(namespace, pod_name, container_name);
        state = find_probe_state(namespace, pod_name, container_name);
    }
    
    if (!state) return -1;
    
    // Execute probe
    probe_result_t result = probe_execute(pod_name, namespace, container_name, config);
    update_probe_state(&state->startup_probe, result);
    
    // For startup probe, pod is not ready until probe succeeds
    if (state->startup_probe.is_successful) {
        return 1;  // Startup probe passed
    }
    
    return 0;  // Still waiting
}

int probe_check_readiness(const char* namespace, const char* pod_name,
                         const char* container_name, probe_config_t* config) {
    if (!config) return 1;  // No probe = ready
    
    container_probe_state_t* state = find_probe_state(namespace, pod_name, container_name);
    if (!state) {
        probe_register_container(namespace, pod_name, container_name);
        state = find_probe_state(namespace, pod_name, container_name);
    }
    
    if (!state) return -1;
    
    // Execute probe
    probe_result_t result = probe_execute(pod_name, namespace, container_name, config);
    update_probe_state(&state->readiness_probe, result);
    
    return state->readiness_probe.is_successful ? 1 : 0;
}

int probe_check_liveness(const char* namespace, const char* pod_name,
                        const char* container_name, probe_config_t* config) {
    if (!config) return 1;  // No probe = alive
    
    container_probe_state_t* state = find_probe_state(namespace, pod_name, container_name);
    if (!state) {
        probe_register_container(namespace, pod_name, container_name);
        state = find_probe_state(namespace, pod_name, container_name);
    }
    
    if (!state) return -1;
    
    // Execute probe
    probe_result_t result = probe_execute(pod_name, namespace, container_name, config);
    update_probe_state(&state->liveness_probe, result);
    
    if (!state->liveness_probe.is_successful) {
        // Liveness probe failed - pod should be killed
        fprintf(stderr, "[PROBE] Liveness probe failed for %s/%s/%s, pod will be restarted\n",
                namespace, pod_name, container_name);
        return 0;  // Not alive, needs restart
    }
    
    return 1;  // Alive
}

probe_result_t probe_get_result(const char* namespace, const char* pod_name,
                               const char* container_name, probe_type_t probe_type) {
    container_probe_state_t* state = find_probe_state(namespace, pod_name, container_name);
    if (!state) return PROBE_UNKNOWN;
    
    switch (probe_type) {
        case PROBE_STARTUP:
            return state->startup_probe.last_result;
        case PROBE_READINESS:
            return state->readiness_probe.last_result;
        case PROBE_LIVENESS:
            return state->liveness_probe.last_result;
        default:
            return PROBE_UNKNOWN;
    }
}

int probe_cleanup(void) {
    fprintf(stderr, "[PROBE] Cleaning up %d registered probes\n", g_probe_count);
    g_probe_count = 0;
    return 0;
}
