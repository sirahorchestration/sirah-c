#include "hpa_controller.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static k8s_hpa_controller_t* g_hpa_controller = NULL;

// ============ Lifecycle ============

k8s_hpa_controller_t* k8s_hpa_controller_new(int max_hpas) {
    if (max_hpas <= 0) return NULL;
    
    k8s_hpa_controller_t* controller = (k8s_hpa_controller_t*)malloc(sizeof(k8s_hpa_controller_t));
    if (!controller) return NULL;
    
    controller->hpas = (k8s_hpa_t**)malloc(max_hpas * sizeof(k8s_hpa_t*));
    if (!controller->hpas) { free(controller); return NULL; }
    
    controller->scaled_targets = (char**)malloc(max_hpas * sizeof(char*));
    if (!controller->scaled_targets) { free(controller->hpas); free(controller); return NULL; }
    
    controller->scale_counts = (int*)malloc(max_hpas * sizeof(int));
    if (!controller->scale_counts) { free(controller->scaled_targets); free(controller->hpas); free(controller); return NULL; }
    
    controller->scale_timestamps = (int*)malloc(max_hpas * sizeof(int));
    if (!controller->scale_timestamps) {
        free(controller->scale_counts);
        free(controller->scaled_targets);
        free(controller->hpas);
        free(controller);
        return NULL;
    }
    
    controller->max_hpas = max_hpas;
    controller->num_hpas = 0;
    controller->num_scaled_targets = 0;
    controller->enabled = true;
    
    return controller;
}

void k8s_hpa_controller_free(k8s_hpa_controller_t* controller) {
    if (!controller) return;
    
    if (controller->hpas) {
        for (int i = 0; i < controller->num_hpas; i++) {
            k8s_hpa_free(controller->hpas[i]);
        }
        free(controller->hpas);
    }
    
    if (controller->scaled_targets) {
        for (int i = 0; i < controller->num_scaled_targets; i++) {
            free(controller->scaled_targets[i]);
        }
        free(controller->scaled_targets);
    }
    
    free(controller->scale_counts);
    free(controller->scale_timestamps);
    free(controller);
}

// ============ CRUD Operations ============

int k8s_hpa_controller_create(k8s_hpa_controller_t* controller, k8s_hpa_t* hpa) {
    if (!controller || !hpa || controller->num_hpas >= controller->max_hpas) return -1;
    
    // Check for duplicates
    for (int i = 0; i < controller->num_hpas; i++) {
        if (strcmp(controller->hpas[i]->metadata.name, hpa->metadata.name) == 0 &&
            strcmp(controller->hpas[i]->metadata.namespace, hpa->metadata.namespace) == 0) {
            return -1;  // Already exists
        }
    }
    
    controller->hpas[controller->num_hpas] = hpa;
    controller->num_hpas++;
    
    return 0;
}

k8s_hpa_t* k8s_hpa_controller_get(k8s_hpa_controller_t* controller, const char* name, const char* namespace) {
    if (!controller || !name || !namespace) return NULL;
    
    for (int i = 0; i < controller->num_hpas; i++) {
        if (strcmp(controller->hpas[i]->metadata.name, name) == 0 &&
            strcmp(controller->hpas[i]->metadata.namespace, namespace) == 0) {
            return controller->hpas[i];
        }
    }
    
    return NULL;
}

int k8s_hpa_controller_update(k8s_hpa_controller_t* controller, k8s_hpa_t* hpa) {
    if (!controller || !hpa) return -1;
    
    for (int i = 0; i < controller->num_hpas; i++) {
        if (strcmp(controller->hpas[i]->metadata.name, hpa->metadata.name) == 0 &&
            strcmp(controller->hpas[i]->metadata.namespace, hpa->metadata.namespace) == 0) {
            // Free old HPA and update
            k8s_hpa_free(controller->hpas[i]);
            controller->hpas[i] = hpa;
            return 0;
        }
    }
    
    return -1;  // Not found
}

