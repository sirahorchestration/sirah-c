#include "authz.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define K8S_MAX_STORED_ROLES 1000
#define K8S_MAX_STORED_ROLE_BINDINGS 1000
#define K8S_MAX_STORED_CLUSTER_ROLES 100
#define K8S_MAX_STORED_CLUSTER_ROLE_BINDINGS 100

typedef struct k8s_authz_engine {
    // Stored Roles (namespace-scoped)
    k8s_role_t** roles;
    int role_count;
    
    // Stored RoleBindings (namespace-scoped)
    k8s_role_binding_t** role_bindings;
    int role_binding_count;
    
    // Stored ClusterRoles
    k8s_cluster_role_t** cluster_roles;
    int cluster_role_count;
    
    // Stored ClusterRoleBindings
    k8s_cluster_role_binding_t** cluster_role_bindings;
    int cluster_role_binding_count;
    
    // Authorization state
    bool enabled;
} k8s_authz_engine_t;

// ============ Engine Lifecycle ============

k8s_authz_engine_t* k8s_authz_engine_new() {
    k8s_authz_engine_t* engine = calloc(1, sizeof(k8s_authz_engine_t));
    if (!engine) return NULL;
    
    engine->roles = calloc(K8S_MAX_STORED_ROLES, sizeof(k8s_role_t*));
    engine->role_bindings = calloc(K8S_MAX_STORED_ROLE_BINDINGS, sizeof(k8s_role_binding_t*));
    engine->cluster_roles = calloc(K8S_MAX_STORED_CLUSTER_ROLES, sizeof(k8s_cluster_role_t*));
    engine->cluster_role_bindings = calloc(K8S_MAX_STORED_CLUSTER_ROLE_BINDINGS, sizeof(k8s_cluster_role_binding_t*));
    
    engine->enabled = true;  // RBAC enabled by default
    return engine;
}

void k8s_authz_engine_free(k8s_authz_engine_t* engine) {
    if (!engine) return;
    
    for (int i = 0; i < engine->role_count; i++) {
        if (engine->roles[i]) k8s_role_free(engine->roles[i]);
    }
    for (int i = 0; i < engine->role_binding_count; i++) {
        if (engine->role_bindings[i]) k8s_role_binding_free(engine->role_bindings[i]);
    }
    for (int i = 0; i < engine->cluster_role_count; i++) {
        if (engine->cluster_roles[i]) k8s_cluster_role_free(engine->cluster_roles[i]);
    }
    for (int i = 0; i < engine->cluster_role_binding_count; i++) {
        if (engine->cluster_role_bindings[i]) k8s_cluster_role_binding_free(engine->cluster_role_bindings[i]);
    }
    
    if (engine->roles) free(engine->roles);
    if (engine->role_bindings) free(engine->role_bindings);
    if (engine->cluster_roles) free(engine->cluster_roles);
    if (engine->cluster_role_bindings) free(engine->cluster_role_bindings);
    
    free(engine);
}

// ============ Management Functions ============

int k8s_authz_add_role(k8s_authz_engine_t* engine, k8s_role_t* role) {
    if (!engine || !role) return -1;
    if (engine->role_count >= K8S_MAX_STORED_ROLES) return -1;
    
    engine->roles[engine->role_count] = role;
    engine->role_count++;
    return 0;
}

int k8s_authz_add_role_binding(k8s_authz_engine_t* engine, k8s_role_binding_t* binding) {
    if (!engine || !binding) return -1;
    if (engine->role_binding_count >= K8S_MAX_STORED_ROLE_BINDINGS) return -1;
    
    engine->role_bindings[engine->role_binding_count] = binding;
    engine->role_binding_count++;
    return 0;
}

int k8s_authz_add_cluster_role(k8s_authz_engine_t* engine, k8s_cluster_role_t* role) {
    if (!engine || !role) return -1;
    if (engine->cluster_role_count >= K8S_MAX_STORED_CLUSTER_ROLES) return -1;
    
    engine->cluster_roles[engine->cluster_role_count] = role;
    engine->cluster_role_count++;
    return 0;
}

int k8s_authz_add_cluster_role_binding(k8s_authz_engine_t* engine,
                                      k8s_cluster_role_binding_t* binding) {
    if (!engine || !binding) return -1;
    if (engine->cluster_role_binding_count >= K8S_MAX_STORED_CLUSTER_ROLE_BINDINGS) return -1;
    
    engine->cluster_role_bindings[engine->cluster_role_binding_count] = binding;
    engine->cluster_role_binding_count++;
    return 0;
}

