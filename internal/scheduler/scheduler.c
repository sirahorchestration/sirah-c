#include "scheduler.h"
#include "scoring.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

// Scheduler creation and lifecycle
scheduler_t* scheduler_new(void) {
    scheduler_t* scheduler = (scheduler_t*)malloc(sizeof(scheduler_t));
    if (!scheduler) return NULL;
    
    memset(scheduler, 0, sizeof(scheduler_t));
    
    scheduler->node_capacity = 10;
    scheduler->nodes = (node_state_t*)malloc(sizeof(node_state_t) * scheduler->node_capacity);
    scheduler->node_count = 0;
    
    scheduler->assignment_count = 0;
    scheduler->assignment_capacity = 5000;  // Increased from 100 to handle more pods
    scheduler->assigned_pods = (char**)malloc(sizeof(char*) * 5000);
    scheduler->assigned_nodes = (char**)malloc(sizeof(char*) * 5000);
    
    scheduler->running = 0;
    pthread_mutex_init(&scheduler->lock, NULL);
    
    // Default configuration
    scheduler->enable_bin_packing = 1;
    scheduler->enable_pod_spread = 1;
    scheduler->enable_affinity = 1;
    scheduler->enable_topology_spread = 1;
    
    // Scoring weights (sum to 100 for percentages)
    scheduler->weight_bin_packing = 40;
    scheduler->weight_pod_spread = 30;
    scheduler->weight_affinity = 20;
    
    printf("Scheduler created with default configuration\n");
    
    return scheduler;
}

void scheduler_free(scheduler_t* scheduler) {
    if (!scheduler) return;
    
    // Free nodes
    for (int i = 0; i < scheduler->node_count; i++) {
        free(scheduler->nodes[i].node_name);
        free(scheduler->nodes[i].status);
        for (int j = 0; j < scheduler->nodes[i].taint_count; j++) {
            free(scheduler->nodes[i].taints[j]);
        }
        free(scheduler->nodes[i].taints);
        for (int j = 0; j < scheduler->nodes[i].label_count; j++) {
            free(scheduler->nodes[i].label_keys[j]);
            free(scheduler->nodes[i].label_values[j]);
        }
        free(scheduler->nodes[i].label_keys);
        free(scheduler->nodes[i].label_values);
    }
    free(scheduler->nodes);
    
    // Free assignments
    for (int i = 0; i < scheduler->assignment_count; i++) {
        free(scheduler->assigned_pods[i]);
        free(scheduler->assigned_nodes[i]);
    }
    free(scheduler->assigned_pods);
    free(scheduler->assigned_nodes);
    
    pthread_mutex_destroy(&scheduler->lock);
    free(scheduler);
}

int scheduler_init(scheduler_t* scheduler) {
    if (!scheduler) return -1;
    scheduler->running = 1;
    printf("Scheduler initialized\n");
    return 0;
}

void scheduler_shutdown(scheduler_t* scheduler) {
    if (!scheduler) return;
    scheduler->running = 0;
    printf("Scheduler shutdown complete\n");
}

// Node management
int scheduler_add_node(scheduler_t* scheduler, const char* node_name, 
                       int cpu_millicores, long memory_bytes) {
    if (!scheduler || !node_name) return -1;
    
    pthread_mutex_lock(&scheduler->lock);
    
    // Expand if needed
    if (scheduler->node_count >= scheduler->node_capacity) {
        scheduler->node_capacity *= 2;
        scheduler->nodes = realloc(scheduler->nodes, 
                                  sizeof(node_state_t) * scheduler->node_capacity);
    }
    
    node_state_t* node = &scheduler->nodes[scheduler->node_count];
    memset(node, 0, sizeof(node_state_t));
    
    node->node_name = strdup(node_name);
    node->status = strdup("Ready");
    node->allocatable_cpus = cpu_millicores;
    node->allocatable_memory = memory_bytes;
    node->allocated_cpus = 0;
    node->allocated_memory = 0;
    node->pod_count = 0;
    node->max_pods = 110;  // Kubernetes default
    node->last_heartbeat = time(NULL);
    
    scheduler->node_count++;
    
    pthread_mutex_unlock(&scheduler->lock);
    
    printf("Node '%s' added (CPU: %d, Memory: %ldB)\n", 
           node_name, cpu_millicores, memory_bytes);
    
    return 0;
}