int k8s_hpa_controller_delete(k8s_hpa_controller_t* controller, const char* name, const char* namespace) {
    if (!controller || !name || !namespace) return -1;
    
    for (int i = 0; i < controller->num_hpas; i++) {
        if (strcmp(controller->hpas[i]->metadata.name, name) == 0 &&
            strcmp(controller->hpas[i]->metadata.namespace, namespace) == 0) {
            
            k8s_hpa_free(controller->hpas[i]);
            
            // Shift remaining HPAs
            for (int j = i; j < controller->num_hpas - 1; j++) {
                controller->hpas[j] = controller->hpas[j + 1];
            }
            controller->num_hpas--;
            return 0;
        }
    }
    
    return -1;
}

k8s_hpa_t** k8s_hpa_controller_list(k8s_hpa_controller_t* controller, const char* namespace, int* count) {
    if (!controller || !namespace || !count) return NULL;
    
    // Count HPAs in namespace
    int ns_count = 0;
    for (int i = 0; i < controller->num_hpas; i++) {
        if (strcmp(controller->hpas[i]->metadata.namespace, namespace) == 0) {
            ns_count++;
        }
    }
    
    if (ns_count == 0) {
        *count = 0;
        return NULL;
    }
    
    // Allocate and copy
    k8s_hpa_t** result = (k8s_hpa_t**)malloc(ns_count * sizeof(k8s_hpa_t*));
    if (!result) return NULL;
    
    int idx = 0;
    for (int i = 0; i < controller->num_hpas; i++) {
        if (strcmp(controller->hpas[i]->metadata.namespace, namespace) == 0) {
            result[idx++] = controller->hpas[i];
        }
    }
    
    *count = ns_count;
    return result;
}

// ============ Evaluation and Scaling ============

int k8s_hpa_controller_evaluate_all(
    k8s_hpa_controller_t* controller,
    char*** targets,
    int** desired_replicas,
    int* num_scaling_decisions) {
    
    if (!controller || !targets || !desired_replicas || !num_scaling_decisions) return -1;
    if (controller->num_hpas == 0) {
        *num_scaling_decisions = 0;
        return 0;
    }
    
    *targets = (char**)malloc(controller->num_hpas * sizeof(char*));
    *desired_replicas = (int*)malloc(controller->num_hpas * sizeof(int));
    if (!(*targets) || !(*desired_replicas)) {
        free(*targets);
        free(*desired_replicas);
        return -1;
    }
    
    int decisions = 0;
    time_t now = time(NULL);
    
    // Evaluate each HPA
    for (int i = 0; i < controller->num_hpas; i++) {
        k8s_hpa_t* hpa = controller->hpas[i];
        if (!hpa->spec || !hpa->status) continue;
        
        // Check if enough time has passed since last scale
        int elapsed = (int)(now - hpa->status->last_scale_time);
        int min_window = (hpa->status->desired_replicas > hpa->status->current_replicas) ?
                        hpa->spec->upscale_window_seconds :
                        hpa->spec->downscale_window_seconds;
        
        if (elapsed < min_window) continue;
        
        // Calculate desired replicas based on current utilization
        int current_util = hpa->status->current_cpu_utilization;
        int target_util = hpa->spec->target_cpu_utilization;
        int desired = k8s_hpa_calculate_desired_replicas(hpa, current_util, target_util);
        
        if (desired != hpa->status->current_replicas) {
            (*targets)[decisions] = (char*)malloc(strlen(hpa->spec->scale_target_ref) + 1);
            if ((*targets)[decisions]) {
                strcpy((*targets)[decisions], hpa->spec->scale_target_ref);
            }
            (*desired_replicas)[decisions] = desired;
            decisions++;
        }
    }
    
    *num_scaling_decisions = decisions;
    return 0;
}