void k8s_authz_set_enabled(k8s_authz_engine_t* engine, bool enabled) {
    if (engine) {
        engine->enabled = enabled;
    }
}

bool k8s_authz_is_enabled(k8s_authz_engine_t* engine) {
    if (!engine) return false;
    return engine->enabled;
}

// ============ Helper Functions ============

static bool string_matches(const char* str, char* array[], int count) {
    if (!str || count == 0) return false;
    
    for (int i = 0; i < count; i++) {
        if (!array[i]) continue;
        if (strcmp(array[i], "*") == 0) return true;  // Wildcard
        if (strcmp(array[i], str) == 0) return true;
    }
    return false;
}

static bool rule_matches_request(k8s_policy_rule_t* rules, int num_rules,
                                k8s_authz_request_t* req) {
    if (!rules || !req) return false;
    
    for (int r = 0; r < num_rules; r++) {
        k8s_policy_rule_t* rule = &rules[r];
        
        // Check verb
        if (!string_matches(req->verb, rule->verbs, rule->num_verbs)) continue;
        
        // Check API group
        if (!string_matches(req->api_group, rule->api_groups, rule->num_apigroups)) continue;
        
        // Check resource
        if (!string_matches(req->resource, rule->resources, rule->num_resources)) continue;
        
        // Check resource name (if specified in rule)
        if (rule->num_resourcenames > 0) {
            if (!req->resource_name) continue;  // Rule requires resource name but request doesn't have one
            if (!string_matches(req->resource_name, rule->resource_names, rule->num_resourcenames)) continue;
        }
        
        return true;  // All checks passed
    }
    return false;
}

// ============ Authorization ============

bool k8s_authz_authorize(k8s_authz_engine_t* engine,
                        k8s_authz_request_t* req,
                        char** reason) {
    if (!engine || !req) {
        if (reason) *reason = strdup("Invalid parameters");
        return false;
    }
    
    // If RBAC disabled, allow all
    if (!engine->enabled) {
        if (reason) *reason = strdup("RBAC disabled");
        return true;
    }
    
    // System accounts bypass authorization (convenience for system components)
    if (req->user && (strstr(req->user, "system:") == req->user)) {
        if (reason) *reason = strdup("System account bypass");
        return true;  // Allow system accounts
    }
    
    // Check RoleBindings in the namespace (namespace-scoped)
    for (int i = 0; i < engine->role_binding_count; i++) {
        k8s_role_binding_t* binding = engine->role_bindings[i];
        if (!binding) continue;
        
        // Check if user is a subject of this binding
        bool user_in_binding = false;
        for (int j = 0; j < binding->spec.num_subjects; j++) {
            k8s_subject_t* subject = &binding->spec.subjects[j];
            
            if (strcmp(subject->kind, "User") == 0 && 
                strcmp(subject->name, req->user) == 0) {
                user_in_binding = true;
                break;
            }
            if (strcmp(subject->kind, "ServiceAccount") == 0 &&
                strcmp(subject->name, req->user) == 0) {
                user_in_binding = true;
                break;
            }
        }
        
        if (!user_in_binding) continue;
        
        // Check if binding applies to this namespace
        if (req->namespace && binding->metadata && 
            strcmp(binding->metadata->namespace, req->namespace) != 0) continue;
        
        // Find the referenced Role
        k8s_role_t* role = NULL;
        for (int j = 0; j < engine->role_count; j++) {
            if (engine->roles[j] && engine->roles[j]->metadata &&
                strcmp(engine->roles[j]->metadata->name, binding->spec.role_name) == 0 &&
                strcmp(engine->roles[j]->metadata->namespace, binding->metadata->namespace) == 0) {
                role = engine->roles[j];
                break;
            }
        }
        
        if (!role) continue;  // Role not found
        
        // Check if role allows the action
        if (rule_matches_request(role->spec.rules, role->spec.num_rules, req)) {
            if (reason) *reason = strdup("Matched RoleBinding with Role");
            return true;
        }
    }
    
    // Check ClusterRoleBindings (cluster-scoped)
    for (int i = 0; i < engine->cluster_role_binding_count; i++) {
        k8s_cluster_role_binding_t* binding = engine->cluster_role_bindings[i];
        if (!binding) continue;
        
        // Check if user is a subject of this binding
        bool user_in_binding = false;
        for (int j = 0; j < binding->spec.num_subjects; j++) {
            k8s_subject_t* subject = &binding->spec.subjects[j];
            
            if (strcmp(subject->kind, "User") == 0 &&
                strcmp(subject->name, req->user) == 0) {
                user_in_binding = true;
                break;
            }
            if (strcmp(subject->kind, "ServiceAccount") == 0 &&
                strcmp(subject->name, req->user) == 0) {
                user_in_binding = true;
                break;
            }
        }
        
        if (!user_in_binding) continue;
        
        // Find the referenced ClusterRole
        k8s_cluster_role_t* crole = NULL;
        for (int j = 0; j < engine->cluster_role_count; j++) {
            if (engine->cluster_roles[j] && engine->cluster_roles[j]->metadata &&
                strcmp(engine->cluster_roles[j]->metadata->name, binding->spec.cluster_role_name) == 0) {
                crole = engine->cluster_roles[j];
                break;
            }
        }
        
        if (!crole) continue;  // ClusterRole not found
        
        // Check if cluster role allows the action
        if (rule_matches_request(crole->spec.rules, crole->spec.num_rules, req)) {
            if (reason) *reason = strdup("Matched ClusterRoleBinding with ClusterRole");
            return true;
        }
    }
    
    // Default: deny
    if (reason) {
        static char reason_buf[256];
        snprintf(reason_buf, sizeof(reason_buf),
            "No rules found for user '%s', verb '%s', resource '%s'",
            req->user ? req->user : "unknown",
            req->verb ? req->verb : "unknown",
            req->resource ? req->resource : "unknown");
        *reason = reason_buf;
    }
    return false;
}

