#ifndef K8S_TAINT_CONTROLLER_H
#define K8S_TAINT_CONTROLLER_H

#include "../../pkg/types/affinity.h"
#include "../../pkg/types/node.h"

typedef struct {
    int max_taints;
    
    // Taint management
    k8s_taint_t** taints;
    char** taint_nodes;     // Node name for each taint
    int num_taints;
    
    // Node taint mapping
    char** nodes;
    k8s_taint_t*** node_taints;
    int* node_num_taints;
    int num_nodes;
    int max_nodes;
} k8s_taint_controller_t;

// Lifecycle
k8s_taint_controller_t* k8s_taint_controller_new(int max_taints, int max_nodes);
void k8s_taint_controller_free(k8s_taint_controller_t* controller);

// Node management
int k8s_taint_controller_register_node(k8s_taint_controller_t* controller, const char* node_name);
int k8s_taint_controller_unregister_node(k8s_taint_controller_t* controller, const char* node_name);

// Taint operations
int k8s_taint_controller_add_taint(k8s_taint_controller_t* controller, const char* node_name, k8s_taint_t* taint);
int k8s_taint_controller_remove_taint(k8s_taint_controller_t* controller, const char* node_name, const char* taint_key);
int k8s_taint_controller_remove_taint_by_effect(k8s_taint_controller_t* controller, const char* node_name, const char* effect);

// Query taints
k8s_taint_t** k8s_taint_controller_get_taints(k8s_taint_controller_t* controller, const char* node_name, int* count);
k8s_taint_t* k8s_taint_controller_get_taint(k8s_taint_controller_t* controller, const char* node_name, const char* taint_key);

// Apply taints to node
int k8s_taint_controller_apply_taints(k8s_taint_controller_t* controller, k8s_node_t* node);

// Effects enum helpers
bool k8s_taint_controller_has_no_schedule_effect(k8s_taint_t* taint);
bool k8s_taint_controller_has_no_execute_effect(k8s_taint_t* taint);
bool k8s_taint_controller_has_prefer_no_schedule_effect(k8s_taint_t* taint);

// Control
void k8s_taint_controller_set_enabled(k8s_taint_controller_t* controller, bool enabled);
bool k8s_taint_controller_is_enabled(k8s_taint_controller_t* controller);

// Global instance
k8s_taint_controller_t* k8s_taint_controller_global();

#endif
