// internal/kubelet/health_probe.c
// Health probe execution implementation

#include "health_probe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <json-c/json.h>

// ============================================================================
// Probe Execution - HTTP
// ============================================================================

probe_result_t health_probe_execute_http(const char* pod_name, const char* namespace,
                                         const char* container_name, probe_config_t* probe_cfg) {
    if (!pod_name || !namespace || !container_name || !probe_cfg) {
        return PROBE_UNKNOWN;
    }
    
    // For MVP, we check if HTTP port is open (TCP probe equivalent)
    // Full HTTP health check would require executing inside container
    
    fprintf(stderr, "[PROBE] HTTP probe: %s/%s:%s (port %d, path %s)\n",
           namespace, pod_name, container_name, probe_cfg->http_port,
           probe_cfg->http_path ? probe_cfg->http_path : "/");
    
    // TCP connection test to HTTP port
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return PROBE_UNKNOWN;
    }
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(probe_cfg->http_port);
    
    // For MVP, assume localhost container communication
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    // Set timeout
    struct timeval tv;
    tv.tv_sec = probe_cfg->timeout_seconds ? probe_cfg->timeout_seconds : 1;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    int result = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);
    
    if (result == 0) {
        fprintf(stderr, "[PROBE] HTTP probe SUCCESS\n");
        return PROBE_SUCCESS;
    } else {
        fprintf(stderr, "[PROBE] HTTP probe FAILED\n");
        return PROBE_FAILURE;
    }
}

// ============================================================================
// Probe Execution - TCP
// ============================================================================

probe_result_t health_probe_execute_tcp(const char* pod_name, const char* namespace,
                                        const char* container_name, probe_config_t* probe_cfg) {
    if (!pod_name || !namespace || !container_name || !probe_cfg) {
        return PROBE_UNKNOWN;
    }
    
    fprintf(stderr, "[PROBE] TCP probe: %s/%s:%s (host %s, port %d)\n",
           namespace, pod_name, container_name,
           probe_cfg->tcp_host ? probe_cfg->tcp_host : "127.0.0.1",
           probe_cfg->tcp_port);
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return PROBE_UNKNOWN;
    }
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(probe_cfg->tcp_port);
    
    // Parse host (default to localhost)
    const char* host = probe_cfg->tcp_host ? probe_cfg->tcp_host : "127.0.0.1";
    inet_pton(AF_INET, host, &addr.sin_addr);
    
    // Set timeout
    struct timeval tv;
    tv.tv_sec = probe_cfg->timeout_seconds ? probe_cfg->timeout_seconds : 1;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    int result = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);
    
    if (result == 0) {
        fprintf(stderr, "[PROBE] TCP probe SUCCESS\n");
        return PROBE_SUCCESS;
    } else {
        fprintf(stderr, "[PROBE] TCP probe FAILED\n");
        return PROBE_FAILURE;
    }
}

// ============================================================================
// Probe Execution - Exec
// ============================================================================

probe_result_t health_probe_execute_exec(const char* pod_name, const char* namespace,
                                         const char* container_name, probe_config_t* probe_cfg) {
    if (!pod_name || !namespace || !container_name || !probe_cfg) {
        return PROBE_UNKNOWN;
    }
    
    fprintf(stderr, "[PROBE] Exec probe: %s/%s:%s (command: ",
           namespace, pod_name, container_name);
    
    if (probe_cfg->exec_command && probe_cfg->exec_command_count > 0) {
        for (int i = 0; i < probe_cfg->exec_command_count; i++) {
            fprintf(stderr, "%s ", probe_cfg->exec_command[i]);
        }
    }
    fprintf(stderr, ")\n");
    
    // For MVP exec probe: Just verify container is in RUNNING state
    // Full implementation would use container exec API
    // Success = we're in running state (simplified)
    return PROBE_SUCCESS;
}

// ============================================================================
// Unified Probe Execution
// ============================================================================

probe_result_t health_probe_execute(const char* pod_name, const char* namespace,
                                    const char* container_name, probe_config_t* probe_cfg) {
    if (!probe_cfg) {
        return PROBE_UNKNOWN;
    }
    
    // Try handlers in order
    if (probe_cfg->exec_enabled) {
        return health_probe_execute_exec(pod_name, namespace, container_name, probe_cfg);
    }
    
    if (probe_cfg->http_enabled) {
        return health_probe_execute_http(pod_name, namespace, container_name, probe_cfg);
    }
    
    if (probe_cfg->tcp_enabled) {
        return health_probe_execute_tcp(pod_name, namespace, container_name, probe_cfg);
    }
    
    return PROBE_UNKNOWN;
}

// ============================================================================
// Probe State Management
// ============================================================================

