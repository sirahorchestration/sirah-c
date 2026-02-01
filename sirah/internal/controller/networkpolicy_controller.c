#include "networkpolicy_controller.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Global controller instance
static k8s_networkpolicy_controller_t* g_networkpolicy_controller = NULL;

// ============ Initialization ============

k8s_networkpolicy_controller_t* k8s_networkpolicy_controller_new() {
    k8s_networkpolicy_controller_t* controller = calloc(1, sizeof(k8s_networkpolicy_controller_t));
    if (!controller) return NULL;
    
    controller->max_policies = 1000;
    controller->num_policies = 0;
    controller->enabled = true;
    
    return controller;
}

void k8s_networkpolicy_controller_free(k8s_networkpolicy_controller_t* controller) {
    if (!controller) return;
    
    for (int i = 0; i < controller->num_policies; i++) {
        if (controller->policies[i]) {
            k8s_network_policy_free(controller->policies[i]);
        }
    }
    free(controller->policies);
    free(controller);
}

// ============ Policy Management ============

int k8s_networkpolicy_controller_create(k8s_networkpolicy_controller_t* controller,
                                        k8s_network_policy_t* policy) {
    if (!controller || !policy || !policy->metadata) return -1;
    
    // Check if policy already exists
    if (k8s_networkpolicy_controller_exists(controller, policy->metadata->name,
                                            policy->metadata->namespace)) {
        return -1;  // Duplicate policy
    }
    
    // Check quota
    if (controller->num_policies >= controller->max_policies) {
        return -1;  // Quota exceeded
    }
    
    // Validate policy
    if (k8s_networkpolicy_controller_validate(controller, policy) != 0) {
        return -1;  // Validation failed
    }
    
    // Add policy
    k8s_network_policy_t** new_policies = realloc(controller->policies,
                                                   (controller->num_policies + 1) * sizeof(k8s_network_policy_t*));
    if (!new_policies) return -1;
    
    // Set status
    if (policy->metadata && !policy->metadata->uid) {
        char uid[37];
        snprintf(uid, sizeof(uid), "policy-%d-%ld", controller->num_policies, (long)time(NULL));
        policy->metadata->uid = malloc(strlen(uid) + 1);
        if (policy->metadata->uid) strcpy(policy->metadata->uid, uid);
    }
    
    new_policies[controller->num_policies] = policy;
    controller->policies = new_policies;
    controller->num_policies++;
    
    // TODO: Sync to storage layer (etcd)
    
    return 0;
}

int k8s_networkpolicy_controller_get(k8s_networkpolicy_controller_t* controller,
                                     const char* name,
                                     const char* namespace,
                                     k8s_network_policy_t** policy) {
    if (!controller || !name || !namespace || !policy) return -1;
    
    for (int i = 0; i < controller->num_policies; i++) {
        k8s_network_policy_t* p = controller->policies[i];
        if (p && p->metadata &&
            strcmp(p->metadata->name, name) == 0 &&
            strcmp(p->metadata->namespace, namespace) == 0) {
            *policy = p;
            return 0;
        }
    }
    
    return -1;  // Not found
}

int k8s_networkpolicy_controller_update(k8s_networkpolicy_controller_t* controller,
                                        k8s_network_policy_t* policy) {
    if (!controller || !policy || !policy->metadata) return -1;
    
    // Validate policy
    if (k8s_networkpolicy_controller_validate(controller, policy) != 0) {
        return -1;
    }
    
    // Find and update policy
    for (int i = 0; i < controller->num_policies; i++) {
        k8s_network_policy_t* existing = controller->policies[i];
        if (existing && existing->metadata &&
            strcmp(existing->metadata->name, policy->metadata->name) == 0 &&
            strcmp(existing->metadata->namespace, policy->metadata->namespace) == 0) {
            
            k8s_network_policy_free(existing);
            controller->policies[i] = policy;
            
            // TODO: Sync to storage layer
            
            return 0;
        }
    }
    
    return -1;  // Not found
}

