/*
 * node_monitor.c
 * 
 * Implementation of node failure detection and recovery
 */

#include "node_monitor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/**
 * Monitoring thread function
 */
static void* node_monitor_thread(void *arg);

/**
 * Create a new node monitor
 */
node_monitor_t* node_monitor_new(const node_monitor_config_t *config) {
    if (!config) {
        return NULL;
    }
    
    node_monitor_t *monitor = (node_monitor_t *)malloc(sizeof(node_monitor_t));
    if (!monitor) {
        return NULL;
    }
    
    monitor->config = *config;
    pthread_mutex_init(&monitor->mutex, NULL);
    
    monitor->heartbeat_capacity = 100;
    monitor->heartbeat_count = 0;
    monitor->heartbeats = (node_heartbeat_t *)calloc(monitor->heartbeat_capacity, sizeof(node_heartbeat_t));
    
    if (!monitor->heartbeats) {
        free(monitor);
        return NULL;
    }
    
    monitor->monitor_running = false;
    
    monitor->on_node_offline = NULL;
    monitor->on_node_recovered = NULL;
    monitor->evict_pods_from_node = NULL;
    monitor->callback_userdata = NULL;
    
    return monitor;
}

/**
 * Free node monitor
 */
void node_monitor_free(node_monitor_t *monitor) {
    if (!monitor) {
        return;
    }
    
    if (monitor->monitor_running) {
        node_monitor_stop(monitor);
    }
    
    pthread_mutex_lock(&monitor->mutex);
    
    for (uint32_t i = 0; i < monitor->heartbeat_count; i++) {
        free(monitor->heartbeats[i].node_name);
        free(monitor->heartbeats[i].status_message);
    }
    
    free(monitor->heartbeats);
    
    pthread_mutex_unlock(&monitor->mutex);
    pthread_mutex_destroy(&monitor->mutex);
    
    free(monitor);
}

/**
 * Monitoring thread function
 */
static void* node_monitor_thread(void *arg) {
    node_monitor_t *monitor = (node_monitor_t *)arg;
    
    while (monitor->monitor_running) {
        sleep(monitor->config.check_interval_seconds);
        
        pthread_mutex_lock(&monitor->mutex);
        
        time_t now = time(NULL);
        
        // Check each node for timeout
        for (uint32_t i = 0; i < monitor->heartbeat_count; i++) {
            node_heartbeat_t *hb = &monitor->heartbeats[i];
            
            // Check if node is offline
            time_t time_since_heartbeat = now - hb->last_heartbeat;
            bool is_now_offline = (time_since_heartbeat > (time_t)monitor->config.heartbeat_timeout_seconds);
            bool was_offline = (hb->status == NODE_STATUS_NOTREADY || hb->status == NODE_STATUS_UNKNOWN);
            
            if (is_now_offline && !was_offline) {
                // Node just went offline
                hb->status = NODE_STATUS_NOTREADY;
                if (hb->status_message) {
                    free(hb->status_message);
                }
                hb->status_message = (char *)malloc(100);
                snprintf(hb->status_message, 100, "Heartbeat timeout (no response for %ld seconds)",
                        time_since_heartbeat);
                
                printf("NODE_MONITOR: Node '%s' is offline\n", hb->node_name);
                
                // Call offline callback
                if (monitor->on_node_offline) {
                    void (*callback)(const char *, void *) = monitor->on_node_offline;
                    char *node_name = strdup(hb->node_name);
                    void *userdata = monitor->callback_userdata;
                    pthread_mutex_unlock(&monitor->mutex);
                    
                    callback(node_name, userdata);
                    free(node_name);
                    
                    pthread_mutex_lock(&monitor->mutex);
                }
                
                // If configured, evict pods from failed node
                if (monitor->config.enable_pod_eviction) {
                    if (monitor->evict_pods_from_node) {
                        void (*evict)(const char *, void *) = monitor->evict_pods_from_node;
                        char *node_name = strdup(hb->node_name);
                        void *userdata = monitor->callback_userdata;
                        pthread_mutex_unlock(&monitor->mutex);
                        
                        printf("NODE_MONITOR: Evicting pods from failed node '%s'\n", node_name);
                        evict(node_name, userdata);
                        free(node_name);
                        
                        pthread_mutex_lock(&monitor->mutex);
                    }
                }
            } else if (!is_now_offline && was_offline) {
                // Node recovered
                hb->status = NODE_STATUS_READY;
                if (hb->status_message) {
                    free(hb->status_message);
                    hb->status_message = NULL;
                }
                
                printf("NODE_MONITOR: Node '%s' recovered\n", hb->node_name);
                
                // Call recovered callback
                if (monitor->on_node_recovered) {
                    void (*callback)(const char *, void *) = monitor->on_node_recovered;
                    char *node_name = strdup(hb->node_name);
                    void *userdata = monitor->callback_userdata;
                    pthread_mutex_unlock(&monitor->mutex);
                    
                    callback(node_name, userdata);
                    free(node_name);
                    
                    pthread_mutex_lock(&monitor->mutex);
                }
            }
        }
        
        pthread_mutex_unlock(&monitor->mutex);
    }
    
    return NULL;
}

