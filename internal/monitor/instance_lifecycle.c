/*
 * instance_lifecycle.c
 * 
 * Instance/pod lifecycle management implementation
 */

#include "instance_lifecycle.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

/**
 * Create instance metadata
 */
instance_metadata_t* instance_metadata_create(const char *pod_name,
                                             const char *namespace,
                                             const char *node_name) {
    if (!pod_name || !namespace || !node_name) {
        return NULL;
    }
    
    instance_metadata_t *metadata = malloc(sizeof(instance_metadata_t));
    if (!metadata) return NULL;
    
    memset(metadata, 0, sizeof(instance_metadata_t));
    
    metadata->pod_name = strdup(pod_name);
    metadata->namespace = strdup(namespace);
    metadata->node_name = strdup(node_name);
    
    if (!metadata->pod_name || !metadata->namespace || !metadata->node_name) {
        free(metadata->pod_name);
        free(metadata->namespace);
        free(metadata->node_name);
        free(metadata);
        return NULL;
    }
    
    metadata->state = INSTANCE_STATE_CREATED;
    metadata->creation_time = time(NULL);
    metadata->state_change_time = metadata->creation_time;
    metadata->last_exit_code = -1;
    metadata->is_ready = false;
    
    pthread_mutex_init(&metadata->mutex, NULL);
    
    return metadata;
}

/**
 * Free instance metadata
 */
void instance_metadata_free(instance_metadata_t *metadata) {
    if (!metadata) return;
    
    free(metadata->pod_name);
    free(metadata->namespace);
    free(metadata->node_name);
    free(metadata->process_path);
    free(metadata->last_failure_reason);
    
    if (metadata->labels) {
        for (uint32_t i = 0; i < metadata->label_count; i++) {
            free(metadata->labels[i]);
        }
        free(metadata->labels);
    }
    
    pthread_mutex_destroy(&metadata->mutex);
    free(metadata);
}

/**
 * Set process information
 */
bool instance_metadata_set_process(instance_metadata_t *metadata,
                                  pid_t process_id,
                                  const char *process_path) {
    if (!metadata || process_id <= 0 || !process_path) {
        return false;
    }
    
    pthread_mutex_lock(&metadata->mutex);
    
    metadata->process_id = process_id;
    
    free(metadata->process_path);
    metadata->process_path = strdup(process_path);
    
    if (!metadata->process_path) {
        pthread_mutex_unlock(&metadata->mutex);
        return false;
    }
    
    pthread_mutex_unlock(&metadata->mutex);
    return true;
}

/**
 * Set resource limits
 */
bool instance_metadata_set_limits(instance_metadata_t *metadata,
                                 uint64_t memory_bytes,
                                 uint32_t cpu_millicores) {
    if (!metadata) {
        return false;
    }
    
    pthread_mutex_lock(&metadata->mutex);
    
    metadata->memory_limit_bytes = memory_bytes;
    metadata->cpu_limit_millicores = cpu_millicores;
    
    pthread_mutex_unlock(&metadata->mutex);
    return true;
}

/**
 * Update instance state
 */
bool instance_metadata_set_state(instance_metadata_t *metadata,
                                instance_state_t new_state) {
    if (!metadata) {
        return false;
    }
    
    pthread_mutex_lock(&metadata->mutex);
    
    if (metadata->state != new_state) {
        metadata->state = new_state;
        metadata->state_change_time = time(NULL);
    }
    
    pthread_mutex_unlock(&metadata->mutex);
    return true;
}

/**
 * Get current state
 */
instance_state_t instance_metadata_get_state(instance_metadata_t *metadata) {
    if (!metadata) {
        return INSTANCE_STATE_FAILED;
    }
    
    pthread_mutex_lock(&metadata->mutex);
    instance_state_t state = metadata->state;
    pthread_mutex_unlock(&metadata->mutex);
    
    return state;
}

/**
 * Mark instance as ready
 */
bool instance_metadata_set_ready(instance_metadata_t *metadata) {
    if (!metadata) {
        return false;
    }
    
    pthread_mutex_lock(&metadata->mutex);
    
    if (!metadata->is_ready) {
        metadata->is_ready = true;
        metadata->ready_time = time(NULL);
    }
    
    pthread_mutex_unlock(&metadata->mutex);
    return true;
}

/**
 * Mark instance as not ready
 */
bool instance_metadata_set_not_ready(instance_metadata_t *metadata) {
    if (!metadata) {
        return false;
    }
    
    pthread_mutex_lock(&metadata->mutex);
    metadata->is_ready = false;
    pthread_mutex_unlock(&metadata->mutex);
    
    return true;
}