int k8s_networkpolicy_controller_delete(k8s_networkpolicy_controller_t* controller,
                                        const char* name,
                                        const char* namespace) {
    if (!controller || !name || !namespace) return -1;
    
    for (int i = 0; i < controller->num_policies; i++) {
        k8s_network_policy_t* policy = controller->policies[i];
        if (policy && policy->metadata &&
            strcmp(policy->metadata->name, name) == 0 &&
            strcmp(policy->metadata->namespace, namespace) == 0) {
            
            k8s_network_policy_free(policy);
            
            // Remove from array
            for (int j = i; j < controller->num_policies - 1; j++) {
                controller->policies[j] = controller->policies[j + 1];
            }
            controller->num_policies--;
            
            // TODO: Sync to storage layer
            
            return 0;
        }
    }
    
    return -1;  // Not found
}

int k8s_networkpolicy_controller_list(k8s_networkpolicy_controller_t* controller,
                                      const char* namespace,
                                      k8s_network_policy_t*** policies,
                                      int* count) {
    if (!controller || !policies || !count) return -1;
    
    int matched = 0;
    k8s_network_policy_t** result = NULL;
    
    for (int i = 0; i < controller->num_policies; i++) {
        k8s_network_policy_t* policy = controller->policies[i];
        if (policy && policy->metadata) {
            // If namespace specified, filter by it
            if (namespace && strcmp(policy->metadata->namespace, namespace) != 0) {
                continue;
            }
            
            k8s_network_policy_t** new_result = realloc(result, (matched + 1) * sizeof(k8s_network_policy_t*));
            if (!new_result) {
                free(result);
                return -1;
            }
            
            new_result[matched] = policy;
            result = new_result;
            matched++;
        }
    }
    
    *policies = result;
    *count = matched;
    return 0;
}

// ============ Validation ============

int k8s_networkpolicy_controller_validate(k8s_networkpolicy_controller_t* controller,
                                          k8s_network_policy_t* policy) {
    if (!policy || !policy->metadata || !policy->spec) return -1;
    
    // Validate name
    if (!policy->metadata->name || strlen(policy->metadata->name) == 0) {
        return -1;
    }
    
    // Validate namespace
    if (!policy->metadata->namespace || strlen(policy->metadata->namespace) == 0) {
        return -1;
    }
    
    // Validate spec
    if (!policy->spec->pod_selector.pod_selector.match_labels && policy->spec->num_ingress > 0) {
        // Pod selector required if rules defined
        // For now, allow empty selector (matches all pods)
    }
    
    // Validate rules have valid ports
    for (int i = 0; i < policy->spec->num_ingress; i++) {
        k8s_network_rule_t* rule = policy->spec->ingress_rules[i];
        if (rule) {
            for (int j = 0; j < rule->num_ports; j++) {
                k8s_network_port_t* port = rule->ports[j];
                if (port && (port->port <= 0 || port->port > 65535)) {
                    return -1;  // Invalid port
                }
            }
        }
    }
    
    for (int i = 0; i < policy->spec->num_egress; i++) {
        k8s_network_rule_t* rule = policy->spec->egress_rules[i];
        if (rule) {
            for (int j = 0; j < rule->num_ports; j++) {
                k8s_network_port_t* port = rule->ports[j];
                if (port && (port->port <= 0 || port->port > 65535)) {
                    return -1;  // Invalid port
                }
            }
        }
    }
    
    return 0;
}

bool k8s_networkpolicy_controller_exists(k8s_networkpolicy_controller_t* controller,
                                         const char* name,
                                         const char* namespace) {
    if (!controller || !name || !namespace) return false;
    
    for (int i = 0; i < controller->num_policies; i++) {
        k8s_network_policy_t* policy = controller->policies[i];
        if (policy && policy->metadata &&
            strcmp(policy->metadata->name, name) == 0 &&
            strcmp(policy->metadata->namespace, namespace) == 0) {
            return true;
        }
    }
    
    return false;
}

// ============ Control ============

void k8s_networkpolicy_controller_set_enabled(k8s_networkpolicy_controller_t* controller, bool enabled) {
    if (controller) {
        controller->enabled = enabled;
    }
}

bool k8s_networkpolicy_controller_is_enabled(k8s_networkpolicy_controller_t* controller) {
    if (!controller) return false;
    return controller->enabled;
}

// ============ Global Controller ============

k8s_networkpolicy_controller_t* k8s_networkpolicy_controller_global() {
    if (!g_networkpolicy_controller) {
        g_networkpolicy_controller = k8s_networkpolicy_controller_new();
    }
    return g_networkpolicy_controller;
}