/**
 * Start monitoring nodes
 */
bool node_monitor_start(node_monitor_t *monitor) {
    if (!monitor) {
        return false;
    }
    
    pthread_mutex_lock(&monitor->mutex);
    
    if (monitor->monitor_running) {
        pthread_mutex_unlock(&monitor->mutex);
        return true;  // Already running
    }
    
    monitor->monitor_running = true;
    
    pthread_mutex_unlock(&monitor->mutex);
    
    if (pthread_create(&monitor->monitor_thread, NULL, node_monitor_thread, monitor) != 0) {
        pthread_mutex_lock(&monitor->mutex);
        monitor->monitor_running = false;
        pthread_mutex_unlock(&monitor->mutex);
        perror("pthread_create");
        return false;
    }
    
    printf("NODE_MONITOR: Started monitoring thread\n");
    
    return true;
}

/**
 * Stop monitoring nodes
 */
void node_monitor_stop(node_monitor_t *monitor) {
    if (!monitor) {
        return;
    }
    
    pthread_mutex_lock(&monitor->mutex);
    monitor->monitor_running = false;
    pthread_mutex_unlock(&monitor->mutex);
    
    printf("NODE_MONITOR: Waiting for monitoring thread to exit\n");
    pthread_join(monitor->monitor_thread, NULL);
    printf("NODE_MONITOR: Monitor stopped\n");
}

/**
 * Register a heartbeat from a node
 */
void node_monitor_register_heartbeat(node_monitor_t *monitor,
                                     const char *node_name,
                                     node_status_t status,
                                     const char *status_msg,
                                     uint32_t allocatable_cpu,
                                     uint64_t allocatable_memory,
                                     uint32_t pod_count) {
    if (!monitor || !node_name) {
        return;
    }
    
    pthread_mutex_lock(&monitor->mutex);
    
    // Find or create heartbeat entry
    node_heartbeat_t *hb = NULL;
    for (uint32_t i = 0; i < monitor->heartbeat_count; i++) {
        if (strcmp(monitor->heartbeats[i].node_name, node_name) == 0) {
            hb = &monitor->heartbeats[i];
            break;
        }
    }
    
    if (!hb) {
        // Need new entry
        if (monitor->heartbeat_count >= monitor->heartbeat_capacity) {
            // Expand array
            uint32_t new_capacity = monitor->heartbeat_capacity * 2;
            node_heartbeat_t *new_heartbeats = (node_heartbeat_t *)realloc(
                monitor->heartbeats,
                sizeof(node_heartbeat_t) * new_capacity
            );
            if (!new_heartbeats) {
                pthread_mutex_unlock(&monitor->mutex);
                return;
            }
            
            memset(&new_heartbeats[monitor->heartbeat_capacity], 0,
                   sizeof(node_heartbeat_t) * (new_capacity - monitor->heartbeat_capacity));
            
            monitor->heartbeats = new_heartbeats;
            monitor->heartbeat_capacity = new_capacity;
        }
        
        hb = &monitor->heartbeats[monitor->heartbeat_count++];
        hb->node_name = (char *)malloc(strlen(node_name) + 1);
        strcpy(hb->node_name, node_name);
        hb->status_message = NULL;
    }
    
    // Update heartbeat
    hb->last_heartbeat = time(NULL);
    hb->status = status;
    hb->allocatable_cpu_millicores = allocatable_cpu;
    hb->allocatable_memory_bytes = allocatable_memory;
    hb->active_pod_count = pod_count;
    
    // Update status message
    if (hb->status_message) {
        free(hb->status_message);
    }
    if (status_msg) {
        hb->status_message = (char *)malloc(strlen(status_msg) + 1);
        strcpy(hb->status_message, status_msg);
    } else {
        hb->status_message = NULL;
    }
    
    pthread_mutex_unlock(&monitor->mutex);
}

/**
 * Get node status
 */
node_status_t node_monitor_get_status(node_monitor_t *monitor, const char *node_name) {
    if (!monitor || !node_name) {
        return NODE_STATUS_UNKNOWN;
    }
    
    pthread_mutex_lock(&monitor->mutex);
    
    for (uint32_t i = 0; i < monitor->heartbeat_count; i++) {
        if (strcmp(monitor->heartbeats[i].node_name, node_name) == 0) {
            node_status_t status = monitor->heartbeats[i].status;
            pthread_mutex_unlock(&monitor->mutex);
            return status;
        }
    }
    
    pthread_mutex_unlock(&monitor->mutex);
    return NODE_STATUS_UNKNOWN;
}

