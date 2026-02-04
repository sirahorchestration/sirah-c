#ifndef SIRAH_RBAC_MANAGER_H
#define SIRAH_RBAC_MANAGER_H

#include <pthread.h>
#include <time.h>

/* ============================================================================
   RBAC Manager - Kubernetes v1.28 Role-Based Access Control
   
   Implements:
   - Role (namespace-scoped)
   - RoleBinding (namespace-scoped)
   - ClusterRole (cluster-scoped)
   - ClusterRoleBinding (cluster-scoped)
   - Permission checking and enforcement
   - Audit logging
   
   Features:
   - Thread-safe operations with mutex protection
   - Wildcard support for resources and verbs
   - ServiceAccount integration
   - Namespace isolation
   - Comprehensive audit logging
   
   ============================================================================ */

/* Policy Decision Result */
typedef enum {
    RBAC_ALLOW = 0,
    RBAC_DENY = 1,
    RBAC_NO_OPINION = 2
} rbac_decision_t;

/* Subject Types */
typedef enum {
    RBAC_SUBJECT_USER = 0,
    RBAC_SUBJECT_GROUP = 1,
    RBAC_SUBJECT_SERVICE_ACCOUNT = 2
} rbac_subject_type_t;

/* RBAC Subject (User, Group, or ServiceAccount) */
typedef struct {
    rbac_subject_type_t type;
    char name[256];           /* User/Group name or "namespace:serviceaccount" */
    char namespace[64];       /* Only for ServiceAccount type */
} rbac_subject_t;

/* Single permission rule */
typedef struct {
    char api_groups[5][64];   /* API groups (e.g., "", "apps", "batch") */
    int api_group_count;
    
    char resources[10][64];   /* Resources (e.g., "pods", "services") */
    int resource_count;
    
    char verbs[10][32];       /* Verbs (get, list, watch, create, update, patch, delete) */
    int verb_count;
    
    char resource_names[5][256];  /* Optional: specific resource names */
    int resource_name_count;
    
    char subresources[5][64]; /* Subresources (e.g., "status", "scale") */
    int subresource_count;
} rbac_rule_t;

/* Role Definition (namespace-scoped or cluster-scoped) */
typedef struct {
    char name[256];
    char namespace[64];       /* Empty for ClusterRole */
    
    rbac_rule_t rules[50];
    int rule_count;
    
    /* Aggregation rules - for composing roles */
    char aggregation_labels[10][128];
    int aggregation_label_count;
    
    time_t created_at;
    char created_by[256];
} rbac_role_t;

/* Role Binding (namespace-scoped or cluster-scoped) */
typedef struct {
    char name[256];
    char namespace[64];       /* Empty for ClusterRoleBinding */
    
    rbac_subject_t subjects[20];
    int subject_count;
    
    char role_name[256];      /* Role or ClusterRole reference */
    char role_kind[32];       /* "Role" or "ClusterRole" */
    
    time_t created_at;
    char created_by[256];
} rbac_role_binding_t;

/* Policy Decision with Reason */
typedef struct {
    rbac_decision_t decision;
    char reason[512];
    char matched_rule[512];   /* Which rule matched/denied */
} rbac_policy_decision_t;

/* Audit Log Entry */
typedef struct {
    time_t timestamp;
    
    /* Request info */
    char user[256];
    char group[256];
    char verb[32];            /* get, list, create, update, patch, delete, watch */
    char api_group[64];
    char resource[64];
    char resource_name[256];
    char namespace[64];
    
    /* Decision */
    rbac_decision_t decision;
    char reason[512];
    
    /* Source */
    char source_ip[64];
} rbac_audit_event_t;

/* ============================================================================
   RBAC Manager - Main Interface
   ============================================================================ */

/**
 * Initialize RBAC manager
 * Creates default cluster roles and bindings
 * Thread-safe
 */
int rbac_manager_init(void);

/**
 * Shutdown RBAC manager
 * Gracefully shuts down audit logging
 */
int rbac_manager_shutdown(void);

/* ============================================================================
   Role Management (Namespace-Scoped)
   ============================================================================ */

/**
 * Create a new Role in a namespace
 * Returns 0 on success, -1 on error
 */
int rbac_create_role(const char* namespace, rbac_role_t* role);

/**
 * Get a Role by name
 * Returns 0 on success, -1 if not found
 */