int k8s_hpa_controller_scale_target(k8s_hpa_controller_t* controller, const char* target, int replicas) {
    if (!controller || !target || replicas < 1) return -1;
    if (!controller->enabled) return -1;
    
    // Find HPA for this target
    for (int i = 0; i < controller->num_hpas; i++) {
        if (strcmp(controller->hpas[i]->spec->scale_target_ref, target) == 0) {
            k8s_hpa_t* hpa = controller->hpas[i];
            
            // Check limits
            if (replicas < hpa->spec->min_replicas) replicas = hpa->spec->min_replicas;
            if (replicas > hpa->spec->max_replicas) replicas = hpa->spec->max_replicas;
            
            // Update status
            k8s_hpa_mark_scaled(hpa, replicas);
            
            // Record scaling event
            bool found_target = false;
            for (int j = 0; j < controller->num_scaled_targets; j++) {
                if (strcmp(controller->scaled_targets[j], target) == 0) {
                    controller->scale_counts[j]++;
                    controller->scale_timestamps[j] = (int)time(NULL);
                    found_target = true;
                    break;
                }
            }
            
            if (!found_target && controller->num_scaled_targets < controller->max_hpas) {
                controller->scaled_targets[controller->num_scaled_targets] = (char*)malloc(strlen(target) + 1);
                if (controller->scaled_targets[controller->num_scaled_targets]) {
                    strcpy(controller->scaled_targets[controller->num_scaled_targets], target);
                    controller->scale_counts[controller->num_scaled_targets] = 1;
                    controller->scale_timestamps[controller->num_scaled_targets] = (int)time(NULL);
                    controller->num_scaled_targets++;
                }
            }
            
            return 0;
        }
    }
    
    return -1;  // Target not found
}

int k8s_hpa_controller_update_metrics(
    k8s_hpa_controller_t* controller,
    const char* target,
    int cpu_utilization,
    int memory_utilization) {
    
    if (!controller || !target) return -1;
    
    // Update metrics for all HPAs targeting this workload
    for (int i = 0; i < controller->num_hpas; i++) {
        if (strcmp(controller->hpas[i]->spec->scale_target_ref, target) == 0) {
            k8s_hpa_update_current_metrics(controller->hpas[i], cpu_utilization, memory_utilization);
        }
    }
    
    return 0;
}

bool k8s_hpa_controller_can_scale_now(k8s_hpa_controller_t* controller, const char* target) {
    if (!controller || !target) return false;
    
    // Find scaling history for target
    for (int i = 0; i < controller->num_scaled_targets; i++) {
        if (strcmp(controller->scaled_targets[i], target) == 0) {
            time_t now = time(NULL);
            int elapsed = (int)(now - controller->scale_timestamps[i]);
            
            // Default cooldown: 60 seconds
            return elapsed > 60;
        }
    }
    
    // Not scaled before, can scale now
    return true;
}

int k8s_hpa_controller_get_recommendations(
    k8s_hpa_controller_t* controller,
    const char* target,
    int* recommended_replicas) {
    
    if (!controller || !target || !recommended_replicas) return -1;
    
    // Find HPA for target
    for (int i = 0; i < controller->num_hpas; i++) {
        if (strcmp(controller->hpas[i]->spec->scale_target_ref, target) == 0) {
            k8s_hpa_t* hpa = controller->hpas[i];
            
            // Calculate recommendation based on current metrics
            int current_util = hpa->status->current_cpu_utilization;
            int target_util = hpa->spec->target_cpu_utilization;
            int desired = k8s_hpa_calculate_desired_replicas(hpa, current_util, target_util);
            
            *recommended_replicas = desired;
            return 0;
        }
    }
    
    return -1;
}

// ============ Control ============

void k8s_hpa_controller_set_enabled(k8s_hpa_controller_t* controller, bool enabled) {
    if (!controller) return;
    controller->enabled = enabled;
}

bool k8s_hpa_controller_is_enabled(k8s_hpa_controller_t* controller) {
    if (!controller) return false;
    return controller->enabled;
}

// ============ Global Instance ============

k8s_hpa_controller_t* k8s_hpa_controller_global() {
    if (!g_hpa_controller) {
        g_hpa_controller = k8s_hpa_controller_new(5000);  // Support 5000 HPAs
    }
    return g_hpa_controller;
}
