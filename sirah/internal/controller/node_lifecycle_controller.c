// internal/controller/node_lifecycle_controller.c
// Node Lifecycle Controller Implementation

#include "node_lifecycle_controller.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

static node_lifecycle_controller_t g_controller = {0};

// ============ Initialization & Cleanup ============

int node_lifecycle_controller_init(void) {
    memset(&g_controller, 0, sizeof(node_lifecycle_controller_t));
    pthread_mutex_init(&g_controller.lock, NULL);
    return 0;
}

int node_lifecycle_controller_run(void) {
    g_controller.running = 1;
    
    // Start health check thread
    if (pthread_create(&g_controller.health_check_thread, NULL,
                      node_lifecycle_health_check_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create health check thread\n");
        return -1;
    }
    
    // Start eviction thread
    if (pthread_create(&g_controller.eviction_thread, NULL,
                      node_lifecycle_eviction_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create eviction thread\n");
        return -1;
    }
    
    printf("Node Lifecycle Controller started\n");
    return 0;
}

int node_lifecycle_controller_shutdown(void) {
    g_controller.running = 0;
    pthread_join(g_controller.health_check_thread, NULL);
    pthread_join(g_controller.eviction_thread, NULL);
    pthread_mutex_destroy(&g_controller.lock);
    return 0;
}

// ============ Node Registration ============

int node_lifecycle_register_node(const char* name) {
    if (!name || strlen(name) == 0) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    // Check if node already exists
    for (int i = 0; i < g_controller.node_count; i++) {
        if (strcmp(g_controller.nodes[i].name, name) == 0) {
            pthread_mutex_unlock(&g_controller.lock);
            return 0;  // Already registered
        }
    }
    
    if (g_controller.node_count >= NODE_MAX_NODES) {
        pthread_mutex_unlock(&g_controller.lock);
        return -1;  // Max nodes exceeded
    }
    
    // Create new node record
    node_lifecycle_record_t* node = &g_controller.nodes[g_controller.node_count++];
    memset(node, 0, sizeof(node_lifecycle_record_t));
    
    strncpy(node->name, name, sizeof(node->name) - 1);
    node->state = NODE_STATE_INITIALIZING;
    node->health_status = NODE_HEALTH_UNKNOWN;
    node->joined_at = time(NULL);
    node->last_heartbeat = time(NULL);
    node->consecutive_unhealthy_checks = 0;
    
    pthread_mutex_unlock(&g_controller.lock);
    
    printf("[NodeLifecycle] Registered node: %s\n", name);
    return 0;
}

int node_lifecycle_unregister_node(const char* name) {
    if (!name || strlen(name) == 0) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.node_count; i++) {
        if (strcmp(g_controller.nodes[i].name, name) == 0) {
            // Remove node
            if (i < g_controller.node_count - 1) {
                memmove(&g_controller.nodes[i], &g_controller.nodes[i + 1],
                       (g_controller.node_count - i - 1) * sizeof(node_lifecycle_record_t));
            }
            g_controller.node_count--;
            
            pthread_mutex_unlock(&g_controller.lock);
            printf("[NodeLifecycle] Unregistered node: %s\n", name);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;  // Node not found
}

int node_lifecycle_node_heartbeat(const char* name) {
    if (!name || strlen(name) == 0) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.node_count; i++) {
        if (strcmp(g_controller.nodes[i].name, name) == 0) {
            node_lifecycle_record_t* node = &g_controller.nodes[i];
            node->last_heartbeat = time(NULL);
            
            // If node was offline, transition back to healthy (if health checks pass)
            if (node->health_status == NODE_HEALTH_OFFLINE) {
                node->health_status = NODE_HEALTH_UNKNOWN;
                node->consecutive_unhealthy_checks = 0;
            }
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

// ============ Health Monitoring ============

int node_lifecycle_update_health(const char* name, 
                                int cpu_available_pct,
                                int memory_available_pct,
                                int disk_available_pct) {
    if (!name || strlen(name) == 0) return -1;
    if (cpu_available_pct < 0 || cpu_available_pct > 100) return -1;
    if (memory_available_pct < 0 || memory_available_pct > 100) return -1;
    if (disk_available_pct < 0 || disk_available_pct > 100) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.node_count; i++) {
        if (strcmp(g_controller.nodes[i].name, name) == 0) {
            node_lifecycle_record_t* node = &g_controller.nodes[i];
            
            // Add metric to history
            if (node->metric_count < 10) {
                node_health_metric_t* metric = &node->health_metrics[node->metric_count++];
                metric->timestamp = time(NULL);
                metric->cpu_available_percent = cpu_available_pct;
                metric->memory_available_percent = memory_available_pct;
                metric->disk_available_percent = disk_available_pct;
                metric->ready_pod_count = node->ready_pods;
                metric->total_pod_count = node->total_pods;
                
                // Determine health status based on resource availability
                if (cpu_available_pct > 10 && memory_available_pct > 10 && disk_available_pct > 10) {
                    if (node->health_status != NODE_HEALTH_HEALTHY) {
                        node->consecutive_unhealthy_checks = 0;
                    }
                    node->health_status = NODE_HEALTH_HEALTHY;
                    strcpy(metric->reason, "All resources available");
                } else {
                    strcpy(metric->reason, "Low available resources");
                    node->health_status = NODE_HEALTH_UNHEALTHY;
                    node->consecutive_unhealthy_checks++;
                }
            } else {
                // Shift metrics and add new one
                memmove(&node->health_metrics[0], &node->health_metrics[1],
                       9 * sizeof(node_health_metric_t));
                node_health_metric_t* metric = &node->health_metrics[9];
                metric->timestamp = time(NULL);
                metric->cpu_available_percent = cpu_available_pct;
                metric->memory_available_percent = memory_available_pct;
                metric->disk_available_percent = disk_available_pct;
            }
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int node_lifecycle_get_health(const char* name, node_health_status_t* status, 
                             char* reason, int reason_len) {
    if (!name || !status) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.node_count; i++) {
        if (strcmp(g_controller.nodes[i].name, name) == 0) {
            node_lifecycle_record_t* node = &g_controller.nodes[i];
            
            // Check for offline (no heartbeat)
            time_t now = time(NULL);
            if (now - node->last_heartbeat > NODE_HEARTBEAT_TIMEOUT) {
                *status = NODE_HEALTH_OFFLINE;
                if (reason) {
                    snprintf(reason, reason_len, "No heartbeat for %ld seconds",
                            now - node->last_heartbeat);
                }
            } else {
                *status = node->health_status;
                if (reason && node->metric_count > 0) {
                    strncpy(reason, node->health_metrics[node->metric_count - 1].reason, reason_len - 1);
                }
            }
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int node_lifecycle_get_node_status(const char* name, json_object** result) {
    if (!name || !result) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.node_count; i++) {
        if (strcmp(g_controller.nodes[i].name, name) == 0) {
            node_lifecycle_record_t* node = &g_controller.nodes[i];
            
            *result = json_object_new_object();
            json_object_object_add(*result, "name", json_object_new_string(node->name));
            json_object_object_add(*result, "state", json_object_new_int(node->state));
            json_object_object_add(*result, "health_status", json_object_new_int(node->health_status));
            json_object_object_add(*result, "cordoned", json_object_new_int(node->cordoned));
            json_object_object_add(*result, "draining", json_object_new_int(node->draining));
            json_object_object_add(*result, "total_pods", json_object_new_int(node->total_pods));
            json_object_object_add(*result, "ready_pods", json_object_new_int(node->ready_pods));
            
            time_t now = time(NULL);
            json_object_object_add(*result, "age_seconds", json_object_new_int(now - node->joined_at));
            json_object_object_add(*result, "last_heartbeat_seconds_ago",
                                 json_object_new_int(now - node->last_heartbeat));
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int node_lifecycle_list_nodes(json_object** result) {
    if (!result) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    *result = json_object_new_array();
    
    for (int i = 0; i < g_controller.node_count; i++) {
        node_lifecycle_record_t* node = &g_controller.nodes[i];
        
        json_object* node_obj = json_object_new_object();
        json_object_object_add(node_obj, "name", json_object_new_string(node->name));
        json_object_object_add(node_obj, "state", json_object_new_int(node->state));
        json_object_object_add(node_obj, "health_status", json_object_new_int(node->health_status));
        json_object_object_add(node_obj, "total_pods", json_object_new_int(node->total_pods));
        json_object_object_add(node_obj, "ready_pods", json_object_new_int(node->ready_pods));
        json_object_object_add(node_obj, "cordoned", json_object_new_int(node->cordoned));
        
        json_array_add(json_object_get_array(*result), node_obj);
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return 0;
}

// ============ Node State Management ============

int node_lifecycle_cordon_node(const char* name) {
    if (!name) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.node_count; i++) {
        if (strcmp(g_controller.nodes[i].name, name) == 0) {
            g_controller.nodes[i].cordoned = 1;
            printf("[NodeLifecycle] Cordoned node: %s\n", name);
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int node_lifecycle_uncordon_node(const char* name) {
    if (!name) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.node_count; i++) {
        if (strcmp(g_controller.nodes[i].name, name) == 0) {
            g_controller.nodes[i].cordoned = 0;
            printf("[NodeLifecycle] Uncordoned node: %s\n", name);
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int node_lifecycle_drain_node(const char* name, const char* reason) {
    if (!name) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.node_count; i++) {
        if (strcmp(g_controller.nodes[i].name, name) == 0) {
            node_lifecycle_record_t* node = &g_controller.nodes[i];
            node->draining = 1;
            node->state = NODE_STATE_DRAINING;
            node->drain_started_at = time(NULL);
            if (reason) {
                strncpy(node->eviction_reason, reason, sizeof(node->eviction_reason) - 1);
            }
            printf("[NodeLifecycle] Started draining node: %s\n", name);
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int node_lifecycle_get_node_state(const char* name, node_state_t* state) {
    if (!name || !state) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.node_count; i++) {
        if (strcmp(g_controller.nodes[i].name, name) == 0) {
            *state = g_controller.nodes[i].state;
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

// ============ Pod Eviction ============

int node_lifecycle_get_pods_for_eviction(const char* node_name, 
                                        char*** pod_names, char*** pod_namespaces, 
                                        int* count) {
    if (!node_name || !count) return -1;
    
    *count = 0;
    *pod_names = NULL;
    *pod_namespaces = NULL;
    
    // This would be implemented to fetch pods on this node from the pod controller
    // For now, return empty list
    
    return 0;
}

int node_lifecycle_evict_pod(const char* node_name, const char* pod_namespace, 
                            const char* pod_name) {
    if (!node_name || !pod_namespace || !pod_name) return -1;
    
    printf("[NodeLifecycle] Evicting pod %s/%s from node %s\n", 
          pod_namespace, pod_name, node_name);
    
    // This would call the pod controller to delete the pod
    return 0;
}

int node_lifecycle_get_eviction_progress(const char* node_name, 
                                        int* total_pods, int* evicted_pods) {
    if (!node_name || !total_pods || !evicted_pods) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.node_count; i++) {
        if (strcmp(g_controller.nodes[i].name, node_name) == 0) {
            node_lifecycle_record_t* node = &g_controller.nodes[i];
            *total_pods = node->total_pods;
            *evicted_pods = node->total_pods - node->ready_pods;
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

// ============ Health Monitoring Queries ============

int node_lifecycle_list_unhealthy_nodes(char*** node_names, int* count) {
    if (!count) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    *count = 0;
    *node_names = (char**)malloc(g_controller.node_count * sizeof(char*));
    
    for (int i = 0; i < g_controller.node_count; i++) {
        if (g_controller.nodes[i].health_status == NODE_HEALTH_UNHEALTHY) {
            (*node_names)[(*count)++] = strdup(g_controller.nodes[i].name);
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return 0;
}

int node_lifecycle_list_offline_nodes(char*** node_names, int* count) {
    if (!count) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    *count = 0;
    *node_names = (char**)malloc(g_controller.node_count * sizeof(char*));
    
    time_t now = time(NULL);
    for (int i = 0; i < g_controller.node_count; i++) {
        if (now - g_controller.nodes[i].last_heartbeat > NODE_HEARTBEAT_TIMEOUT) {
            (*node_names)[(*count)++] = strdup(g_controller.nodes[i].name);
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return 0;
}

// ============ Background Threads ============

static void* node_lifecycle_health_check_thread(void* arg) {
    (void)arg;
    
    while (g_controller.running) {
        sleep(NODE_HEALTH_CHECK_INTERVAL);
        
        // Health check logic would go here
        // Check metrics, detect offline nodes, etc.
    }
    
    return NULL;
}

static void* node_lifecycle_eviction_thread(void* arg) {
    (void)arg;
    
    while (g_controller.running) {
        sleep(5);
        
        // Eviction logic would go here
        // Process nodes in draining state, evict pods, etc.
    }
    
    return NULL;
}
