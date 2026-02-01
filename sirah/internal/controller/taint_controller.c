#include "taint_controller.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// Global controller instance
static k8s_taint_controller_t* g_taint_controller = NULL;

// ============ Lifecycle ============

k8s_taint_controller_t* k8s_taint_controller_new(int max_taints, int max_nodes) {
    k8s_taint_controller_t* ctrl = calloc(1, sizeof(k8s_taint_controller_t));
    if (!ctrl) return NULL;
    
    ctrl->max_taints = max_taints > 0 ? max_taints : 1000;
    ctrl->max_nodes = max_nodes > 0 ? max_nodes : 100;
    
    ctrl->taints = calloc(ctrl->max_taints, sizeof(k8s_taint_t*));
    ctrl->taint_nodes = calloc(ctrl->max_taints, sizeof(char*));
    
    ctrl->nodes = calloc(ctrl->max_nodes, sizeof(char*));
    ctrl->node_taints = calloc(ctrl->max_nodes, sizeof(k8s_taint_t**));
    ctrl->node_num_taints = calloc(ctrl->max_nodes, sizeof(int));
    
    if (!ctrl->taints || !ctrl->taint_nodes || !ctrl->nodes || 
        !ctrl->node_taints || !ctrl->node_num_taints) {
        k8s_taint_controller_free(ctrl);
        return NULL;
    }
    
    g_taint_controller = ctrl;
    return ctrl;
}

void k8s_taint_controller_free(k8s_taint_controller_t* ctrl) {
    if (!ctrl) return;
    
    for (int i = 0; i < ctrl->num_taints; i++) {
        if (ctrl->taints[i]) k8s_taint_free(ctrl->taints[i]);
        if (ctrl->taint_nodes[i]) free(ctrl->taint_nodes[i]);
    }
    
    for (int i = 0; i < ctrl->num_nodes; i++) {
        if (ctrl->nodes[i]) free(ctrl->nodes[i]);
        if (ctrl->node_taints[i]) {
            for (int j = 0; j < ctrl->node_num_taints[i]; j++) {
                if (ctrl->node_taints[i][j]) k8s_taint_free(ctrl->node_taints[i][j]);
            }
            free(ctrl->node_taints[i]);
        }
    }
    
    if (ctrl->taints) free(ctrl->taints);
    if (ctrl->taint_nodes) free(ctrl->taint_nodes);
    if (ctrl->nodes) free(ctrl->nodes);
    if (ctrl->node_taints) free(ctrl->node_taints);
    if (ctrl->node_num_taints) free(ctrl->node_num_taints);
    
    free(ctrl);
    
    if (g_taint_controller == ctrl) {
        g_taint_controller = NULL;
    }
}

k8s_taint_controller_t* k8s_taint_controller_global() {
    if (!g_taint_controller) {
        g_taint_controller = k8s_taint_controller_new(1000, 100);
    }
    return g_taint_controller;
}

// ============ Node Management ============

int k8s_taint_controller_register_node(k8s_taint_controller_t* ctrl, const char* node_name) {
    if (!ctrl || !node_name) return -1;
    if (ctrl->num_nodes >= ctrl->max_nodes) return -1;
    
    ctrl->nodes[ctrl->num_nodes] = strdup(node_name);
    ctrl->node_taints[ctrl->num_nodes] = calloc(10, sizeof(k8s_taint_t*));
    ctrl->node_num_taints[ctrl->num_nodes] = 0;
    ctrl->num_nodes++;
    
    return 0;
}

int k8s_taint_controller_unregister_node(k8s_taint_controller_t* ctrl, const char* node_name) {
    if (!ctrl || !node_name) return -1;
    
    for (int i = 0; i < ctrl->num_nodes; i++) {
        if (strcmp(ctrl->nodes[i], node_name) == 0) {
            free(ctrl->nodes[i]);
            if (ctrl->node_taints[i]) free(ctrl->node_taints[i]);
            
            // Shift remaining nodes
            for (int j = i; j < ctrl->num_nodes - 1; j++) {
                ctrl->nodes[j] = ctrl->nodes[j + 1];
                ctrl->node_taints[j] = ctrl->node_taints[j + 1];
                ctrl->node_num_taints[j] = ctrl->node_num_taints[j + 1];
            }
            ctrl->num_nodes--;
            return 0;
        }
    }
    return -1;  // Node not found
}

// ============ Taint Operations ============

int k8s_taint_controller_add_taint(k8s_taint_controller_t* ctrl, 
                                   const char* node_name,
                                   k8s_taint_t* taint) {
    if (!ctrl || !node_name || !taint) return -1;
    
    for (int i = 0; i < ctrl->num_nodes; i++) {
        if (strcmp(ctrl->nodes[i], node_name) == 0) {
            ctrl->node_taints[i][ctrl->node_num_taints[i]] = taint;
            ctrl->node_num_taints[i]++;
            return 0;
        }
    }
    return -1;  // Node not found
}

