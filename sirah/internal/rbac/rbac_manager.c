#include "rbac_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <json-c/json.h>

/* ============================================================================
   RBAC Manager Implementation - Kubernetes v1.28
   
   Thread-safe global RBAC state with mutex protection.
   Supports Roles, RoleBindings, ClusterRoles, ClusterRoleBindings.
   Comprehensive audit logging of all authorization decisions.
   
   ============================================================================ */

/* Storage limits */
#define MAX_ROLES 1000
#define MAX_CLUSTER_ROLES 200
#define MAX_ROLE_BINDINGS 2000
#define MAX_CLUSTER_ROLE_BINDINGS 500
#define MAX_AUDIT_LOGS 10000

/* RBAC Manager State */
typedef struct {
    pthread_mutex_t lock;
    
    /* Namespace-scoped roles and bindings */
    rbac_role_t roles[MAX_ROLES];
    int role_count;
    
    rbac_role_binding_t role_bindings[MAX_ROLE_BINDINGS];
    int role_binding_count;
    
    /* Cluster-scoped roles and bindings */
    rbac_role_t cluster_roles[MAX_CLUSTER_ROLES];
    int cluster_role_count;
    
    rbac_role_binding_t cluster_role_bindings[MAX_CLUSTER_ROLE_BINDINGS];
    int cluster_role_binding_count;
    
    /* Audit logging */
    rbac_audit_event_t audit_logs[MAX_AUDIT_LOGS];
    int audit_log_count;
    
    int running;
} rbac_manager_t;

static rbac_manager_t g_rbac_manager = {0};

/* Background thread for audit log cleanup */
static void* rbac_audit_cleanup_thread(void* arg);

/* ============================================================================
   Initialization and Shutdown
   ============================================================================ */

int rbac_manager_init(void) {
    pthread_mutex_init(&g_rbac_manager.lock, NULL);
    
    g_rbac_manager.role_count = 0;
    g_rbac_manager.role_binding_count = 0;
    g_rbac_manager.cluster_role_count = 0;
    g_rbac_manager.cluster_role_binding_count = 0;
    g_rbac_manager.audit_log_count = 0;
    g_rbac_manager.running = 1;
    
    printf("[RBAC] Manager initialized\n");
    
    /* Create default system roles and bindings */
    if (rbac_create_default_roles() != 0) {
        fprintf(stderr, "[RBAC] Failed to create default roles\n");
        return -1;
    }
    
    printf("[RBAC] Default roles and bindings created\n");
    return 0;
}

int rbac_manager_shutdown(void) {
    pthread_mutex_lock(&g_rbac_manager.lock);
    g_rbac_manager.running = 0;
    pthread_mutex_unlock(&g_rbac_manager.lock);
    
    printf("[RBAC] Manager shutdown\n");
    return 0;
}

/* ============================================================================
   Role Management (Namespace-Scoped)
   ============================================================================ */

int rbac_create_role(const char* namespace, rbac_role_t* role) {
    if (!namespace || !role || !role->name) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    if (g_rbac_manager.role_count >= MAX_ROLES) {
        pthread_mutex_unlock(&g_rbac_manager.lock);
        return -1;
    }
    
    /* Check for duplicate */
    for (int i = 0; i < g_rbac_manager.role_count; i++) {
        if (strcmp(g_rbac_manager.roles[i].name, role->name) == 0 &&
            strcmp(g_rbac_manager.roles[i].namespace, namespace) == 0) {
            pthread_mutex_unlock(&g_rbac_manager.lock);
            return -1;  /* Already exists */
        }
    }
    
    /* Create role */
    rbac_role_t* new_role = &g_rbac_manager.roles[g_rbac_manager.role_count++];
    memcpy(new_role, role, sizeof(rbac_role_t));
    strncpy(new_role->namespace, namespace, sizeof(new_role->namespace) - 1);
    new_role->created_at = time(NULL);
    
    printf("[RBAC] Role created: %s/%s with %d rules\n", namespace, role->name, role->rule_count);
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return 0;
}

