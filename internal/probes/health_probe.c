/*
 * health_probe.c
 * 
 * Health probe execution and state tracking
 */

#include "health_probe.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>

/**
 * Create pod probe manager
 */
pod_probe_manager_t* pod_probe_manager_create(const char *pod_name,
                                               const char *namespace,
                                               const char *pod_ip) {
    if (!pod_name || !namespace || !pod_ip) {
        return NULL;
    }
    
    pod_probe_manager_t *manager = malloc(sizeof(pod_probe_manager_t));
    if (!manager) return NULL;
    
    memset(manager, 0, sizeof(pod_probe_manager_t));
    
    manager->pod_name = strdup(pod_name);
    manager->namespace = strdup(namespace);
    manager->pod_ip = strdup(pod_ip);
    
    if (!manager->pod_name || !manager->namespace || !manager->pod_ip) {
        free(manager->pod_name);
        free(manager->namespace);
        free(manager->pod_ip);
        free(manager);
        return NULL;
    }
    
    // Initialize probe array
    manager->probe_count = 0;
    manager->probes = NULL;
    
    pthread_mutex_init(&manager->mutex, NULL);
    
    return manager;
}

/**
 * Free pod probe manager
 */
void pod_probe_manager_free(pod_probe_manager_t *manager) {
    if (!manager) return;
    
    for (uint32_t i = 0; i < manager->probe_count; i++) {
        free(manager->probes[i].pod_name);
        free(manager->probes[i].namespace);
        free(manager->probes[i].container_name);
        free(manager->probes[i].pod_ip);
        free(manager->probes[i].state.last_error);
        pthread_mutex_destroy(&manager->probes[i].mutex);
    }
    free(manager->probes);
    
    free(manager->pod_name);
    free(manager->namespace);
    free(manager->pod_ip);
    
    pthread_mutex_destroy(&manager->mutex);
    free(manager);
}

/**
 * Add probe to pod
 */
probe_instance_t* pod_probe_manager_add_probe(pod_probe_manager_t *manager,
                                               const probe_config_t *config) {
    if (!manager || !config) {
        return NULL;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    // Expand probe array
    probe_instance_t *new_probes = realloc(manager->probes,
                                            sizeof(probe_instance_t) * (manager->probe_count + 1));
    if (!new_probes) {
        pthread_mutex_unlock(&manager->mutex);
        return NULL;
    }
    
    manager->probes = new_probes;
    probe_instance_t *probe = &manager->probes[manager->probe_count];
    memset(probe, 0, sizeof(probe_instance_t));
    
    probe->pod_name = strdup(manager->pod_name);
    probe->namespace = strdup(manager->namespace);
    probe->pod_ip = strdup(manager->pod_ip);
    
    if (!probe->pod_name || !probe->namespace || !probe->pod_ip) {
        free(probe->pod_name);
        free(probe->namespace);
        free(probe->pod_ip);
        pthread_mutex_unlock(&manager->mutex);
        return NULL;
    }
    
    memcpy(&probe->config, config, sizeof(probe_config_t));
    pthread_mutex_init(&probe->mutex, NULL);
    
    manager->probe_count++;
    pthread_mutex_unlock(&manager->mutex);
    
    return probe;
}

/**
 * Execute HTTP probe
 */
probe_result_t execute_http_probe(const char *pod_ip,
                                   const http_probe_config_t *config,
                                   uint32_t timeout_seconds) {
    if (!pod_ip || !config || !config->path) {
        return PROBE_RESULT_ERROR;
    }
    
    // In production, would use libcurl with timeout
    // For now, simulate successful probes
    
    return PROBE_RESULT_SUCCESS;
}

/**
 * Execute TCP probe
 */
probe_result_t execute_tcp_probe(const char *pod_ip,
                                  const tcp_probe_config_t *config,
                                  uint32_t timeout_seconds) {
    if (!pod_ip || !config || config->port == 0) {
        return PROBE_RESULT_ERROR;
    }
    
    // Try to connect to TCP port
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return PROBE_RESULT_ERROR;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config->port);
    
    if (inet_pton(AF_INET, pod_ip, &addr.sin_addr) != 1) {
        close(sock);
        return PROBE_RESULT_ERROR;
    }
    
    // Set timeout
    struct timeval tv;
    tv.tv_sec = timeout_seconds;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    // Try connection
    int result = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    close(sock);
    
    return (result == 0) ? PROBE_RESULT_SUCCESS : PROBE_RESULT_FAILURE;
}

/**
 * Execute Exec probe
 */
probe_result_t execute_exec_probe(const char *pod_name,
                                   const char *namespace,
                                   const exec_probe_config_t *config,
                                   uint32_t timeout_seconds) {
    if (!pod_name || !namespace || !config || !config->command || config->command_len == 0) {
        return PROBE_RESULT_ERROR;
    }
    
    // In production, would exec in container namespace
    // For now, simulate successful probes
    
    return PROBE_RESULT_SUCCESS;
}

/**
 * Execute probe (dispatcher)
 */
probe_result_t pod_probe_execute(pod_probe_manager_t *manager,
                                  const probe_config_t *probe_config) {
    if (!manager || !probe_config) {
        return PROBE_RESULT_ERROR;
    }
    
    switch (probe_config->handler_type) {
        case PROBE_HANDLER_HTTP:
            return execute_http_probe(manager->pod_ip, &probe_config->handler_config.http,
                                     probe_config->timeout_seconds);
        case PROBE_HANDLER_TCP:
            return execute_tcp_probe(manager->pod_ip, &probe_config->handler_config.tcp,
                                    probe_config->timeout_seconds);
        case PROBE_HANDLER_EXEC:
            return execute_exec_probe(manager->pod_name, manager->namespace,
                                     &probe_config->handler_config.exec,
                                     probe_config->timeout_seconds);
        default:
            return PROBE_RESULT_ERROR;
    }
}