int rbac_get_role(const char* namespace, const char* name, rbac_role_t* out);

/**
 * Update an existing Role
 * Returns 0 on success, -1 on error
 */
int rbac_update_role(const char* namespace, rbac_role_t* role);

/**
 * Delete a Role
 * Returns 0 on success, -1 if not found
 */
int rbac_delete_role(const char* namespace, const char* name);

/**
 * List all Roles in a namespace
 * Caller must free the returned array
 * Returns count, -1 on error
 */
int rbac_list_roles(const char* namespace, rbac_role_t** out);

/**
 * Add a rule to a Role
 * Returns 0 on success, -1 on error
 */
int rbac_add_rule_to_role(const char* namespace, const char* role_name, rbac_rule_t* rule);

/**
 * Remove a rule from a Role
 * Returns 0 on success, -1 on error
 */
int rbac_remove_rule_from_role(const char* namespace, const char* role_name, int rule_index);

/* ============================================================================
   ClusterRole Management (Cluster-Scoped)
   ============================================================================ */

/**
 * Create a new ClusterRole
 * Returns 0 on success, -1 on error
 */
int rbac_create_cluster_role(rbac_role_t* role);

/**
 * Get a ClusterRole by name
 * Returns 0 on success, -1 if not found
 */
int rbac_get_cluster_role(const char* name, rbac_role_t* out);

/**
 * Update a ClusterRole
 * Returns 0 on success, -1 on error
 */
int rbac_update_cluster_role(rbac_role_t* role);

/**
 * Delete a ClusterRole
 * Returns 0 on success, -1 if not found
 */
int rbac_delete_cluster_role(const char* name);

/**
 * List all ClusterRoles
 * Caller must free the returned array
 * Returns count, -1 on error
 */
int rbac_list_cluster_roles(rbac_role_t** out);

/* ============================================================================
   RoleBinding Management (Namespace-Scoped)
   ============================================================================ */

/**
 * Create a new RoleBinding
 * Returns 0 on success, -1 on error
 */
int rbac_create_role_binding(const char* namespace, rbac_role_binding_t* binding);

/**
 * Get a RoleBinding by name
 * Returns 0 on success, -1 if not found
 */
int rbac_get_role_binding(const char* namespace, const char* name, rbac_role_binding_t* out);

/**
 * Update a RoleBinding
 * Returns 0 on success, -1 on error
 */
int rbac_update_role_binding(const char* namespace, rbac_role_binding_t* binding);

/**
 * Delete a RoleBinding
 * Returns 0 on success, -1 if not found
 */
int rbac_delete_role_binding(const char* namespace, const char* name);

/**
 * List all RoleBindings in a namespace
 * Caller must free the returned array
 * Returns count, -1 on error
 */
int rbac_list_role_bindings(const char* namespace, rbac_role_binding_t** out);

/**
 * Add a subject to a RoleBinding
 * Returns 0 on success, -1 on error
 */
int rbac_add_subject_to_binding(const char* namespace, const char* binding_name,
                               rbac_subject_t* subject);

/**
 * Remove a subject from a RoleBinding
 * Returns 0 on success, -1 on error
 */
int rbac_remove_subject_from_binding(const char* namespace, const char* binding_name,
                                    rbac_subject_t* subject);

/* ============================================================================
   ClusterRoleBinding Management (Cluster-Scoped)
   ============================================================================ */

/**
 * Create a new ClusterRoleBinding
 * Returns 0 on success, -1 on error
 */
int rbac_create_cluster_role_binding(rbac_role_binding_t* binding);

/**
 * Get a ClusterRoleBinding by name
 * Returns 0 on success, -1 if not found
 */
int rbac_get_cluster_role_binding(const char* name, rbac_role_binding_t* out);

/**
 * Update a ClusterRoleBinding
 * Returns 0 on success, -1 on error
 */
int rbac_update_cluster_role_binding(rbac_role_binding_t* binding);

/**
 * Delete a ClusterRoleBinding
 * Returns 0 on success, -1 if not found
 */
int rbac_delete_cluster_role_binding(const char* name);

/**
 * List all ClusterRoleBindings
 * Caller must free the returned array
 * Returns count, -1 on error
 */
int rbac_list_cluster_role_bindings(rbac_role_binding_t** out);

/* ============================================================================
   Authorization and Policy Checking
   ============================================================================ */