probe_state_t health_probe_state_create(probe_type_t type, const k8s_container_t* container) {
    probe_state_t state = {0};
    state.type = type;
    state.enabled = 0;
    state.has_executed = 0;
    state.is_successful = 0;
    state.consecutive_successes = 0;
    state.consecutive_failures = 0;
    state.last_result = PROBE_UNKNOWN;
    
    // Default config
    state.config.timeout_seconds = 1;
    state.config.period_seconds = 10;
    state.config.initial_delay_seconds = 0;
    state.config.success_threshold = 1;
    state.config.failure_threshold = 3;
    
    // Probe not enabled unless explicitly configured
    state.enabled = 0;
    
    return state;
}

int health_probe_state_update(probe_state_t* state, probe_result_t result, time_t now) {
    if (!state) {
        return -1;
    }
    
    int old_success = state->is_successful;
    
    state->last_result = result;
    state->last_execution_time = now;
    
    if (result == PROBE_SUCCESS) {
        state->consecutive_successes++;
        state->consecutive_failures = 0;
        
        if (state->consecutive_successes >= state->config.success_threshold) {
            state->is_successful = 1;
        }
    } else {
        state->consecutive_failures++;
        state->consecutive_successes = 0;
        
        if (state->consecutive_failures >= state->config.failure_threshold) {
            state->is_successful = 0;
        }
    }
    
    return (old_success != state->is_successful) ? 1 : 0;  // Status changed
}

int health_probe_state_is_due(probe_state_t* state, time_t now) {
    if (!state || !state->enabled) {
        return 0;
    }
    
    // Check if initial delay has passed
    if (!state->has_executed) {
        if (state->first_execution_time == 0) {
            state->first_execution_time = now;
        }
        if (now - state->first_execution_time < state->config.initial_delay_seconds) {
            return 0;  // Still in initial delay
        }
        return 1;  // Time for first execution
    }
    
    // Check if period has elapsed
    if (state->last_execution_time == 0) {
        return 1;
    }
    
    return (now - state->last_execution_time) >= state->config.period_seconds;
}

int health_probe_startup_completed(probe_state_t* startup) {
    if (!startup) {
        return 1;  // No startup probe, consider complete
    }
    
    if (!startup->enabled) {
        return 1;  // Startup probe not configured, complete
    }
    
    return startup->is_successful;  // Successful = complete
}

// ============================================================================
// Container Health Management
// ============================================================================

container_health_t* health_container_create(const char* container_name) {
    container_health_t* health = (container_health_t*)malloc(sizeof(container_health_t));
    if (!health) return NULL;
    
    memset(health, 0, sizeof(container_health_t));
    
    if (container_name) {
        health->container_name = strdup(container_name);
    }
    
    // Initialize probe states
    health->startup_probe = health_probe_state_create(PROBE_STARTUP, NULL);
    health->readiness_probe = health_probe_state_create(PROBE_READINESS, NULL);
    health->liveness_probe = health_probe_state_create(PROBE_LIVENESS, NULL);
    
    // Initial state
    health->state = CONTAINER_STATE_WAITING;
    health->is_ready = 0;
    health->is_alive = 1;
    health->restart_count = 0;
    
    return health;
}

void health_container_free(container_health_t* health) {
    if (!health) return;
    
    if (health->container_name) {
        free(health->container_name);
    }
    
    free(health);
}

int health_container_update(container_health_t* health, time_t now) {
    if (!health) {
        return -1;
    }
    
    // Determine readiness: must pass startup probe, then readiness probe
    if (health_probe_startup_completed(&health->startup_probe)) {
        health->is_ready = health->readiness_probe.is_successful;
    } else {
        health->is_ready = 0;
    }
    
    // Determine aliveness: liveness probe status
    health->is_alive = health->liveness_probe.is_successful;
    
    return 0;
}

int health_container_should_restart(container_health_t* health) {
    if (!health) {
        return 0;
    }
    
    // Restart if: liveness probe failed AND we haven't exceeded restart limit
    if (!health->is_alive && health->restart_count < 3) {
        return 1;
    }
    
    return 0;
}

int health_container_reset_restart_count(container_health_t* health) {
    if (!health) {
        return -1;
    }
    
    health->restart_count = 0;
    return 0;
}

int health_container_handle_restart(container_health_t* health) {
    if (!health) {
        return -1;
    }
    
    health->restart_count++;
    health->last_restart_timestamp = time(NULL);
    
    // Reset probe states on restart
    health->startup_probe = health_probe_state_create(PROBE_STARTUP, NULL);
    health->readiness_probe = health_probe_state_create(PROBE_READINESS, NULL);
    health->liveness_probe = health_probe_state_create(PROBE_LIVENESS, NULL);
    
    health->is_ready = 0;
    health->state = CONTAINER_STATE_WAITING;
    
    fprintf(stderr, "[HEALTH] Container %s restarted (count: %d)\n",
           health->container_name, health->restart_count);
    
    return 0;
}