int rbac_get_role(const char* namespace, const char* name, rbac_role_t* out) {
    if (!namespace || !name || !out) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    for (int i = 0; i < g_rbac_manager.role_count; i++) {
        if (strcmp(g_rbac_manager.roles[i].name, name) == 0 &&
            strcmp(g_rbac_manager.roles[i].namespace, namespace) == 0) {
            memcpy(out, &g_rbac_manager.roles[i], sizeof(rbac_role_t));
            pthread_mutex_unlock(&g_rbac_manager.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return -1;  /* Not found */
}

int rbac_update_role(const char* namespace, rbac_role_t* role) {
    if (!namespace || !role || !role->name) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    for (int i = 0; i < g_rbac_manager.role_count; i++) {
        if (strcmp(g_rbac_manager.roles[i].name, role->name) == 0 &&
            strcmp(g_rbac_manager.roles[i].namespace, namespace) == 0) {
            memcpy(&g_rbac_manager.roles[i], role, sizeof(rbac_role_t));
            strncpy(g_rbac_manager.roles[i].namespace, namespace, 
                   sizeof(g_rbac_manager.roles[i].namespace) - 1);
            pthread_mutex_unlock(&g_rbac_manager.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return -1;  /* Not found */
}

int rbac_delete_role(const char* namespace, const char* name) {
    if (!namespace || !name) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    for (int i = 0; i < g_rbac_manager.role_count; i++) {
        if (strcmp(g_rbac_manager.roles[i].name, name) == 0 &&
            strcmp(g_rbac_manager.roles[i].namespace, namespace) == 0) {
            /* Remove by shifting */
            for (int j = i; j < g_rbac_manager.role_count - 1; j++) {
                memcpy(&g_rbac_manager.roles[j], &g_rbac_manager.roles[j + 1],
                      sizeof(rbac_role_t));
            }
            g_rbac_manager.role_count--;
            printf("[RBAC] Role deleted: %s/%s\n", namespace, name);
            pthread_mutex_unlock(&g_rbac_manager.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return -1;  /* Not found */
}

int rbac_list_roles(const char* namespace, rbac_role_t** out) {
    if (!namespace || !out) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    /* Count matching roles */
    int count = 0;
    for (int i = 0; i < g_rbac_manager.role_count; i++) {
        if (strcmp(g_rbac_manager.roles[i].namespace, namespace) == 0) {
            count++;
        }
    }
    
    if (count == 0) {
        pthread_mutex_unlock(&g_rbac_manager.lock);
        *out = NULL;
        return 0;
    }
    
    /* Allocate and copy */
    *out = malloc(count * sizeof(rbac_role_t));
    int idx = 0;
    for (int i = 0; i < g_rbac_manager.role_count; i++) {
        if (strcmp(g_rbac_manager.roles[i].namespace, namespace) == 0) {
            memcpy(&(*out)[idx++], &g_rbac_manager.roles[i], sizeof(rbac_role_t));
        }
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return count;
}

int rbac_add_rule_to_role(const char* namespace, const char* role_name, rbac_rule_t* rule) {
    if (!namespace || !role_name || !rule) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    for (int i = 0; i < g_rbac_manager.role_count; i++) {
        if (strcmp(g_rbac_manager.roles[i].name, role_name) == 0 &&
            strcmp(g_rbac_manager.roles[i].namespace, namespace) == 0) {
            
            if (g_rbac_manager.roles[i].rule_count >= 50) {
                pthread_mutex_unlock(&g_rbac_manager.lock);
                return -1;  /* Rule limit exceeded */
            }
            
            memcpy(&g_rbac_manager.roles[i].rules[g_rbac_manager.roles[i].rule_count++],
                  rule, sizeof(rbac_rule_t));
            pthread_mutex_unlock(&g_rbac_manager.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return -1;  /* Role not found */
}

int rbac_remove_rule_from_role(const char* namespace, const char* role_name, int rule_index) {
    if (!namespace || !role_name || rule_index < 0) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    for (int i = 0; i < g_rbac_manager.role_count; i++) {
        if (strcmp(g_rbac_manager.roles[i].name, role_name) == 0 &&
            strcmp(g_rbac_manager.roles[i].namespace, namespace) == 0) {
            
            if (rule_index >= g_rbac_manager.roles[i].rule_count) {
                pthread_mutex_unlock(&g_rbac_manager.lock);
                return -1;  /* Index out of range */
            }
            
            /* Remove by shifting */
            for (int j = rule_index; j < g_rbac_manager.roles[i].rule_count - 1; j++) {
                memcpy(&g_rbac_manager.roles[i].rules[j],
                      &g_rbac_manager.roles[i].rules[j + 1],
                      sizeof(rbac_rule_t));
            }
            g_rbac_manager.roles[i].rule_count--;
            pthread_mutex_unlock(&g_rbac_manager.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return -1;  /* Role not found */
}

/* ============================================================================
   ClusterRole Management (Cluster-Scoped)
   ============================================================================ */

int rbac_create_cluster_role(rbac_role_t* role) {
    if (!role || !role->name) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    if (g_rbac_manager.cluster_role_count >= MAX_CLUSTER_ROLES) {
        pthread_mutex_unlock(&g_rbac_manager.lock);
        return -1;
    }
    
    /* Check for duplicate */
    for (int i = 0; i < g_rbac_manager.cluster_role_count; i++) {
        if (strcmp(g_rbac_manager.cluster_roles[i].name, role->name) == 0) {
            pthread_mutex_unlock(&g_rbac_manager.lock);
            return -1;
        }
    }
    
    rbac_role_t* new_role = &g_rbac_manager.cluster_roles[g_rbac_manager.cluster_role_count++];
    memcpy(new_role, role, sizeof(rbac_role_t));
    new_role->namespace[0] = '\0';  /* Cluster-scoped */
    new_role->created_at = time(NULL);
    
    printf("[RBAC] ClusterRole created: %s with %d rules\n", role->name, role->rule_count);
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return 0;
}

int rbac_get_cluster_role(const char* name, rbac_role_t* out) {
    if (!name || !out) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    for (int i = 0; i < g_rbac_manager.cluster_role_count; i++) {
        if (strcmp(g_rbac_manager.cluster_roles[i].name, name) == 0) {
            memcpy(out, &g_rbac_manager.cluster_roles[i], sizeof(rbac_role_t));
            pthread_mutex_unlock(&g_rbac_manager.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return -1;
}

int rbac_update_cluster_role(rbac_role_t* role) {
    if (!role || !role->name) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    for (int i = 0; i < g_rbac_manager.cluster_role_count; i++) {
        if (strcmp(g_rbac_manager.cluster_roles[i].name, role->name) == 0) {
            memcpy(&g_rbac_manager.cluster_roles[i], role, sizeof(rbac_role_t));
            g_rbac_manager.cluster_roles[i].namespace[0] = '\0';
            pthread_mutex_unlock(&g_rbac_manager.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return -1;
}

int rbac_delete_cluster_role(const char* name) {
    if (!name) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    for (int i = 0; i < g_rbac_manager.cluster_role_count; i++) {
        if (strcmp(g_rbac_manager.cluster_roles[i].name, name) == 0) {
            for (int j = i; j < g_rbac_manager.cluster_role_count - 1; j++) {
                memcpy(&g_rbac_manager.cluster_roles[j], &g_rbac_manager.cluster_roles[j + 1],
                      sizeof(rbac_role_t));
            }
            g_rbac_manager.cluster_role_count--;
            printf("[RBAC] ClusterRole deleted: %s\n", name);
            pthread_mutex_unlock(&g_rbac_manager.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return -1;
}

int rbac_list_cluster_roles(rbac_role_t** out) {
    if (!out) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    if (g_rbac_manager.cluster_role_count == 0) {
        pthread_mutex_unlock(&g_rbac_manager.lock);
        *out = NULL;
        return 0;
    }
    
    *out = malloc(g_rbac_manager.cluster_role_count * sizeof(rbac_role_t));
    for (int i = 0; i < g_rbac_manager.cluster_role_count; i++) {
        memcpy(&(*out)[i], &g_rbac_manager.cluster_roles[i], sizeof(rbac_role_t));
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return g_rbac_manager.cluster_role_count;
}

/* ============================================================================
   RoleBinding Management (Namespace-Scoped)
   ============================================================================ */

int rbac_create_role_binding(const char* namespace, rbac_role_binding_t* binding) {
    if (!namespace || !binding || !binding->name) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    if (g_rbac_manager.role_binding_count >= MAX_ROLE_BINDINGS) {
        pthread_mutex_unlock(&g_rbac_manager.lock);
        return -1;
    }
    
    /* Check for duplicate */
    for (int i = 0; i < g_rbac_manager.role_binding_count; i++) {
        if (strcmp(g_rbac_manager.role_bindings[i].name, binding->name) == 0 &&
            strcmp(g_rbac_manager.role_bindings[i].namespace, namespace) == 0) {
            pthread_mutex_unlock(&g_rbac_manager.lock);
            return -1;
        }
    }
    
    rbac_role_binding_t* new_binding = 
        &g_rbac_manager.role_bindings[g_rbac_manager.role_binding_count++];
    memcpy(new_binding, binding, sizeof(rbac_role_binding_t));
    strncpy(new_binding->namespace, namespace, sizeof(new_binding->namespace) - 1);
    new_binding->created_at = time(NULL);
    
    printf("[RBAC] RoleBinding created: %s/%s -> %s (%d subjects)\n",
          namespace, binding->name, binding->role_name, binding->subject_count);
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return 0;
}

int rbac_get_role_binding(const char* namespace, const char* name, rbac_role_binding_t* out) {
    if (!namespace || !name || !out) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    for (int i = 0; i < g_rbac_manager.role_binding_count; i++) {
        if (strcmp(g_rbac_manager.role_bindings[i].name, name) == 0 &&
            strcmp(g_rbac_manager.role_bindings[i].namespace, namespace) == 0) {
            memcpy(out, &g_rbac_manager.role_bindings[i], sizeof(rbac_role_binding_t));
            pthread_mutex_unlock(&g_rbac_manager.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return -1;
}

int rbac_update_role_binding(const char* namespace, rbac_role_binding_t* binding) {
    if (!namespace || !binding || !binding->name) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    for (int i = 0; i < g_rbac_manager.role_binding_count; i++) {
        if (strcmp(g_rbac_manager.role_bindings[i].name, binding->name) == 0 &&
            strcmp(g_rbac_manager.role_bindings[i].namespace, namespace) == 0) {
            memcpy(&g_rbac_manager.role_bindings[i], binding, sizeof(rbac_role_binding_t));
            strncpy(g_rbac_manager.role_bindings[i].namespace, namespace,
                   sizeof(g_rbac_manager.role_bindings[i].namespace) - 1);
            pthread_mutex_unlock(&g_rbac_manager.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return -1;
}

int rbac_delete_role_binding(const char* namespace, const char* name) {
    if (!namespace || !name) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    for (int i = 0; i < g_rbac_manager.role_binding_count; i++) {
        if (strcmp(g_rbac_manager.role_bindings[i].name, name) == 0 &&
            strcmp(g_rbac_manager.role_bindings[i].namespace, namespace) == 0) {
            for (int j = i; j < g_rbac_manager.role_binding_count - 1; j++) {
                memcpy(&g_rbac_manager.role_bindings[j], &g_rbac_manager.role_bindings[j + 1],
                      sizeof(rbac_role_binding_t));
            }
            g_rbac_manager.role_binding_count--;
            printf("[RBAC] RoleBinding deleted: %s/%s\n", namespace, name);
            pthread_mutex_unlock(&g_rbac_manager.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return -1;
}

int rbac_list_role_bindings(const char* namespace, rbac_role_binding_t** out) {
    if (!namespace || !out) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    int count = 0;
    for (int i = 0; i < g_rbac_manager.role_binding_count; i++) {
        if (strcmp(g_rbac_manager.role_bindings[i].namespace, namespace) == 0) {
            count++;
        }
    }
    
    if (count == 0) {
        pthread_mutex_unlock(&g_rbac_manager.lock);
        *out = NULL;
        return 0;
    }
    
    *out = malloc(count * sizeof(rbac_role_binding_t));
    int idx = 0;
    for (int i = 0; i < g_rbac_manager.role_binding_count; i++) {
        if (strcmp(g_rbac_manager.role_bindings[i].namespace, namespace) == 0) {
            memcpy(&(*out)[idx++], &g_rbac_manager.role_bindings[i], sizeof(rbac_role_binding_t));
        }
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return count;
}

int rbac_add_subject_to_binding(const char* namespace, const char* binding_name,
                               rbac_subject_t* subject) {
    if (!namespace || !binding_name || !subject) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    for (int i = 0; i < g_rbac_manager.role_binding_count; i++) {
        if (strcmp(g_rbac_manager.role_bindings[i].name, binding_name) == 0 &&
            strcmp(g_rbac_manager.role_bindings[i].namespace, namespace) == 0) {
            
            if (g_rbac_manager.role_bindings[i].subject_count >= 20) {
                pthread_mutex_unlock(&g_rbac_manager.lock);
                return -1;
            }
            
            memcpy(&g_rbac_manager.role_bindings[i].subjects[
                g_rbac_manager.role_bindings[i].subject_count++],
                  subject, sizeof(rbac_subject_t));
            pthread_mutex_unlock(&g_rbac_manager.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return -1;
}

int rbac_remove_subject_from_binding(const char* namespace, const char* binding_name,
                                    rbac_subject_t* subject) {
    if (!namespace || !binding_name || !subject) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    for (int i = 0; i < g_rbac_manager.role_binding_count; i++) {
        if (strcmp(g_rbac_manager.role_bindings[i].name, binding_name) == 0 &&
            strcmp(g_rbac_manager.role_bindings[i].namespace, namespace) == 0) {
            
            for (int j = 0; j < g_rbac_manager.role_bindings[i].subject_count; j++) {
                if (strcmp(g_rbac_manager.role_bindings[i].subjects[j].name, subject->name) == 0 &&
                    g_rbac_manager.role_bindings[i].subjects[j].type == subject->type) {
                    
                    /* Remove by shifting */
                    for (int k = j; k < g_rbac_manager.role_bindings[i].subject_count - 1; k++) {
                        memcpy(&g_rbac_manager.role_bindings[i].subjects[k],
                              &g_rbac_manager.role_bindings[i].subjects[k + 1],
                              sizeof(rbac_subject_t));
                    }
                    g_rbac_manager.role_bindings[i].subject_count--;
                    pthread_mutex_unlock(&g_rbac_manager.lock);
                    return 0;
                }
            }
            
            pthread_mutex_unlock(&g_rbac_manager.lock);
            return -1;  /* Subject not found */
        }
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return -1;  /* Binding not found */
}

/* ============================================================================
   ClusterRoleBinding Management (Cluster-Scoped)
   ============================================================================ */

int rbac_create_cluster_role_binding(rbac_role_binding_t* binding) {
    if (!binding || !binding->name) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    if (g_rbac_manager.cluster_role_binding_count >= MAX_CLUSTER_ROLE_BINDINGS) {
        pthread_mutex_unlock(&g_rbac_manager.lock);
        return -1;
    }
    
    /* Check for duplicate */
    for (int i = 0; i < g_rbac_manager.cluster_role_binding_count; i++) {
        if (strcmp(g_rbac_manager.cluster_role_bindings[i].name, binding->name) == 0) {
            pthread_mutex_unlock(&g_rbac_manager.lock);
            return -1;
        }
    }
    
    rbac_role_binding_t* new_binding = 
        &g_rbac_manager.cluster_role_bindings[g_rbac_manager.cluster_role_binding_count++];
    memcpy(new_binding, binding, sizeof(rbac_role_binding_t));
    new_binding->namespace[0] = '\0';
    new_binding->created_at = time(NULL);
    
    printf("[RBAC] ClusterRoleBinding created: %s -> %s\n", binding->name, binding->role_name);
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return 0;
}

int rbac_get_cluster_role_binding(const char* name, rbac_role_binding_t* out) {
    if (!name || !out) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    for (int i = 0; i < g_rbac_manager.cluster_role_binding_count; i++) {
        if (strcmp(g_rbac_manager.cluster_role_bindings[i].name, name) == 0) {
            memcpy(out, &g_rbac_manager.cluster_role_bindings[i], sizeof(rbac_role_binding_t));
            pthread_mutex_unlock(&g_rbac_manager.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return -1;
}

int rbac_update_cluster_role_binding(rbac_role_binding_t* binding) {
    if (!binding || !binding->name) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    for (int i = 0; i < g_rbac_manager.cluster_role_binding_count; i++) {
        if (strcmp(g_rbac_manager.cluster_role_bindings[i].name, binding->name) == 0) {
            memcpy(&g_rbac_manager.cluster_role_bindings[i], binding, sizeof(rbac_role_binding_t));
            g_rbac_manager.cluster_role_bindings[i].namespace[0] = '\0';
            pthread_mutex_unlock(&g_rbac_manager.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return -1;
}

int rbac_delete_cluster_role_binding(const char* name) {
    if (!name) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    for (int i = 0; i < g_rbac_manager.cluster_role_binding_count; i++) {
        if (strcmp(g_rbac_manager.cluster_role_bindings[i].name, name) == 0) {
            for (int j = i; j < g_rbac_manager.cluster_role_binding_count - 1; j++) {
                memcpy(&g_rbac_manager.cluster_role_bindings[j],
                      &g_rbac_manager.cluster_role_bindings[j + 1],
                      sizeof(rbac_role_binding_t));
            }
            g_rbac_manager.cluster_role_binding_count--;
            printf("[RBAC] ClusterRoleBinding deleted: %s\n", name);
            pthread_mutex_unlock(&g_rbac_manager.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return -1;
}

int rbac_list_cluster_role_bindings(rbac_role_binding_t** out) {
    if (!out) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    if (g_rbac_manager.cluster_role_binding_count == 0) {
        pthread_mutex_unlock(&g_rbac_manager.lock);
        *out = NULL;
        return 0;
    }
    
    *out = malloc(g_rbac_manager.cluster_role_binding_count * sizeof(rbac_role_binding_t));
    for (int i = 0; i < g_rbac_manager.cluster_role_binding_count; i++) {
        memcpy(&(*out)[i], &g_rbac_manager.cluster_role_bindings[i], 
              sizeof(rbac_role_binding_t));
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return g_rbac_manager.cluster_role_binding_count;
}

/* ============================================================================
   Authorization and Policy Checking
   ============================================================================ */

/* Helper to match verb (supports wildcards) */
int rbac_verb_matches(const char* rule_verb, const char* target_verb) {
    if (!rule_verb || !target_verb) {
        return 0;
    }
    if (strcmp(rule_verb, "*") == 0) {
        return 1;
    }
    return strcmp(rule_verb, target_verb) == 0;
}

/* Helper to match resource (supports wildcards) */
int rbac_resource_matches(const char* rule_resource, const char* target_resource) {
    if (!rule_resource || !target_resource) {
        return 0;
    }
    if (strcmp(rule_resource, "*") == 0) {
        return 1;
    }
    return strcmp(rule_resource, target_resource) == 0;
}

/* Main authorization function */
int rbac_can_perform_action(
    const char* user, const char* groups,
    const char* verb, const char* api_group, const char* resource,
    const char* namespace, const char* resource_name,
    rbac_policy_decision_t* decision) {
    
    if (!user || !verb || !resource || !decision) {
        return -1;  /* DENY */
    }
    
    memset(decision, 0, sizeof(rbac_policy_decision_t));
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    int user_allowed = 0;
    const char* matched_rule = "";
    
    /* Check ClusterRoleBindings first (cluster-scoped) */
    for (int b = 0; b < g_rbac_manager.cluster_role_binding_count; b++) {
        rbac_role_binding_t* binding = &g_rbac_manager.cluster_role_bindings[b];
        
        /* Check if user/group is in subjects */
        int user_matches = 0;
        for (int s = 0; s < binding->subject_count; s++) {
            if (binding->subjects[s].type == RBAC_SUBJECT_USER &&
                strcmp(binding->subjects[s].name, user) == 0) {
                user_matches = 1;
                break;
            }
            if (binding->subjects[s].type == RBAC_SUBJECT_GROUP && groups) {
                /* Check if group is in user's groups */
                if (strstr(groups, binding->subjects[s].name) != NULL) {
                    user_matches = 1;
                    break;
                }
            }
        }
        
        if (!user_matches) continue;
        
        /* Find referenced ClusterRole */
        rbac_role_t* role = NULL;
        for (int r = 0; r < g_rbac_manager.cluster_role_count; r++) {
            if (strcmp(g_rbac_manager.cluster_roles[r].name, binding->role_name) == 0) {
                role = &g_rbac_manager.cluster_roles[r];
                break;
            }
        }
        
        if (!role) continue;
        
        /* Check rules in role */
        for (int r = 0; r < role->rule_count; r++) {
            rbac_rule_t* rule = &role->rules[r];
            
            /* Check API group */
            int api_matches = 0;
            for (int ag = 0; ag < rule->api_group_count; ag++) {
                if (strcmp(rule->api_groups[ag], "*") == 0 ||
                    strcmp(rule->api_groups[ag], api_group ? api_group : "") == 0) {
                    api_matches = 1;
                    break;
                }
            }
            if (!api_matches) continue;
            
            /* Check resource */
            int resource_matches = 0;
            for (int re = 0; re < rule->resource_count; re++) {
                if (rbac_resource_matches(rule->resources[re], resource)) {
                    resource_matches = 1;
                    break;
                }
            }
            if (!resource_matches) continue;
            
            /* Check verb */
            int verb_matches = 0;
            for (int v = 0; v < rule->verb_count; v++) {
                if (rbac_verb_matches(rule->verbs[v], verb)) {
                    verb_matches = 1;
                    break;
                }
            }
            if (!verb_matches) continue;
            
            /* Check resource name if specified in rule */
            if (rule->resource_name_count > 0 && resource_name) {
                int name_matches = 0;
                for (int n = 0; n < rule->resource_name_count; n++) {
                    if (strcmp(rule->resource_names[n], "*") == 0 ||
                        strcmp(rule->resource_names[n], resource_name) == 0) {
                        name_matches = 1;
                        break;
                    }
                }
                if (!name_matches) continue;
            }
            
            /* Rule matched! */
            user_allowed = 1;
            matched_rule = binding->role_name;
            break;
        }
        
        if (user_allowed) break;
    }
    
    /* If not allowed by ClusterRole, check namespace RoleBindings */
    if (!user_allowed && namespace) {
        for (int b = 0; b < g_rbac_manager.role_binding_count; b++) {
            rbac_role_binding_t* binding = &g_rbac_manager.role_bindings[b];
            
            if (strcmp(binding->namespace, namespace) != 0) continue;
            
            /* Check if user is in subjects */
            int user_matches = 0;
            for (int s = 0; s < binding->subject_count; s++) {
                if (binding->subjects[s].type == RBAC_SUBJECT_USER &&
                    strcmp(binding->subjects[s].name, user) == 0) {
                    user_matches = 1;
                    break;
                }
                if (binding->subjects[s].type == RBAC_SUBJECT_GROUP && groups) {
                    if (strstr(groups, binding->subjects[s].name) != NULL) {
                        user_matches = 1;
                        break;
                    }
                }
            }
            
            if (!user_matches) continue;
            
            /* Find referenced Role */
            rbac_role_t* role = NULL;
            for (int r = 0; r < g_rbac_manager.role_count; r++) {
                if (strcmp(g_rbac_manager.roles[r].name, binding->role_name) == 0 &&
                    strcmp(g_rbac_manager.roles[r].namespace, namespace) == 0) {
                    role = &g_rbac_manager.roles[r];
                    break;
                }
            }
            
            if (!role) continue;
            
            /* Check rules */
            for (int r = 0; r < role->rule_count; r++) {
                rbac_rule_t* rule = &role->rules[r];
                
                int api_matches = 0;
                for (int ag = 0; ag < rule->api_group_count; ag++) {
                    if (strcmp(rule->api_groups[ag], "*") == 0 ||
                        strcmp(rule->api_groups[ag], api_group ? api_group : "") == 0) {
                        api_matches = 1;
                        break;
                    }
                }
                if (!api_matches) continue;
                
                int resource_matches = 0;
                for (int re = 0; re < rule->resource_count; re++) {
                    if (rbac_resource_matches(rule->resources[re], resource)) {
                        resource_matches = 1;
                        break;
                    }
                }
                if (!resource_matches) continue;
                
                int verb_matches = 0;
                for (int v = 0; v < rule->verb_count; v++) {
                    if (rbac_verb_matches(rule->verbs[v], verb)) {
                        verb_matches = 1;
                        break;
                    }
                }
                if (!verb_matches) continue;
                
                if (rule->resource_name_count > 0 && resource_name) {
                    int name_matches = 0;
                    for (int n = 0; n < rule->resource_name_count; n++) {
                        if (strcmp(rule->resource_names[n], "*") == 0 ||
                            strcmp(rule->resource_names[n], resource_name) == 0) {
                            name_matches = 1;
                            break;
                        }
                    }
                    if (!name_matches) continue;
                }
                
                user_allowed = 1;
                matched_rule = binding->role_name;
                break;
            }
            
            if (user_allowed) break;
        }
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    
    if (user_allowed) {
        decision->decision = RBAC_ALLOW;
        snprintf(decision->reason, sizeof(decision->reason),
                "Matched rule in %s", matched_rule);
        snprintf(decision->matched_rule, sizeof(decision->matched_rule),
                "%s", matched_rule);
        return 0;  /* ALLOW */
    } else {
        decision->decision = RBAC_DENY;
        snprintf(decision->reason, sizeof(decision->reason),
                "No matching rule for %s to %s %s", user, verb, resource);
        return -1;  /* DENY */
    }
}

/* Convenience functions */
int rbac_can_get(const char* user, const char* groups, const char* resource,
                const char* namespace, const char* resource_name,
                rbac_policy_decision_t* decision) {
    return rbac_can_perform_action(user, groups, "get", "", resource, 
                                  namespace, resource_name, decision);
}

int rbac_can_list(const char* user, const char* groups, const char* resource,
                 const char* namespace, rbac_policy_decision_t* decision) {
    return rbac_can_perform_action(user, groups, "list", "", resource,
                                  namespace, NULL, decision);
}

int rbac_can_watch(const char* user, const char* groups, const char* resource,
                  const char* namespace, rbac_policy_decision_t* decision) {
    return rbac_can_perform_action(user, groups, "watch", "", resource,
                                  namespace, NULL, decision);
}

int rbac_can_create(const char* user, const char* groups, const char* resource,
                   const char* namespace, rbac_policy_decision_t* decision) {
    return rbac_can_perform_action(user, groups, "create", "", resource,
                                  namespace, NULL, decision);
}

int rbac_can_update(const char* user, const char* groups, const char* resource,
                   const char* namespace, const char* resource_name,
                   rbac_policy_decision_t* decision) {
    return rbac_can_perform_action(user, groups, "update", "", resource,
                                  namespace, resource_name, decision);
}

int rbac_can_patch(const char* user, const char* groups, const char* resource,
                  const char* namespace, const char* resource_name,
                  rbac_policy_decision_t* decision) {
    return rbac_can_perform_action(user, groups, "patch", "", resource,
                                  namespace, resource_name, decision);
}

int rbac_can_delete(const char* user, const char* groups, const char* resource,
                   const char* namespace, const char* resource_name,
                   rbac_policy_decision_t* decision) {
    return rbac_can_perform_action(user, groups, "delete", "", resource,
                                  namespace, resource_name, decision);
}

/* ============================================================================
   Audit Logging
   ============================================================================ */

int rbac_log_audit(rbac_audit_event_t* event) {
    if (!event) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    if (g_rbac_manager.audit_log_count >= MAX_AUDIT_LOGS) {
        /* Shift out oldest entry */
        for (int i = 0; i < g_rbac_manager.audit_log_count - 1; i++) {
            memcpy(&g_rbac_manager.audit_logs[i],
                  &g_rbac_manager.audit_logs[i + 1],
                  sizeof(rbac_audit_event_t));
        }
        g_rbac_manager.audit_log_count--;
    }
    
    event->timestamp = time(NULL);
    memcpy(&g_rbac_manager.audit_logs[g_rbac_manager.audit_log_count++],
          event, sizeof(rbac_audit_event_t));
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return 0;
}

int rbac_get_audit_logs(const char* user_filter, rbac_audit_event_t** out) {
    if (!out) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    int count = 0;
    for (int i = 0; i < g_rbac_manager.audit_log_count; i++) {
        if (!user_filter || strcmp(g_rbac_manager.audit_logs[i].user, user_filter) == 0) {
            count++;
        }
    }
    
    if (count == 0) {
        pthread_mutex_unlock(&g_rbac_manager.lock);
        *out = NULL;
        return 0;
    }
    
    *out = malloc(count * sizeof(rbac_audit_event_t));
    int idx = 0;
    for (int i = 0; i < g_rbac_manager.audit_log_count; i++) {
        if (!user_filter || strcmp(g_rbac_manager.audit_logs[i].user, user_filter) == 0) {
            memcpy(&(*out)[idx++], &g_rbac_manager.audit_logs[i], sizeof(rbac_audit_event_t));
        }
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return count;
}

int rbac_clear_old_audit_logs(int max_age_seconds) {
    if (max_age_seconds <= 0) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rbac_manager.lock);
    
    time_t cutoff = time(NULL) - max_age_seconds;
    int cleared = 0;
    
    for (int i = 0; i < g_rbac_manager.audit_log_count; i++) {
        if (g_rbac_manager.audit_logs[i].timestamp < cutoff) {
            /* Shift remaining logs */
            for (int j = i; j < g_rbac_manager.audit_log_count - 1; j++) {
                memcpy(&g_rbac_manager.audit_logs[j],
                      &g_rbac_manager.audit_logs[j + 1],
                      sizeof(rbac_audit_event_t));
            }
            g_rbac_manager.audit_log_count--;
            cleared++;
            i--;  /* Re-check this position */
        }
    }
    
    pthread_mutex_unlock(&g_rbac_manager.lock);
    return cleared;
}

/* ============================================================================
   Default Roles and Bindings
   ============================================================================ */

int rbac_create_default_roles(void) {
    /* Create system:masters ClusterRole (admin) */
    rbac_role_t admin_role = {
        .name = "system:masters",
    };
    admin_role.rule_count = 1;
    
    /* Add wildcard rule (admin access to everything) */
    rbac_rule_t wildcard_rule = {0};
    strcpy(wildcard_rule.api_groups[0], "*");
    wildcard_rule.api_group_count = 1;
    strcpy(wildcard_rule.resources[0], "*");
    wildcard_rule.resource_count = 1;
    strcpy(wildcard_rule.verbs[0], "*");
    wildcard_rule.verb_count = 1;
    
    memcpy(&admin_role.rules[0], &wildcard_rule, sizeof(rbac_rule_t));
    
    if (rbac_create_cluster_role(&admin_role) != 0) {
        fprintf(stderr, "[RBAC] Failed to create system:masters ClusterRole\n");
        return -1;
    }
    
    printf("[RBAC] Created system:masters ClusterRole\n");
    
    return 0;
}

int rbac_create_admin_binding(const char* user) {
    if (!user) {
        return -1;
    }
    
    rbac_role_binding_t binding = {0};
    snprintf(binding.name, sizeof(binding.name), "admin-%s", user);
    snprintf(binding.role_name, sizeof(binding.role_name), "system:masters");
    strcpy(binding.role_kind, "ClusterRole");
    
    rbac_subject_t subject = {
        .type = RBAC_SUBJECT_USER,
    };
    strncpy(subject.name, user, sizeof(subject.name) - 1);
    
    binding.subjects[0] = subject;
    binding.subject_count = 1;
    
    return rbac_create_cluster_role_binding(&binding);
}

int rbac_create_readonly_binding(const char* group) {
    if (!group) {
        return -1;
    }
    
    rbac_role_binding_t binding = {0};
    snprintf(binding.name, sizeof(binding.name), "readonly-%s", group);
    snprintf(binding.role_name, sizeof(binding.role_name), "system:unauthenticated");
    strcpy(binding.role_kind, "ClusterRole");
    
    rbac_subject_t subject = {
        .type = RBAC_SUBJECT_GROUP,
    };
    strncpy(subject.name, group, sizeof(subject.name) - 1);
    
    binding.subjects[0] = subject;
    binding.subject_count = 1;
    
    return rbac_create_cluster_role_binding(&binding);
}

rbac_rule_t rbac_create_rule_for_resources(
    const char* api_group,
    const char** resources,
    const char** verbs,
    int allow_all_names) {
    
    rbac_rule_t rule = {0};
    
    if (api_group) {
        strncpy(rule.api_groups[0], api_group, sizeof(rule.api_groups[0]) - 1);
        rule.api_group_count = 1;
    }
    
    if (resources) {
        int i = 0;
        while (resources[i] && i < 10) {
            strncpy(rule.resources[i], resources[i], sizeof(rule.resources[i]) - 1);
            i++;
        }
        rule.resource_count = i;
    }
    
    if (verbs) {
        int i = 0;
        while (verbs[i] && i < 10) {
            strncpy(rule.verbs[i], verbs[i], sizeof(rule.verbs[i]) - 1);
            i++;
        }
        rule.verb_count = i;
    }
    
    if (allow_all_names) {
        strcpy(rule.resource_names[0], "*");
        rule.resource_name_count = 1;
    }
    
    return rule;
}