/**
 * Record restart event
 */
bool instance_metadata_record_restart(instance_metadata_t *metadata) {
    if (!metadata) {
        return false;
    }
    
    pthread_mutex_lock(&metadata->mutex);
    
    metadata->restart_count++;
    metadata->last_restart_time = time(NULL);
    metadata->state = INSTANCE_STATE_RESTARTING;
    
    pthread_mutex_unlock(&metadata->mutex);
    return true;
}

/**
 * Record failure event
 */
bool instance_metadata_record_failure(instance_metadata_t *metadata,
                                     int exit_code,
                                     const char *failure_reason) {
    if (!metadata) {
        return false;
    }
    
    pthread_mutex_lock(&metadata->mutex);
    
    metadata->last_exit_code = exit_code;
    metadata->last_failure_time = time(NULL);
    metadata->consecutive_failures++;
    metadata->state = INSTANCE_STATE_FAILED;
    
    free(metadata->last_failure_reason);
    if (failure_reason) {
        metadata->last_failure_reason = strdup(failure_reason);
    }
    
    pthread_mutex_unlock(&metadata->mutex);
    return true;
}

/**
 * Clear consecutive failures
 */
bool instance_metadata_clear_failures(instance_metadata_t *metadata) {
    if (!metadata) {
        return false;
    }
    
    pthread_mutex_lock(&metadata->mutex);
    metadata->consecutive_failures = 0;
    pthread_mutex_unlock(&metadata->mutex);
    
    return true;
}

/**
 * Check if instance should be auto-restarted
 */
bool instance_metadata_should_restart(instance_metadata_t *metadata) {
    if (!metadata) {
        return false;
    }
    
    pthread_mutex_lock(&metadata->mutex);
    
    // Don't restart if in drain mode or already stopped
    if (metadata->state == INSTANCE_STATE_DRAINING ||
        metadata->state == INSTANCE_STATE_STOPPING ||
        metadata->state == INSTANCE_STATE_STOPPED) {
        pthread_mutex_unlock(&metadata->mutex);
        return false;
    }
    
    // Check if max restarts reached
    if (metadata->restart_count >= 5) {  // Max 5 restarts
        pthread_mutex_unlock(&metadata->mutex);
        return false;
    }
    
    pthread_mutex_unlock(&metadata->mutex);
    return true;
}

/**
 * Get restart delay with exponential backoff
 * Base: 1s, Max: 300s (5 minutes)
 * Backoff: 1s, 2s, 4s, 8s, 16s, 32s, 64s, 128s, 256s, 300s
 */
uint32_t instance_metadata_get_restart_delay(instance_metadata_t *metadata) {
    if (!metadata) {
        return 1;
    }
    
    pthread_mutex_lock(&metadata->mutex);
    uint32_t restart_count = metadata->restart_count;
    pthread_mutex_unlock(&metadata->mutex);
    
    // Exponential backoff: 2^n seconds, max 300 seconds
    uint32_t delay = 1;
    for (uint32_t i = 0; i < restart_count && delay < 300; i++) {
        delay *= 2;
    }
    
    return (delay > 300) ? 300 : delay;
}

/**
 * Create lifecycle manager
 */
instance_lifecycle_manager_t* instance_lifecycle_manager_create(uint32_t check_interval) {
    instance_lifecycle_manager_t *manager = malloc(sizeof(instance_lifecycle_manager_t));
    if (!manager) return NULL;
    
    memset(manager, 0, sizeof(instance_lifecycle_manager_t));
    
    manager->check_interval_seconds = check_interval > 0 ? check_interval : 10;
    manager->max_restarts = 5;
    manager->initial_backoff_seconds = 1;
    manager->max_backoff_seconds = 300;
    manager->grace_period_seconds = 30;
    manager->drain_mode = false;
    
    pthread_mutex_init(&manager->mutex, NULL);
    
    return manager;
}

/**
 * Free lifecycle manager
 */
void instance_lifecycle_manager_free(instance_lifecycle_manager_t *manager) {
    if (!manager) return;
    
    if (manager->running) {
        instance_lifecycle_manager_stop(manager);
    }
    
    if (manager->instances) {
        for (uint32_t i = 0; i < manager->instance_count; i++) {
            if (manager->instances[i]) {
                instance_metadata_free(manager->instances[i]);
            }
        }
        free(manager->instances);
    }
    
    pthread_mutex_destroy(&manager->mutex);
    free(manager);
}

/**
 * Add instance to manager
 */