/**
 * Update probe result
 */
bool pod_probe_update_result(probe_instance_t *probe, probe_result_t result) {
    if (!probe) return false;
    
    pthread_mutex_lock(&probe->mutex);
    
    bool state_changed = false;
    
    if (result == PROBE_RESULT_SUCCESS) {
        probe->state.consecutive_successes++;
        probe->state.consecutive_failures = 0;
        
        // Check if threshold reached
        if (probe->state.consecutive_successes >= probe->config.success_threshold) {
            if (!probe->state.passed) {
                probe->state.passed = true;
                state_changed = true;
            }
        }
    } else {
        probe->state.consecutive_failures++;
        probe->state.consecutive_successes = 0;
        
        // Check if threshold reached
        if (probe->state.consecutive_failures >= probe->config.failure_threshold) {
            if (probe->state.passed) {
                probe->state.passed = false;
                state_changed = true;
            }
        }
    }
    
    probe->state.last_result = result;
    probe->state.last_check_time = time(NULL);
    
    pthread_mutex_unlock(&probe->mutex);
    
    return state_changed;
}

/**
 * Check startup complete
 */
bool pod_probe_is_startup_complete(pod_probe_manager_t *manager) {
    if (!manager) return false;
    
    pthread_mutex_lock(&manager->mutex);
    
    // All startup probes must pass (or no startup probes means complete)
    bool has_startup = false;
    bool all_startup_passed = true;
    
    for (uint32_t i = 0; i < manager->probe_count; i++) {
        if (manager->probes[i].config.type == PROBE_TYPE_STARTUP) {
            has_startup = true;
            if (!manager->probes[i].state.passed) {
                all_startup_passed = false;
                break;
            }
        }
    }
    
    pthread_mutex_unlock(&manager->mutex);
    
    // If no startup probes, startup is complete
    return !has_startup || all_startup_passed;
}

/**
 * Check if pod is ready
 */
bool pod_probe_is_ready(pod_probe_manager_t *manager) {
    if (!manager) return false;
    
    pthread_mutex_lock(&manager->mutex);
    
    // Startup must be complete
    bool startup_complete = true;
    for (uint32_t i = 0; i < manager->probe_count; i++) {
        if (manager->probes[i].config.type == PROBE_TYPE_STARTUP &&
            !manager->probes[i].state.passed) {
            startup_complete = false;
            break;
        }
    }
    
    // All readiness probes must pass (or no readiness probes means ready)
    bool has_readiness = false;
    bool all_readiness_passed = true;
    
    for (uint32_t i = 0; i < manager->probe_count; i++) {
        if (manager->probes[i].config.type == PROBE_TYPE_READINESS) {
            has_readiness = true;
            if (!manager->probes[i].state.passed) {
                all_readiness_passed = false;
                break;
            }
        }
    }
    
    pthread_mutex_unlock(&manager->mutex);
    
    return startup_complete && (!has_readiness || all_readiness_passed);
}

/**
 * Check if pod is alive
 */
bool pod_probe_is_alive(pod_probe_manager_t *manager) {
    if (!manager) return true;  // Default alive if no probes
    
    pthread_mutex_lock(&manager->mutex);
    
    // All liveness probes must pass (or no liveness probes means alive)
    bool has_liveness = false;
    bool all_liveness_passed = true;
    
    for (uint32_t i = 0; i < manager->probe_count; i++) {
        if (manager->probes[i].config.type == PROBE_TYPE_LIVENESS) {
            has_liveness = true;
            if (!manager->probes[i].state.passed) {
                all_liveness_passed = false;
                break;
            }
        }
    }
    
    pthread_mutex_unlock(&manager->mutex);
    
    return !has_liveness || all_liveness_passed;
}

/**
 * Get probe state
 */
bool pod_probe_get_state(pod_probe_manager_t *manager,
                         probe_type_t probe_type,
                         probe_state_t *out_state) {
    if (!manager || !out_state) return false;
    
    pthread_mutex_lock(&manager->mutex);
    
    for (uint32_t i = 0; i < manager->probe_count; i++) {
        if (manager->probes[i].config.type == probe_type) {
            memcpy(out_state, &manager->probes[i].state, sizeof(probe_state_t));
            pthread_mutex_unlock(&manager->mutex);
            return true;
        }
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return false;
}

/**
 * Get statistics
 */
void pod_probe_get_stats(pod_probe_manager_t *manager,
                         uint32_t *out_total,
                         uint32_t *out_passing,
                         uint32_t *out_failing) {
    if (!manager) return;
    
    pthread_mutex_lock(&manager->mutex);
    
    if (out_total) *out_total = manager->probe_count;
    
    uint32_t passing = 0, failing = 0;
    for (uint32_t i = 0; i < manager->probe_count; i++) {
        if (manager->probes[i].state.passed) {
            passing++;
        } else {
            failing++;
        }
    }
    
    if (out_passing) *out_passing = passing;
    if (out_failing) *out_failing = failing;
    
    pthread_mutex_unlock(&manager->mutex);
}

/**
 * Free probe config
 */
void pod_probe_free_config(probe_config_t *config) {
    if (!config) return;
    
    if (config->handler_type == PROBE_HANDLER_HTTP) {
        free(config->handler_config.http.path);
        free(config->handler_config.http.host);
        free(config->handler_config.http.scheme);
    } else if (config->handler_type == PROBE_HANDLER_EXEC) {
        if (config->handler_config.exec.command) {
            for (uint32_t i = 0; i < config->handler_config.exec.command_len; i++) {
                free(config->handler_config.exec.command[i]);
            }
            free(config->handler_config.exec.command);
        }
    }
}
