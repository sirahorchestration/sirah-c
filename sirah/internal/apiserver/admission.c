#include "admission.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============ Initialization ============

k8s_admission_controller_t* k8s_admission_controller_new() {
    k8s_admission_controller_t* ctrl = calloc(1, sizeof(k8s_admission_controller_t));
    if (!ctrl) return NULL;
    
    ctrl->enabled = true;
    return ctrl;
}

void k8s_admission_controller_free(k8s_admission_controller_t* ctrl) {
    if (!ctrl) return;
    
    for (int i = 0; i < ctrl->num_mutating; i++) {
        if (ctrl->mutating_configs[i]) {
            k8s_mutating_webhook_config_free(ctrl->mutating_configs[i]);
        }
    }
    
    for (int i = 0; i < ctrl->num_validating; i++) {
        if (ctrl->validating_configs[i]) {
            k8s_validating_webhook_config_free(ctrl->validating_configs[i]);
        }
    }
    
    free(ctrl);
}

// ============ Webhook Registration ============

int k8s_admission_register_mutating_webhook(k8s_admission_controller_t* ctrl,
                                            k8s_mutating_webhook_config_t* config) {
    if (!ctrl || !config) return -1;
    if (ctrl->num_mutating >= K8S_MAX_MUTATING_WEBHOOKS) return -1;
    
    ctrl->mutating_configs[ctrl->num_mutating] = config;
    ctrl->num_mutating++;
    return 0;
}

int k8s_admission_register_validating_webhook(k8s_admission_controller_t* ctrl,
                                              k8s_validating_webhook_config_t* config) {
    if (!ctrl || !config) return -1;
    if (ctrl->num_validating >= K8S_MAX_VALIDATING_WEBHOOKS) return -1;
    
    ctrl->validating_configs[ctrl->num_validating] = config;
    ctrl->num_validating++;
    return 0;
}

int k8s_admission_unregister_mutating_webhook(k8s_admission_controller_t* ctrl, 
                                             const char* config_name) {
    if (!ctrl || !config_name) return -1;
    
    for (int i = 0; i < ctrl->num_mutating; i++) {
        if (ctrl->mutating_configs[i] && 
            strcmp(ctrl->mutating_configs[i]->metadata->name, config_name) == 0) {
            
            k8s_mutating_webhook_config_free(ctrl->mutating_configs[i]);
            
            // Shift remaining configs
            for (int j = i; j < ctrl->num_mutating - 1; j++) {
                ctrl->mutating_configs[j] = ctrl->mutating_configs[j + 1];
            }
            ctrl->num_mutating--;
            return 0;
        }
    }
    return -1;  // Not found
}

int k8s_admission_unregister_validating_webhook(k8s_admission_controller_t* ctrl,
                                               const char* config_name) {
    if (!ctrl || !config_name) return -1;
    
    for (int i = 0; i < ctrl->num_validating; i++) {
        if (ctrl->validating_configs[i] && 
            strcmp(ctrl->validating_configs[i]->metadata->name, config_name) == 0) {
            
            k8s_validating_webhook_config_free(ctrl->validating_configs[i]);
            
            // Shift remaining configs
            for (int j = i; j < ctrl->num_validating - 1; j++) {
                ctrl->validating_configs[j] = ctrl->validating_configs[j + 1];
            }
            ctrl->num_validating--;
            return 0;
        }
    }
    return -1;  // Not found
}

// ============ Admission Flow ============

int k8s_admission_mutate(k8s_admission_controller_t* ctrl,
                        const char* kind,
                        const char* api_version,
                        const char* operation,
                        const char* namespace,
                        const char* name,
                        const char* object_json,
                        char* patched_json,
                        size_t patched_size) {
    if (!ctrl || !object_json || !patched_json) return -1;
    
    // For now, return object unchanged (webhook integration stubbed)
    (void)kind;
    (void)api_version;
    (void)operation;
    (void)namespace;
    (void)name;
    
    int len = strlen(object_json);
    if (len >= (int)patched_size) return -1;
    
    strncpy(patched_json, object_json, patched_size - 1);
    patched_json[patched_size - 1] = '\0';
    
    return 0;
}

int k8s_admission_validate(k8s_admission_controller_t* ctrl,
                          const char* kind,
                          const char* api_version,
                          const char* operation,
                          const char* namespace,
                          const char* name,
                          const char* object_json,
                          char* error_message,
                          size_t error_size) {
    if (!ctrl || !object_json) return -1;
    
    // For now, allow all objects (webhook integration stubbed)
    (void)kind;
    (void)api_version;
    (void)operation;
    (void)namespace;
    (void)name;
    
    if (error_message && error_size > 0) {
        error_message[0] = '\0';
    }
    
    return 0;
}

// ============ Control ============

void k8s_admission_set_enabled(k8s_admission_controller_t* ctrl, bool enabled) {
    if (ctrl) {
        ctrl->enabled = enabled;
    }
}

bool k8s_admission_is_enabled(k8s_admission_controller_t* ctrl) {
    if (!ctrl) return false;
    return ctrl->enabled;
}