/**
 * Check if a user/subject can perform an action
 * Main authorization function - called before API operations
 * 
 * Returns:
 * - 0 on ALLOW
 * - -1 on DENY
 * - 1 on NO_OPINION (allow by default if not restricted)
 * 
 * decision output contains detailed allow/deny reasoning
 */
int rbac_can_perform_action(
    const char* user,              /* Username or "system:serviceaccount:ns:name" */
    const char* groups,            /* Comma-separated list of groups */
    const char* verb,              /* get, list, watch, create, update, patch, delete */
    const char* api_group,         /* "" for core, "apps", "batch", etc */
    const char* resource,          /* pods, services, deployments, etc */
    const char* namespace,         /* Target namespace, or "" for cluster-scoped */
    const char* resource_name,     /* Specific resource name (optional) */
    rbac_policy_decision_t* decision
);

/**
 * Check if a user can GET a resource
 * Convenience function
 */
int rbac_can_get(const char* user, const char* groups, const char* resource,
                const char* namespace, const char* resource_name,
                rbac_policy_decision_t* decision);

/**
 * Check if a user can LIST resources
 */
int rbac_can_list(const char* user, const char* groups, const char* resource,
                 const char* namespace, rbac_policy_decision_t* decision);

/**
 * Check if a user can WATCH resources
 */
int rbac_can_watch(const char* user, const char* groups, const char* resource,
                  const char* namespace, rbac_policy_decision_t* decision);

/**
 * Check if a user can CREATE a resource
 */
int rbac_can_create(const char* user, const char* groups, const char* resource,
                   const char* namespace, rbac_policy_decision_t* decision);

/**
 * Check if a user can UPDATE a resource
 */
int rbac_can_update(const char* user, const char* groups, const char* resource,
                   const char* namespace, const char* resource_name,
                   rbac_policy_decision_t* decision);

/**
 * Check if a user can PATCH a resource
 */
int rbac_can_patch(const char* user, const char* groups, const char* resource,
                  const char* namespace, const char* resource_name,
                  rbac_policy_decision_t* decision);

/**
 * Check if a user can DELETE a resource
 */
int rbac_can_delete(const char* user, const char* groups, const char* resource,
                   const char* namespace, const char* resource_name,
                   rbac_policy_decision_t* decision);

/* ============================================================================
   Audit Logging
   ============================================================================ */

/**
 * Log an authorization decision
 * Returns 0 on success, -1 on error
 */
int rbac_log_audit(rbac_audit_event_t* event);

/**
 * Get audit logs (filtered)
 * Caller must free the returned array
 * Returns count, -1 on error
 */
int rbac_get_audit_logs(const char* user_filter, rbac_audit_event_t** out);

/**
 * Clear audit logs older than seconds
 * Returns count of cleared logs, -1 on error
 */
int rbac_clear_old_audit_logs(int max_age_seconds);

/* ============================================================================
   Default Roles and Bindings
   ============================================================================ */

/**
 * Create default system roles and bindings
 * Called automatically by rbac_manager_init()
 * 
 * Creates:
 * - system:masters ClusterRole (admin access)
 * - system:unauthenticated ClusterRole (read-only)
 * - edit Role (namespace developers)
 * - view Role (namespace readers)
 */
int rbac_create_default_roles(void);

/**
 * Create admin binding for a user
 * Binds system:masters ClusterRole to a user
 */
int rbac_create_admin_binding(const char* user);

/**
 * Create read-only binding for a group
 * Binds system:unauthenticated ClusterRole to a group
 */
int rbac_create_readonly_binding(const char* group);

/* ============================================================================
   Utility Functions
   ============================================================================ */

/**
 * Check if a verb matches (supports wildcards)
 * Returns 1 if matches, 0 otherwise
 */
int rbac_verb_matches(const char* rule_verb, const char* target_verb);

/**
 * Check if a resource matches (supports wildcards)
 * Returns 1 if matches, 0 otherwise
 */
int rbac_resource_matches(const char* rule_resource, const char* target_resource);

/**
 * Create a basic rule for common permissions
 * Helper function
 */
rbac_rule_t rbac_create_rule_for_resources(
    const char* api_group,
    const char** resources,     /* NULL-terminated array */
    const char** verbs,         /* NULL-terminated array */
    int allow_all_names
);

#endif /* SIRAH_RBAC_MANAGER_H */