int scheduler_update_node_status(scheduler_t* scheduler, const char* node_name, 
                                  const char* status) {
    if (!scheduler || !node_name || !status) return -1;
    
    pthread_mutex_lock(&scheduler->lock);
    
    for (int i = 0; i < scheduler->node_count; i++) {
        if (strcmp(scheduler->nodes[i].node_name, node_name) == 0) {
            free(scheduler->nodes[i].status);
            scheduler->nodes[i].status = strdup(status);
            scheduler->nodes[i].last_heartbeat = time(NULL);
            pthread_mutex_unlock(&scheduler->lock);
            printf("Node '%s' status updated to '%s'\n", node_name, status);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&scheduler->lock);
    return -1;
}

int scheduler_add_node_label(scheduler_t* scheduler, const char* node_name, 
                             const char* key, const char* value) {
    if (!scheduler || !node_name || !key || !value) return -1;
    
    pthread_mutex_lock(&scheduler->lock);
    
    for (int i = 0; i < scheduler->node_count; i++) {
        if (strcmp(scheduler->nodes[i].node_name, node_name) == 0) {
            node_state_t* node = &scheduler->nodes[i];
            
            // Expand label arrays
            node->label_keys = realloc(node->label_keys, 
                                      sizeof(char*) * (node->label_count + 1));
            node->label_values = realloc(node->label_values, 
                                        sizeof(char*) * (node->label_count + 1));
            
            node->label_keys[node->label_count] = strdup(key);
            node->label_values[node->label_count] = strdup(value);
            node->label_count++;
            
            pthread_mutex_unlock(&scheduler->lock);
            printf("Label %s=%s added to node '%s'\n", key, value, node_name);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&scheduler->lock);
    return -1;
}

int scheduler_add_node_taint(scheduler_t* scheduler, const char* node_name, 
                            const char* taint) {
    if (!scheduler || !node_name || !taint) return -1;
    
    pthread_mutex_lock(&scheduler->lock);
    
    for (int i = 0; i < scheduler->node_count; i++) {
        if (strcmp(scheduler->nodes[i].node_name, node_name) == 0) {
            node_state_t* node = &scheduler->nodes[i];
            
            node->taints = realloc(node->taints, 
                                  sizeof(char*) * (node->taint_count + 1));
            node->taints[node->taint_count] = strdup(taint);
            node->taint_count++;
            
            pthread_mutex_unlock(&scheduler->lock);
            printf("Taint '%s' added to node '%s'\n", taint, node_name);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&scheduler->lock);
    return -1;
}

int scheduler_remove_node_taint(scheduler_t* scheduler, const char* node_name, 
                               const char* taint) {
    if (!scheduler || !node_name || !taint) return -1;
    
    pthread_mutex_lock(&scheduler->lock);
    
    for (int i = 0; i < scheduler->node_count; i++) {
        if (strcmp(scheduler->nodes[i].node_name, node_name) == 0) {
            node_state_t* node = &scheduler->nodes[i];
            
            for (int j = 0; j < node->taint_count; j++) {
                if (strcmp(node->taints[j], taint) == 0) {
                    free(node->taints[j]);
                    // Shift remaining taints
                    for (int k = j; k < node->taint_count - 1; k++) {
                        node->taints[k] = node->taints[k + 1];
                    }
                    node->taint_count--;
                    pthread_mutex_unlock(&scheduler->lock);
                    printf("Taint '%s' removed from node '%s'\n", taint, node_name);
                    return 0;
                }
            }
        }
    }
    
    pthread_mutex_unlock(&scheduler->lock);
    return -1;
}

int scheduler_cordon_node(scheduler_t* scheduler, const char* node_name) {
    return scheduler_update_node_status(scheduler, node_name, "Cordoned");
}

int scheduler_uncordon_node(scheduler_t* scheduler, const char* node_name) {
    return scheduler_update_node_status(scheduler, node_name, "Ready");
}

int scheduler_drain_node(scheduler_t* scheduler, const char* node_name) {
    if (!scheduler || !node_name) return -1;
    
    pthread_mutex_lock(&scheduler->lock);
    
    for (int i = 0; i < scheduler->node_count; i++) {
        if (strcmp(scheduler->nodes[i].node_name, node_name) == 0) {
            // Mark as Draining
            free(scheduler->nodes[i].status);
            scheduler->nodes[i].status = strdup("Draining");
            
            // Find and evict all pods on this node
            int evicted = 0;
            for (int j = 0; j < scheduler->assignment_count; j++) {
                if (strcmp(scheduler->assigned_nodes[j], node_name) == 0) {
                    printf("Evicting pod '%s' from draining node '%s'\n", 
                           scheduler->assigned_pods[j], node_name);
                    evicted++;
                    // In real implementation, would evict pods
                }
            }
            
            pthread_mutex_unlock(&scheduler->lock);
            printf("Node '%s' drained (%d pods evicted)\n", node_name, evicted);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&scheduler->lock);
    return -1;
}

// Pod scheduling
int scheduler_schedule_pod(scheduler_t* scheduler, pod_spec_t* pod) {
    if (!scheduler || !pod) return -1;
    
    // Find best node
    char* best_node = scheduler_find_best_node(scheduler, pod);
    if (!best_node) {
        printf("No suitable node found for pod '%s'\n", pod->pod_name);
        return -1;
    }
    
    // Bind pod to node
    return scheduler_bind_pod(scheduler, pod->pod_name, best_node);
}

char* scheduler_find_best_node(scheduler_t* scheduler, pod_spec_t* pod) {
    if (!scheduler || !pod) return NULL;
    
    pthread_mutex_lock(&scheduler->lock);
    
    int best_score = -1;
    char* best_node = NULL;
    
    // Score all nodes
    for (int i = 0; i < scheduler->node_count; i++) {
        node_state_t* node = &scheduler->nodes[i];
        
        // Skip cordoned/draining nodes
        if (strcmp(node->status, "Ready") != 0) {
            continue;
        }
        
        // Check resource fit
        if (!scheduler_check_resource_fit(scheduler, node->node_name, &pod->request)) {
            continue;
        }
        
        // Check affinity/toleration
        if (!scheduler_check_toleration_match(scheduler, node->node_name, pod)) {
            continue;
        }
        
        // Score this node
        int score = scoring_combined(scheduler, node, pod);
        
        if (score > best_score) {
            best_score = score;
            best_node = node->node_name;
        }
    }
    
    pthread_mutex_unlock(&scheduler->lock);
    
    return best_node;
}

node_score_t* scheduler_score_nodes(scheduler_t* scheduler, pod_spec_t* pod, 
                                    int* score_count) {
    if (!scheduler || !pod || !score_count) return NULL;
    
    pthread_mutex_lock(&scheduler->lock);
    
    node_score_t* scores = (node_score_t*)malloc(sizeof(node_score_t) * scheduler->node_count);
    *score_count = 0;
    
    for (int i = 0; i < scheduler->node_count; i++) {
        node_state_t* node = &scheduler->nodes[i];
        
        node_score_t* score = &scores[*score_count];
        score->node_name = node->node_name;
        score->feasible = 1;
        score->infeasibility_reason = NULL;
        
        // Check feasibility
        if (strcmp(node->status, "Ready") != 0) {
            score->feasible = 0;
            score->infeasibility_reason = "Node not ready";
        } else if (!scheduler_check_resource_fit(scheduler, node->node_name, &pod->request)) {
            score->feasible = 0;
            score->infeasibility_reason = "Insufficient resources";
        } else if (!scheduler_check_toleration_match(scheduler, node->node_name, pod)) {
            score->feasible = 0;
            score->infeasibility_reason = "Taint not tolerated";
        }
        
        // Score if feasible
        if (score->feasible) {
            score->score = scoring_combined(scheduler, node, pod);
        } else {
            score->score = 0;
        }
        
        (*score_count)++;
    }
    
    pthread_mutex_unlock(&scheduler->lock);
    return scores;
}

int scheduler_bind_pod(scheduler_t* scheduler, const char* pod_name, 
                       const char* node_name) {
    if (!scheduler || !pod_name || !node_name) return -1;
    
    pthread_mutex_lock(&scheduler->lock);
    
    // Check if we have capacity for another pod assignment
    if (!scheduler->assignment_capacity) {
        scheduler->assignment_capacity = 5000;
    }
    if (scheduler->assignment_count >= scheduler->assignment_capacity) {
        pthread_mutex_unlock(&scheduler->lock);
        printf("Error: Pod assignment capacity exceeded (%d)", scheduler->assignment_capacity);
        return -1;
    }
    
    // Find node and update resources
    for (int i = 0; i < scheduler->node_count; i++) {
        if (strcmp(scheduler->nodes[i].node_name, node_name) == 0) {
            // In real scheduler, would fetch pod spec and update resources
            // For now, just track assignment
            
            scheduler->assigned_pods[scheduler->assignment_count] = strdup(pod_name);
            scheduler->assigned_nodes[scheduler->assignment_count] = strdup(node_name);
            scheduler->assignment_count++;
            
            scheduler->nodes[i].pod_count++;
            
            pthread_mutex_unlock(&scheduler->lock);
            printf("Pod '%s' bound to node '%s'\n", pod_name, node_name);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&scheduler->lock);
    return -1;
}

int scheduler_evict_pod(scheduler_t* scheduler, const char* pod_name) {
    if (!scheduler || !pod_name) return -1;
    
    pthread_mutex_lock(&scheduler->lock);
    
    for (int i = 0; i < scheduler->assignment_count; i++) {
        if (strcmp(scheduler->assigned_pods[i], pod_name) == 0) {
            char* node_name = scheduler->assigned_nodes[i];
            
            // Find node and update resource counts
            for (int j = 0; j < scheduler->node_count; j++) {
                if (strcmp(scheduler->nodes[j].node_name, node_name) == 0) {
                    scheduler->nodes[j].pod_count--;
                    break;
                }
            }
            
            free(scheduler->assigned_pods[i]);
            free(scheduler->assigned_nodes[i]);
            
            // Shift remaining assignments
            for (int j = i; j < scheduler->assignment_count - 1; j++) {
                scheduler->assigned_pods[j] = scheduler->assigned_pods[j + 1];
                scheduler->assigned_nodes[j] = scheduler->assigned_nodes[j + 1];
            }
            scheduler->assignment_count--;
            
            pthread_mutex_unlock(&scheduler->lock);
            printf("Pod '%s' evicted from node '%s'\n", pod_name, node_name);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&scheduler->lock);
    return -1;
}

// Node queries
node_state_t* scheduler_get_node(scheduler_t* scheduler, const char* node_name) {
    if (!scheduler || !node_name) return NULL;
    
    for (int i = 0; i < scheduler->node_count; i++) {
        if (strcmp(scheduler->nodes[i].node_name, node_name) == 0) {
            return &scheduler->nodes[i];
        }
    }
    
    return NULL;
}

int scheduler_get_node_utilization(scheduler_t* scheduler, const char* node_name, 
                                   int* cpu_percent, long* memory_percent) {
    if (!scheduler || !node_name) return -1;
    
    node_state_t* node = scheduler_get_node(scheduler, node_name);
    if (!node) return -1;
    
    if (cpu_percent) {
        *cpu_percent = (node->allocated_cpus * 100) / node->allocatable_cpus;
    }
    if (memory_percent) {
        *memory_percent = (node->allocated_memory * 100) / node->allocatable_memory;
    }
    
    return 0;
}

node_state_t* scheduler_get_all_nodes(scheduler_t* scheduler, int* count) {
    if (!scheduler || !count) return NULL;
    *count = scheduler->node_count;
    return scheduler->nodes;
}

// Resource checking
int scheduler_check_resource_fit(scheduler_t* scheduler, const char* node_name, 
                                 pod_request_t* request) {
    if (!scheduler || !node_name || !request) return 0;
    
    node_state_t* node = scheduler_get_node(scheduler, node_name);
    if (!node) return 0;
    
    // Check CPU
    if (node->allocated_cpus + request->cpu_millicores > node->allocatable_cpus) {
        return 0;
    }
    
    // Check memory
    if (node->allocated_memory + request->memory_bytes > node->allocatable_memory) {
        return 0;
    }
    
    // Check pod count
    if (node->pod_count >= node->max_pods) {
        return 0;
    }
    
    return 1;
}

// Affinity/toleration checking
int scheduler_check_affinity_match(scheduler_t* scheduler, const char* node_name, 
                                   pod_spec_t* pod) {
    if (!scheduler || !node_name || !pod) return 1;  // Default: no affinity = match
    
    // Check required node affinity
    for (int i = 0; i < pod->required_count; i++) {
        int found = 0;
        node_state_t* node = scheduler_get_node(scheduler, node_name);
        if (!node) return 0;
        
        for (int j = 0; j < node->label_count; j++) {
            if (strcmp(node->label_keys[j], pod->required_node_labels[i]) == 0) {
                found = 1;
                break;
            }
        }
        
        if (!found) return 0;  // Required label not found
    }
    
    return 1;
}

int scheduler_check_toleration_match(scheduler_t* scheduler, const char* node_name, 
                                     pod_spec_t* pod) {
    if (!scheduler || !node_name || !pod) return 1;  // Default: no taints = match
    
    node_state_t* node = scheduler_get_node(scheduler, node_name);
    if (!node) return 0;
    
    // For each taint on the node, check if pod tolerates it
    for (int i = 0; i < node->taint_count; i++) {
        int tolerated = 0;
        
        for (int j = 0; j < pod->toleration_count; j++) {
            if (strcmp(node->taints[i], pod->tolerated_taints[j]) == 0) {
                tolerated = 1;
                break;
            }
        }
        
        if (!tolerated) return 0;  // Taint not tolerated
    }
    
    return 1;
}

// Main scheduler loop
int scheduler_run(scheduler_t* scheduler) {
    if (!scheduler) return -1;
    
    scheduler->running = 1;
    printf("Scheduler main loop started\n");
    
    while (scheduler->running) {
        // In real implementation, would:
        // 1. Watch for unscheduled pods
        // 2. Schedule them
        // 3. Watch for node changes
        // 4. Handle preemption, rescheduling, etc.
        
        sleep(5);  // Poll every 5 seconds
    }
    
    return 0;
}