bool instance_lifecycle_manager_add(instance_lifecycle_manager_t *manager,
                                   instance_metadata_t *metadata) {
    if (!manager || !metadata) {
        return false;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    instance_metadata_t **new_instances = realloc(manager->instances,
                                                  sizeof(instance_metadata_t *) * (manager->instance_count + 1));
    if (!new_instances) {
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }
    
    manager->instances = new_instances;
    manager->instances[manager->instance_count] = metadata;
    manager->instance_count++;
    
    pthread_mutex_unlock(&manager->mutex);
    return true;
}

/**
 * Remove instance from manager
 */
bool instance_lifecycle_manager_remove(instance_lifecycle_manager_t *manager,
                                      const char *pod_name) {
    if (!manager || !pod_name) {
        return false;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    for (uint32_t i = 0; i < manager->instance_count; i++) {
        if (manager->instances[i] && strcmp(manager->instances[i]->pod_name, pod_name) == 0) {
            instance_metadata_free(manager->instances[i]);
            
            // Shift remaining instances
            for (uint32_t j = i; j < manager->instance_count - 1; j++) {
                manager->instances[j] = manager->instances[j + 1];
            }
            
            manager->instance_count--;
            
            if (manager->instance_count == 0) {
                free(manager->instances);
                manager->instances = NULL;
            }
            
            pthread_mutex_unlock(&manager->mutex);
            return true;
        }
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return false;
}

/**
 * Get instance by name
 */
instance_metadata_t* instance_lifecycle_manager_get(instance_lifecycle_manager_t *manager,
                                                   const char *pod_name) {
    if (!manager || !pod_name) {
        return NULL;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    for (uint32_t i = 0; i < manager->instance_count; i++) {
        if (manager->instances[i] && strcmp(manager->instances[i]->pod_name, pod_name) == 0) {
            instance_metadata_t *result = manager->instances[i];
            pthread_mutex_unlock(&manager->mutex);
            return result;
        }
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return NULL;
}

/**
 * Monitor thread
 */
static void* instance_lifecycle_monitor_thread(void *arg) {
    instance_lifecycle_manager_t *manager = (instance_lifecycle_manager_t *)arg;
    
    while (manager->running) {
        pthread_mutex_lock(&manager->mutex);
        
        // Check each instance
        for (uint32_t i = 0; i < manager->instance_count; i++) {
            if (manager->instances[i]) {
                // Check if process is still alive
                instance_metadata_t *meta = manager->instances[i];
                
                if (meta->process_id > 0) {
                    // Check if process exists
                    int ret = kill(meta->process_id, 0);
                    if (ret != 0) {
                        // Process is dead
                        instance_metadata_set_state(meta, INSTANCE_STATE_FAILED);
                        
                        // Try to restart if not in drain mode
                        if (!manager->drain_mode && instance_metadata_should_restart(meta)) {
                            instance_metadata_record_restart(meta);
                        }
                    }
                }
            }
        }
        
        pthread_mutex_unlock(&manager->mutex);
        
        // Sleep for check interval
        sleep(manager->check_interval_seconds);
    }
    
    return NULL;
}

/**
 * Start monitoring thread
 */
bool instance_lifecycle_manager_start(instance_lifecycle_manager_t *manager) {
    if (!manager) return false;
    
    if (manager->running) return true;
    
    manager->running = true;
    
    int ret = pthread_create(&manager->monitor_thread, NULL,
                            instance_lifecycle_monitor_thread, manager);
    if (ret != 0) {
        manager->running = false;
        return false;
    }
    
    return true;
}

/**
 * Stop monitoring thread
 */
bool instance_lifecycle_manager_stop(instance_lifecycle_manager_t *manager) {
    if (!manager) return false;
    
    if (!manager->running) return true;
    
    manager->running = false;
    pthread_join(manager->monitor_thread, NULL);
    
    return true;
}

/**
 * Check instance health
 */
bool instance_check_health(instance_metadata_t *metadata) {
    if (!metadata || metadata->process_id <= 0) {
        return false;  // Unhealthy
    }
    
    // Check if process exists
    if (kill(metadata->process_id, 0) != 0) {
        return false;  // Process dead
    }
    
    return true;  // Process alive
}

/**
 * Gracefully shutdown instance
 */
bool instance_shutdown_graceful(instance_metadata_t *metadata,
                               uint32_t grace_period_seconds) {
    if (!metadata || metadata->process_id <= 0) {
        return false;
    }
    
    instance_metadata_set_state(metadata, INSTANCE_STATE_STOPPING);
    
    // Send SIGTERM to allow graceful shutdown
    kill(metadata->process_id, SIGTERM);
    
    // Wait for graceful period
    time_t start_time = time(NULL);
    while (time(NULL) - start_time < grace_period_seconds) {
        if (kill(metadata->process_id, 0) != 0) {
            // Process exited gracefully
            instance_metadata_set_state(metadata, INSTANCE_STATE_STOPPED);
            return true;
        }
        sleep(1);
    }
    
    // Grace period expired, force kill
    kill(metadata->process_id, SIGKILL);
    sleep(1);
    
    instance_metadata_set_state(metadata, INSTANCE_STATE_STOPPED);
    return true;
}

/**
 * Force kill instance
 */
bool instance_shutdown_force(instance_metadata_t *metadata) {
    if (!metadata || metadata->process_id <= 0) {
        return false;
    }
    
    instance_metadata_set_state(metadata, INSTANCE_STATE_STOPPING);
    
    // Force kill
    kill(metadata->process_id, SIGKILL);
    sleep(1);
    
    instance_metadata_set_state(metadata, INSTANCE_STATE_STOPPED);
    return true;
}

/**
 * Restart instance
 */
bool instance_restart(instance_metadata_t *metadata) {
    if (!metadata || !instance_metadata_should_restart(metadata)) {
        return false;
    }
    
    instance_metadata_record_restart(metadata);
    return true;
}

/**
 * Enable node drain mode
 */
bool instance_lifecycle_manager_enable_drain(instance_lifecycle_manager_t *manager) {
    if (!manager) return false;
    
    pthread_mutex_lock(&manager->mutex);
    manager->drain_mode = true;
    pthread_mutex_unlock(&manager->mutex);
    
    return true;
}

/**
 * Disable node drain mode
 */
bool instance_lifecycle_manager_disable_drain(instance_lifecycle_manager_t *manager) {
    if (!manager) return false;
    
    pthread_mutex_lock(&manager->mutex);
    manager->drain_mode = false;
    pthread_mutex_unlock(&manager->mutex);
    
    return true;
}

/**
 * Check if node is draining
 */
bool instance_lifecycle_manager_is_draining(instance_lifecycle_manager_t *manager) {
    if (!manager) return false;
    
    pthread_mutex_lock(&manager->mutex);
    bool draining = manager->drain_mode;
    pthread_mutex_unlock(&manager->mutex);
    
    return draining;
}

/**
 * Get number of draining instances
 */
uint32_t instance_lifecycle_manager_get_draining_count(instance_lifecycle_manager_t *manager) {
    if (!manager) return 0;
    
    pthread_mutex_lock(&manager->mutex);
    
    uint32_t count = 0;
    for (uint32_t i = 0; i < manager->instance_count; i++) {
        if (manager->instances[i] && 
            manager->instances[i]->state == INSTANCE_STATE_DRAINING) {
            count++;
        }
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return count;
}

/**
 * State to string
 */
const char* instance_state_to_string(instance_state_t state) {
    switch (state) {
        case INSTANCE_STATE_CREATED:
            return "Created";
        case INSTANCE_STATE_RUNNING:
            return "Running";
        case INSTANCE_STATE_READY:
            return "Ready";
        case INSTANCE_STATE_NOT_READY:
            return "NotReady";
        case INSTANCE_STATE_RESTARTING:
            return "Restarting";
        case INSTANCE_STATE_STOPPING:
            return "Stopping";
        case INSTANCE_STATE_STOPPED:
            return "Stopped";
        case INSTANCE_STATE_FAILED:
            return "Failed";
        case INSTANCE_STATE_DRAINING:
            return "Draining";
        default:
            return "Unknown";
    }
}

/**
 * String to state
 */
instance_state_t instance_string_to_state(const char *state_str) {
    if (!state_str) return INSTANCE_STATE_FAILED;
    
    if (strcmp(state_str, "Created") == 0) return INSTANCE_STATE_CREATED;
    if (strcmp(state_str, "Running") == 0) return INSTANCE_STATE_RUNNING;
    if (strcmp(state_str, "Ready") == 0) return INSTANCE_STATE_READY;
    if (strcmp(state_str, "NotReady") == 0) return INSTANCE_STATE_NOT_READY;
    if (strcmp(state_str, "Restarting") == 0) return INSTANCE_STATE_RESTARTING;
    if (strcmp(state_str, "Stopping") == 0) return INSTANCE_STATE_STOPPING;
    if (strcmp(state_str, "Stopped") == 0) return INSTANCE_STATE_STOPPED;
    if (strcmp(state_str, "Failed") == 0) return INSTANCE_STATE_FAILED;
    if (strcmp(state_str, "Draining") == 0) return INSTANCE_STATE_DRAINING;
    
    return INSTANCE_STATE_FAILED;
}
