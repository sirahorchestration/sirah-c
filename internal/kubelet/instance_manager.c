/*
 * instance_manager.c
 *
 * Instance metadata tracking implementation
 */

#include "instance_manager.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/**
 * Create instance manager
 */
instance_manager_t* instance_manager_create(const char *instance_id,
                                            const char *pod_name,
                                            const char *namespace,
                                            const char *node_name) {
    if (!instance_id || !pod_name || !namespace || !node_name) {
        return NULL;
    }
    
    instance_manager_t *manager = malloc(sizeof(instance_manager_t));
    if (!manager) return NULL;
    
    memset(manager, 0, sizeof(instance_manager_t));
    
    // Initialize metadata
    manager->metadata.instance_id = strdup(instance_id);
    manager->metadata.pod_name = strdup(pod_name);
    manager->metadata.namespace = strdup(namespace);
    manager->metadata.node_name = strdup(node_name);
    
    if (!manager->metadata.instance_id || !manager->metadata.pod_name ||
        !manager->metadata.namespace || !manager->metadata.node_name) {
        free(manager->metadata.instance_id);
        free(manager->metadata.pod_name);
        free(manager->metadata.namespace);
        free(manager->metadata.node_name);
        free(manager);
        return NULL;
    }
    
    manager->metadata.status = INSTANCE_STATUS_PENDING;
    manager->metadata.created_time = time(NULL);
    manager->metadata.ready = false;
    manager->metadata.alive = true;
    
    // Default restart configuration
    manager->max_restart_count = 5;
    manager->restart_backoff_seconds = 1;
    manager->max_backoff_seconds = 60;
    manager->backoff_multiplier = 2.0f;
    
    pthread_mutex_init(&manager->mutex, NULL);
    
    return manager;
}

/**
 * Free instance manager
 */
void instance_manager_free(instance_manager_t *manager) {
    if (!manager) return;
    
    free(manager->metadata.instance_id);
    free(manager->metadata.pod_name);
    free(manager->metadata.namespace);
    free(manager->metadata.node_name);
    free(manager->metadata.image);
    free(manager->metadata.pod_ip);
    free(manager->metadata.container_ports);
    
    for (uint32_t i = 0; i < manager->metadata.label_count; i += 2) {
        free(manager->metadata.labels[i]);
        free(manager->metadata.labels[i + 1]);
    }
    free(manager->metadata.labels);
    
    pthread_mutex_destroy(&manager->mutex);
    free(manager);
}

/**
 * Set pod specification
 */