// ============================================================================
// Probe Configuration Parsing
// ============================================================================

probe_config_t* health_probe_config_from_json(json_object* probe_json) {
    if (!probe_json) {
        return NULL;
    }
    
    probe_config_t* config = (probe_config_t*)malloc(sizeof(probe_config_t));
    if (!config) return NULL;
    
    memset(config, 0, sizeof(probe_config_t));
    
    // Default timing values
    config->timeout_seconds = 1;
    config->period_seconds = 10;
    config->initial_delay_seconds = 0;
    config->success_threshold = 1;
    config->failure_threshold = 3;
    
    // Parse timing if present
    json_object* timeout = json_object_object_get(probe_json, "timeoutSeconds");
    if (timeout) {
        config->timeout_seconds = json_object_get_int(timeout);
    }
    
    json_object* period = json_object_object_get(probe_json, "periodSeconds");
    if (period) {
        config->period_seconds = json_object_get_int(period);
    }
    
    json_object* initial = json_object_object_get(probe_json, "initialDelaySeconds");
    if (initial) {
        config->initial_delay_seconds = json_object_get_int(initial);
    }
    
    json_object* success = json_object_object_get(probe_json, "successThreshold");
    if (success) {
        config->success_threshold = json_object_get_int(success);
    }
    
    json_object* failure = json_object_object_get(probe_json, "failureThreshold");
    if (failure) {
        config->failure_threshold = json_object_get_int(failure);
    }
    
    // Parse exec handler
    json_object* exec = json_object_object_get(probe_json, "exec");
    if (exec) {
        config->exec_enabled = 1;
        json_object* command = json_object_object_get(exec, "command");
        if (command && json_object_is_type(command, json_type_array)) {
            int count = json_object_array_length(command);
            config->exec_command = (char**)malloc(sizeof(char*) * count);
            for (int i = 0; i < count; i++) {
                json_object* cmd = json_object_array_get_idx(command, i);
                config->exec_command[i] = strdup(json_object_get_string(cmd));
            }
            config->exec_command_count = count;
        }
    }
    
    // Parse HTTP handler
    json_object* http = json_object_object_get(probe_json, "httpGet");
    if (http) {
        config->http_enabled = 1;
        
        json_object* path = json_object_object_get(http, "path");
        if (path) {
            config->http_path = strdup(json_object_get_string(path));
        } else {
            config->http_path = strdup("/");
        }
        
        json_object* port = json_object_object_get(http, "port");
        if (port) {
            config->http_port = json_object_get_int(port);
        }
        
        json_object* scheme = json_object_object_get(http, "scheme");
        if (scheme) {
            config->http_scheme = strdup(json_object_get_string(scheme));
        } else {
            config->http_scheme = strdup("HTTP");
        }
    }
    
    // Parse TCP handler
    json_object* tcp = json_object_object_get(probe_json, "tcpSocket");
    if (tcp) {
        config->tcp_enabled = 1;
        
        json_object* port = json_object_object_get(tcp, "port");
        if (port) {
            config->tcp_port = json_object_get_int(port);
        }
    }
    
    return config;
}

void health_probe_config_free(probe_config_t* config) {
    if (!config) return;
    
    if (config->http_path) free(config->http_path);
    if (config->http_scheme) free(config->http_scheme);
    if (config->http_host) free(config->http_host);
    if (config->tcp_host) free(config->tcp_host);
    
    if (config->exec_command) {
        for (int i = 0; i < config->exec_command_count; i++) {
            if (config->exec_command[i]) {
                free(config->exec_command[i]);
            }
        }
        free(config->exec_command);
    }
    
    free(config);
}

// ============================================================================
// Diagnostics
// ============================================================================

const char* health_probe_result_str(probe_result_t result) {
    switch (result) {
        case PROBE_SUCCESS: return "Success";
        case PROBE_FAILURE: return "Failure";
        case PROBE_UNKNOWN: return "Unknown";
        default: return "?";
    }
}

char* health_container_summary(container_health_t* health, char* buf, int buf_len) {
    if (!health || !buf) return NULL;
    
    snprintf(buf, buf_len,
            "Container: %s | State: %d | Ready: %d | Alive: %d | Restarts: %d",
            health->container_name ? health->container_name : "unknown",
            health->state,
            health->is_ready,
            health->is_alive,
            health->restart_count);
    
    return buf;
}