int k8s_taint_controller_remove_taint(k8s_taint_controller_t* ctrl,
                                     const char* node_name,
                                     const char* taint_key) {
    if (!ctrl || !node_name || !taint_key) return -1;
    
    for (int i = 0; i < ctrl->num_nodes; i++) {
        if (strcmp(ctrl->nodes[i], node_name) == 0) {
            for (int j = 0; j < ctrl->node_num_taints[i]; j++) {
                if (strcmp(ctrl->node_taints[i][j]->key, taint_key) == 0) {
                    k8s_taint_free(ctrl->node_taints[i][j]);
                    
                    // Shift remaining taints
                    for (int k = j; k < ctrl->node_num_taints[i] - 1; k++) {
                        ctrl->node_taints[i][k] = ctrl->node_taints[i][k + 1];
                    }
                    ctrl->node_num_taints[i]--;
                    return 0;
                }
            }
        }
    }
    return -1;  // Not found
}

int k8s_taint_controller_remove_taint_by_effect(k8s_taint_controller_t* ctrl,
                                                const char* node_name,
                                                const char* effect) {
    if (!ctrl || !node_name || !effect) return -1;
    
    for (int i = 0; i < ctrl->num_nodes; i++) {
        if (strcmp(ctrl->nodes[i], node_name) == 0) {
            for (int j = 0; j < ctrl->node_num_taints[i]; j++) {
                if (strcmp(ctrl->node_taints[i][j]->effect, effect) == 0) {
                    k8s_taint_free(ctrl->node_taints[i][j]);
                    
                    // Shift remaining taints
                    for (int k = j; k < ctrl->node_num_taints[i] - 1; k++) {
                        ctrl->node_taints[i][k] = ctrl->node_taints[i][k + 1];
                    }
                    ctrl->node_num_taints[i]--;
                    return 0;
                }
            }
        }
    }
    return -1;  // Not found
}

// ============ Query Taints ============

k8s_taint_t** k8s_taint_controller_get_taints(k8s_taint_controller_t* ctrl,
                                              const char* node_name,
                                              int* count) {
    if (!ctrl || !node_name || !count) return NULL;
    
    for (int i = 0; i < ctrl->num_nodes; i++) {
        if (strcmp(ctrl->nodes[i], node_name) == 0) {
            *count = ctrl->node_num_taints[i];
            return ctrl->node_taints[i];
        }
    }
    *count = 0;
    return NULL;
}

k8s_taint_t* k8s_taint_controller_get_taint(k8s_taint_controller_t* ctrl,
                                            const char* node_name,
                                            const char* taint_key) {
    if (!ctrl || !node_name || !taint_key) return NULL;
    
    for (int i = 0; i < ctrl->num_nodes; i++) {
        if (strcmp(ctrl->nodes[i], node_name) == 0) {
            for (int j = 0; j < ctrl->node_num_taints[i]; j++) {
                if (strcmp(ctrl->node_taints[i][j]->key, taint_key) == 0) {
                    return ctrl->node_taints[i][j];
                }
            }
        }
    }
    return NULL;  // Not found
}

// ============ Apply Taints ============

int k8s_taint_controller_apply_taints(k8s_taint_controller_t* ctrl, k8s_node_t* node) {
    if (!ctrl || !node) return -1;
    
    // Stub implementation - would apply taints to node in real system
    (void)ctrl;
    (void)node;
    return 0;
}

// ============ Effect Helpers ============

bool k8s_taint_controller_has_no_schedule_effect(k8s_taint_t* taint) {
    if (!taint) return false;
    return strcmp(taint->effect, "NoSchedule") == 0;
}

bool k8s_taint_controller_has_no_execute_effect(k8s_taint_t* taint) {
    if (!taint) return false;
    return strcmp(taint->effect, "NoExecute") == 0;
}

bool k8s_taint_controller_has_prefer_no_schedule_effect(k8s_taint_t* taint) {
    if (!taint) return false;
    return strcmp(taint->effect, "PreferNoSchedule") == 0;
}

// ============ Control ============

void k8s_taint_controller_set_enabled(k8s_taint_controller_t* ctrl, bool enabled) {
    // Stub - enabled/disabled tracking could be added to struct if needed
    (void)ctrl;
    (void)enabled;
}

bool k8s_taint_controller_is_enabled(k8s_taint_controller_t* ctrl) {
    // Stub - for now always enabled
    (void)ctrl;
    return true;
}