/**
 * Check if node is offline
 */
bool node_monitor_is_offline(node_monitor_t *monitor, const char *node_name) {
    if (!monitor || !node_name) {
        return true;
    }
    
    pthread_mutex_lock(&monitor->mutex);
    
    time_t now = time(NULL);
    
    for (uint32_t i = 0; i < monitor->heartbeat_count; i++) {
        if (strcmp(monitor->heartbeats[i].node_name, node_name) == 0) {
            time_t time_since_hb = now - monitor->heartbeats[i].last_heartbeat;
            bool is_offline = (time_since_hb > (time_t)monitor->config.heartbeat_timeout_seconds);
            pthread_mutex_unlock(&monitor->mutex);
            return is_offline;
        }
    }
    
    pthread_mutex_unlock(&monitor->mutex);
    return true;  // Unknown node is offline
}

/**
 * Get node heartbeat info
 */
node_heartbeat_t* node_monitor_get_heartbeat(node_monitor_t *monitor, const char *node_name) {
    if (!monitor || !node_name) {
        return NULL;
    }
    
    pthread_mutex_lock(&monitor->mutex);
    
    node_heartbeat_t *result = NULL;
    
    for (uint32_t i = 0; i < monitor->heartbeat_count; i++) {
        if (strcmp(monitor->heartbeats[i].node_name, node_name) == 0) {
            node_heartbeat_t *hb = &monitor->heartbeats[i];
            result = (node_heartbeat_t *)malloc(sizeof(node_heartbeat_t));
            if (result) {
                result->node_name = (char *)malloc(strlen(hb->node_name) + 1);
                strcpy(result->node_name, hb->node_name);
                
                result->status_message = NULL;
                if (hb->status_message) {
                    result->status_message = (char *)malloc(strlen(hb->status_message) + 1);
                    strcpy(result->status_message, hb->status_message);
                }
                
                result->last_heartbeat = hb->last_heartbeat;
                result->status = hb->status;
                result->allocatable_cpu_millicores = hb->allocatable_cpu_millicores;
                result->allocatable_memory_bytes = hb->allocatable_memory_bytes;
                result->active_pod_count = hb->active_pod_count;
            }
            break;
        }
    }
    
    pthread_mutex_unlock(&monitor->mutex);
    
    return result;
}

/**
 * Free heartbeat struct
 */
void node_monitor_free_heartbeat(node_heartbeat_t *hb) {
    if (!hb) {
        return;
    }
    
    free(hb->node_name);
    free(hb->status_message);
    free(hb);
}

/**
 * Register callback for when node comes online
 */
void node_monitor_set_online_callback(node_monitor_t *monitor,
                                      void (*callback)(const char *node_name, void *userdata),
                                      void *userdata) {
    if (!monitor) {
        return;
    }
    
    pthread_mutex_lock(&monitor->mutex);
    monitor->on_node_recovered = callback;
    monitor->callback_userdata = userdata;
    pthread_mutex_unlock(&monitor->mutex);
}

/**
 * Register callback for when node goes offline
 */
void node_monitor_set_offline_callback(node_monitor_t *monitor,
                                       void (*callback)(const char *node_name, void *userdata),
                                       void *userdata) {
    if (!monitor) {
        return;
    }
    
    pthread_mutex_lock(&monitor->mutex);
    monitor->on_node_offline = callback;
    monitor->callback_userdata = userdata;
    pthread_mutex_unlock(&monitor->mutex);
}

/**
 * Register callback for pod eviction from failed nodes
 */
void node_monitor_set_eviction_callback(node_monitor_t *monitor,
                                        void (*callback)(const char *node_name, void *userdata),
                                        void *userdata) {
    if (!monitor) {
        return;
    }
    
    pthread_mutex_lock(&monitor->mutex);
    monitor->evict_pods_from_node = callback;
    monitor->callback_userdata = userdata;
    pthread_mutex_unlock(&monitor->mutex);
}

/**
 * Manually trigger eviction of pods from node
 */
void node_monitor_evict_node_pods(node_monitor_t *monitor, const char *node_name) {
    if (!monitor || !node_name) {
        return;
    }
    
    pthread_mutex_lock(&monitor->mutex);
    
    if (monitor->evict_pods_from_node) {
        void (*evict)(const char *, void *) = monitor->evict_pods_from_node;
        void *userdata = monitor->callback_userdata;
        pthread_mutex_unlock(&monitor->mutex);
        
        evict(node_name, userdata);
        
        pthread_mutex_lock(&monitor->mutex);
    }
    
    pthread_mutex_unlock(&monitor->mutex);
}