// ============ Default Roles ============

int k8s_authz_create_default_roles(k8s_authz_engine_t* engine) {
    if (!engine) return -1;
    
    // Create cluster-admin role (full access)
    k8s_cluster_role_t* admin = k8s_cluster_role_new("cluster-admin");
    if (!admin) return -1;
    
    k8s_policy_rule_t* admin_rule = k8s_policy_rule_new();
    if (!admin_rule) {
        k8s_cluster_role_free(admin);
        return -1;
    }
    
    k8s_policy_rule_add_verb(admin_rule, "*");
    k8s_policy_rule_add_api_group(admin_rule, "*");
    k8s_policy_rule_add_resource(admin_rule, "*");
    k8s_cluster_role_add_rule(admin, admin_rule);
    k8s_authz_add_cluster_role(engine, admin);
    
    // Create edit role (create/read/update/delete)
    k8s_cluster_role_t* edit = k8s_cluster_role_new("edit");
    if (!edit) return -1;
    
    k8s_policy_rule_t* edit_rule = k8s_policy_rule_new();
    if (!edit_rule) {
        k8s_cluster_role_free(edit);
        return -1;
    }
    
    k8s_policy_rule_add_verb(edit_rule, "create");
    k8s_policy_rule_add_verb(edit_rule, "delete");
    k8s_policy_rule_add_verb(edit_rule, "get");
    k8s_policy_rule_add_verb(edit_rule, "list");
    k8s_policy_rule_add_verb(edit_rule, "patch");
    k8s_policy_rule_add_verb(edit_rule, "update");
    k8s_policy_rule_add_api_group(edit_rule, "");
    k8s_policy_rule_add_api_group(edit_rule, "apps");
    k8s_policy_rule_add_resource(edit_rule, "pods");
    k8s_policy_rule_add_resource(edit_rule, "deployments");
    k8s_policy_rule_add_resource(edit_rule, "services");
    k8s_cluster_role_add_rule(edit, edit_rule);
    k8s_authz_add_cluster_role(engine, edit);
    
    // Create view role (read-only)
    k8s_cluster_role_t* view = k8s_cluster_role_new("view");
    if (!view) return -1;
    
    k8s_policy_rule_t* view_rule = k8s_policy_rule_new();
    if (!view_rule) {
        k8s_cluster_role_free(view);
        return -1;
    }
    
    k8s_policy_rule_add_verb(view_rule, "get");
    k8s_policy_rule_add_verb(view_rule, "list");
    k8s_policy_rule_add_verb(view_rule, "watch");
    k8s_policy_rule_add_api_group(view_rule, "*");
    k8s_policy_rule_add_resource(view_rule, "*");
    k8s_cluster_role_add_rule(view, view_rule);
    k8s_authz_add_cluster_role(engine, view);
    
    return 0;
}
