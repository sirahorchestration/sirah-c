#ifndef K8S_HPA_CONTROLLER_H
#define K8S_HPA_CONTROLLER_H

#include <types/hpa.h>
#include "deployment.h"
#include <stdbool.h>

typedef struct {
    int max_hpas;
    
    // HPA storage
    k8s_hpa_t** hpas;
    int num_hpas;
    
    // Scaling history for cooldown tracking
    char** scaled_targets;
    int* scale_counts;
    int* scale_timestamps;
    int num_scaled_targets;
    
    bool enabled;
} k8s_hpa_controller_t;

// Lifecycle
k8s_hpa_controller_t* k8s_hpa_controller_new(int max_hpas);
void k8s_hpa_controller_free(k8s_hpa_controller_t* controller);

// CRUD operations
int k8s_hpa_controller_create(k8s_hpa_controller_t* controller, k8s_hpa_t* hpa);
k8s_hpa_t* k8s_hpa_controller_get(k8s_hpa_controller_t* controller, const char* name, const char* namespace);
int k8s_hpa_controller_update(k8s_hpa_controller_t* controller, k8s_hpa_t* hpa);
int k8s_hpa_controller_delete(k8s_hpa_controller_t* controller, const char* name, const char* namespace);
k8s_hpa_t** k8s_hpa_controller_list(k8s_hpa_controller_t* controller, const char* namespace, int* count);

// Evaluation and scaling
// Evaluates all HPAs and returns list of targets that need scaling
int k8s_hpa_controller_evaluate_all(
    k8s_hpa_controller_t* controller,
    char*** targets,
    int** desired_replicas,
    int* num_scaling_decisions
);

// Scale a specific target (Deployment, StatefulSet, etc.)
int k8s_hpa_controller_scale_target(k8s_hpa_controller_t* controller, const char* target, int replicas);

// Update current metrics for a target
int k8s_hpa_controller_update_metrics(
    k8s_hpa_controller_t* controller,
    const char* target,
    int cpu_utilization,
    int memory_utilization
);

// Check if target can be scaled based on cooldown
bool k8s_hpa_controller_can_scale_now(k8s_hpa_controller_t* controller, const char* target);

// Get scaling recommendations
int k8s_hpa_controller_get_recommendations(
    k8s_hpa_controller_t* controller,
    const char* target,
    int* recommended_replicas
);

// Control
void k8s_hpa_controller_set_enabled(k8s_hpa_controller_t* controller, bool enabled);
bool k8s_hpa_controller_is_enabled(k8s_hpa_controller_t* controller);

// Global instance
k8s_hpa_controller_t* k8s_hpa_controller_global();

#endif