bool instance_manager_set_pod_spec(instance_manager_t *manager,
                                    const char *image,
                                    const char *pod_ip,
                                    uint64_t memory_limit,
                                    uint64_t cpu_limit_millicores) {
    if (!manager || !image || !pod_ip) {
        return false;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    manager->metadata.image = strdup(image);
    manager->metadata.pod_ip = strdup(pod_ip);
    manager->metadata.memory_limit_bytes = memory_limit;
    manager->metadata.cpu_limit_millicores = cpu_limit_millicores;
    
    if (!manager->metadata.image || !manager->metadata.pod_ip) {
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return true;
}

/**
 * Add container port
 */
bool instance_manager_add_port(instance_manager_t *manager, uint16_t port) {
    if (!manager) return false;
    
    pthread_mutex_lock(&manager->mutex);
    
    uint16_t *new_ports = realloc(manager->metadata.container_ports,
                                   sizeof(uint16_t) * (manager->metadata.port_count + 1));
    if (!new_ports) {
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }
    
    manager->metadata.container_ports = new_ports;
    manager->metadata.container_ports[manager->metadata.port_count] = port;
    manager->metadata.port_count++;
    
    pthread_mutex_unlock(&manager->mutex);
    return true;
}

/**
 * Set resource limits
 */
bool instance_manager_set_resources(instance_manager_t *manager,
                                     uint64_t memory_limit,
                                     uint64_t memory_request,
                                     uint64_t cpu_limit,
                                     uint64_t cpu_request) {
    if (!manager) return false;
    
    pthread_mutex_lock(&manager->mutex);
    
    manager->metadata.memory_limit_bytes = memory_limit;
    manager->metadata.memory_request_bytes = memory_request;
    manager->metadata.cpu_limit_millicores = cpu_limit;
    manager->metadata.cpu_request_millicores = cpu_request;
    
    pthread_mutex_unlock(&manager->mutex);
    return true;
}

/**
 * Update instance status
 */
bool instance_manager_update_status(instance_manager_t *manager,
                                     instance_status_t new_status,
                                     uint32_t pid) {
    if (!manager) return false;
    
    pthread_mutex_lock(&manager->mutex);
    
    manager->metadata.status = new_status;
    manager->metadata.pid = pid;
    manager->last_status_update = time(NULL);
    
    if (new_status == INSTANCE_STATUS_RUNNING) {
        manager->metadata.started_time = time(NULL);
    } else if (new_status == INSTANCE_STATUS_STOPPED || new_status == INSTANCE_STATUS_FAILED) {
        manager->metadata.stopped_time = time(NULL);
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return true;
}

/**
 * Mark instance as ready
 */
bool instance_manager_set_ready(instance_manager_t *manager, bool ready) {
    if (!manager) return false;
    
    pthread_mutex_lock(&manager->mutex);
    manager->metadata.ready = ready;
    pthread_mutex_unlock(&manager->mutex);
    
    return true;
}

/**
 * Mark instance as alive
 */
bool instance_manager_set_alive(instance_manager_t *manager, bool alive) {
    if (!manager) return false;
    
    pthread_mutex_lock(&manager->mutex);
    manager->metadata.alive = alive;
    pthread_mutex_unlock(&manager->mutex);
    
    return true;
}

/**
 * Record failed health check
 */
bool instance_manager_health_check_failed(instance_manager_t *manager) {
    if (!manager) return false;
    
    pthread_mutex_lock(&manager->mutex);
    manager->metadata.failed_health_checks++;
    pthread_mutex_unlock(&manager->mutex);
    
    return true;
}

/**
 * Reset health check failures
 */
bool instance_manager_health_check_success(instance_manager_t *manager) {
    if (!manager) return false;
    
    pthread_mutex_lock(&manager->mutex);
    manager->metadata.failed_health_checks = 0;
    pthread_mutex_unlock(&manager->mutex);
    
    return true;
}

/**
 * Mark instance for restart
 */
bool instance_manager_mark_restart(instance_manager_t *manager) {
    if (!manager) return false;
    
    pthread_mutex_lock(&manager->mutex);
    
    if (manager->metadata.restart_count >= manager->max_restart_count) {
        manager->metadata.status = INSTANCE_STATUS_FAILED;
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }
    
    manager->metadata.restart_count++;
    manager->metadata.status = INSTANCE_STATUS_STARTING;
    manager->metadata.ready = false;
    manager->metadata.failed_health_checks = 0;
    
    // Calculate backoff: min(restart_backoff * (backoff_multiplier ^ count), max_backoff)
    uint32_t backoff = (uint32_t)(manager->restart_backoff_seconds *
                                  pow(manager->backoff_multiplier, 
                                      manager->metadata.restart_count - 1));
    if (backoff > manager->max_backoff_seconds) {
        backoff = manager->max_backoff_seconds;
    }
    
    manager->next_restart_time = time(NULL) + backoff;
    
    pthread_mutex_unlock(&manager->mutex);
    return true;
}

/**
 * Get restart backoff time
 */
uint32_t instance_manager_get_restart_backoff(instance_manager_t *manager) {
    if (!manager) return 0;
    
    pthread_mutex_lock(&manager->mutex);
    
    time_t now = time(NULL);
    uint32_t backoff = (manager->next_restart_time > now) ?
                       (manager->next_restart_time - now) : 0;
    
    pthread_mutex_unlock(&manager->mutex);
    return backoff;
}

/**
 * Check if restart is allowed
 */
bool instance_manager_can_restart(instance_manager_t *manager) {
    if (!manager) return false;
    
    pthread_mutex_lock(&manager->mutex);
    
    bool can_restart = (manager->metadata.restart_count < manager->max_restart_count) &&
                       (time(NULL) >= manager->next_restart_time);
    
    pthread_mutex_unlock(&manager->mutex);
    return can_restart;
}

/**
 * Get metadata
 */
bool instance_manager_get_metadata(instance_manager_t *manager,
                                    instance_metadata_t *out_metadata) {
    if (!manager || !out_metadata) return false;
    
    pthread_mutex_lock(&manager->mutex);
    memcpy(out_metadata, &manager->metadata, sizeof(instance_metadata_t));
    pthread_mutex_unlock(&manager->mutex);
    
    return true;
}

/**
 * Add label
 */
bool instance_manager_add_label(instance_manager_t *manager,
                                 const char *key,
                                 const char *value) {
    if (!manager || !key || !value) return false;
    
    pthread_mutex_lock(&manager->mutex);
    
    // Labels stored as key-value pairs in array
    char **new_labels = realloc(manager->metadata.labels,
                                sizeof(char*) * (manager->metadata.label_count + 2));
    if (!new_labels) {
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }
    
    manager->metadata.labels = new_labels;
    manager->metadata.labels[manager->metadata.label_count] = strdup(key);
    manager->metadata.labels[manager->metadata.label_count + 1] = strdup(value);
    manager->metadata.label_count += 2;
    
    pthread_mutex_unlock(&manager->mutex);
    return true;
}

/**
 * Get label value
 */
const char* instance_manager_get_label(instance_manager_t *manager,
                                        const char *key) {
    if (!manager || !key) return NULL;
    
    pthread_mutex_lock(&manager->mutex);
    
    for (uint32_t i = 0; i < manager->metadata.label_count; i += 2) {
        if (strcmp(manager->metadata.labels[i], key) == 0) {
            const char *value = manager->metadata.labels[i + 1];
            pthread_mutex_unlock(&manager->mutex);
            return value;
        }
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return NULL;
}
