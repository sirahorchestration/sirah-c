#ifndef K8S_RBAC_H
#define K8S_RBAC_H

#include "common.h"
#include <stdbool.h>

/**
 * RBAC (Role-Based Access Control) Types
 * 
 * Implements Kubernetes RBAC for fine-grained permission management:
 * - Role: Namespace-scoped permissions
 * - RoleBinding: Assigns Role to users/service accounts in a namespace
 * - ClusterRole: Cluster-scoped permissions
 * - ClusterRoleBinding: Assigns ClusterRole to users/service accounts cluster-wide
 * - ServiceAccount: Identity for pods
 */

// Maximum counts
#define K8S_MAX_POLICY_RULES 100
#define K8S_MAX_VERBS 20
#define K8S_MAX_API_GROUPS 10
#define K8S_MAX_RESOURCES 10
#define K8S_MAX_RESOURCE_NAMES 10
#define K8S_MAX_SUBJECTS 100

// Policy rule - defines what can be done
typedef struct {
    char* verbs[K8S_MAX_VERBS];                    // "get", "create", "delete", "*"
    int num_verbs;
    
    char* api_groups[K8S_MAX_API_GROUPS];         // "apps", "core", "*", ""
    int num_apigroups;
    
    char* resources[K8S_MAX_RESOURCES];           // "pods", "deployments", "*"
    int num_resources;
    
    char* resource_names[K8S_MAX_RESOURCE_NAMES]; // Specific names (optional)
    int num_resourcenames;
} k8s_policy_rule_t;

// Role - namespace-scoped permissions
typedef struct {
    k8s_metadata_t* metadata;      // name, namespace, uid, timestamps
    
    struct {
        k8s_policy_rule_t rules[K8S_MAX_POLICY_RULES];
        int num_rules;
    } spec;
} k8s_role_t;

// Subject of a role binding (user, service account, or group)
typedef struct {
    char* kind;                    // "User", "ServiceAccount", "Group"
    char* name;
    char* namespace;               // Only for ServiceAccount
} k8s_subject_t;

// RoleBinding - assigns Role to users/service accounts in namespace
typedef struct {
    k8s_metadata_t* metadata;      // name, namespace
    
    struct {
        char* role_name;           // Name of Role to bind (no namespace - must be in same namespace)
        
        k8s_subject_t subjects[K8S_MAX_SUBJECTS];
        int num_subjects;
    } spec;
} k8s_role_binding_t;

// ClusterRole - cluster-scoped permissions
typedef struct {
    k8s_metadata_t* metadata;      // name, NO namespace
    
    struct {
        k8s_policy_rule_t rules[K8S_MAX_POLICY_RULES];
        int num_rules;
    } spec;
} k8s_cluster_role_t;

// ClusterRoleBinding - cluster-scoped role assignments
typedef struct {
    k8s_metadata_t* metadata;      // name, NO namespace
    
    struct {
        char* cluster_role_name;   // Name of ClusterRole to bind
        
        k8s_subject_t subjects[K8S_MAX_SUBJECTS];
        int num_subjects;
    } spec;
} k8s_cluster_role_binding_t;

// ServiceAccount - identity for pods
typedef struct {
    k8s_metadata_t* metadata;      // name, namespace
    
    struct {
        bool automount_service_account_token;
    } spec;
} k8s_service_account_t;

// Authorization request
typedef struct {
    char* user;                    // Username from certificate
    char* namespace;               // Namespace for request (empty for cluster-scoped)
    char* verb;                    // "get", "create", "list", "delete", "watch", etc.
    char* api_group;               // "apps", "core", "", etc.
    char* resource;                // "pods", "deployments", "roles", etc.
    char* resource_name;           // Specific resource name (optional, for get/delete)
    char* kind;                    // "User", "ServiceAccount"
} k8s_authz_request_t;

// Authorization result
typedef struct {
    bool allowed;
    char* reason;                  // Why allowed or denied
} k8s_authz_result_t;

// ==================== Role Functions ====================

/**
 * Create new Role
 */
k8s_role_t* k8s_role_new(const char* name, const char* namespace);

/**
 * Free Role
 */
void k8s_role_free(k8s_role_t* role);

/**
 * Add policy rule to Role
 */
int k8s_role_add_rule(k8s_role_t* role, k8s_policy_rule_t* rule);

/**
 * Convert Role to JSON
 */
char* k8s_role_to_json(k8s_role_t* role);

/**
 * Create Role from JSON
 */
k8s_role_t* k8s_role_from_json(const char* json);

// ==================== RoleBinding Functions ====================

/**
 * Create new RoleBinding
 */
k8s_role_binding_t* k8s_role_binding_new(const char* name, const char* namespace,
                                        const char* role_name);

/**
 * Free RoleBinding
 */
void k8s_role_binding_free(k8s_role_binding_t* binding);

/**
 * Add subject to RoleBinding
 */
int k8s_role_binding_add_subject(k8s_role_binding_t* binding,
                                const char* kind,
                                const char* name,
                                const char* namespace);

/**
 * Convert RoleBinding to JSON
 */
char* k8s_role_binding_to_json(k8s_role_binding_t* binding);

/**
 * Create RoleBinding from JSON
 */
k8s_role_binding_t* k8s_role_binding_from_json(const char* json);

// ==================== ClusterRole Functions ====================

/**
 * Create new ClusterRole
 */
k8s_cluster_role_t* k8s_cluster_role_new(const char* name);

/**
 * Free ClusterRole
 */
void k8s_cluster_role_free(k8s_cluster_role_t* role);

/**
 * Add policy rule to ClusterRole
 */
int k8s_cluster_role_add_rule(k8s_cluster_role_t* role, k8s_policy_rule_t* rule);

/**
 * Convert ClusterRole to JSON
 */
char* k8s_cluster_role_to_json(k8s_cluster_role_t* role);

/**
 * Create ClusterRole from JSON
 */
k8s_cluster_role_t* k8s_cluster_role_from_json(const char* json);

// ==================== ClusterRoleBinding Functions ====================

/**
 * Create new ClusterRoleBinding
 */
k8s_cluster_role_binding_t* k8s_cluster_role_binding_new(const char* name,
                                                        const char* cluster_role_name);

/**
 * Free ClusterRoleBinding
 */
void k8s_cluster_role_binding_free(k8s_cluster_role_binding_t* binding);

/**
 * Add subject to ClusterRoleBinding
 */
int k8s_cluster_role_binding_add_subject(k8s_cluster_role_binding_t* binding,
                                        const char* kind,
                                        const char* name,
                                        const char* namespace);

/**
 * Convert ClusterRoleBinding to JSON
 */
char* k8s_cluster_role_binding_to_json(k8s_cluster_role_binding_t* binding);

/**
 * Create ClusterRoleBinding from JSON
 */
k8s_cluster_role_binding_t* k8s_cluster_role_binding_from_json(const char* json);

// ==================== ServiceAccount Functions ====================

/**
 * Create new ServiceAccount
 */
k8s_service_account_t* k8s_service_account_new(const char* name, const char* namespace);

/**
 * Free ServiceAccount
 */
void k8s_service_account_free(k8s_service_account_t* sa);

/**
 * Convert ServiceAccount to JSON
 */
char* k8s_service_account_to_json(k8s_service_account_t* sa);

/**
 * Create ServiceAccount from JSON
 */
k8s_service_account_t* k8s_service_account_from_json(const char* json);

// ==================== Policy Rule Functions ====================

/**
 * Create policy rule
 */
k8s_policy_rule_t* k8s_policy_rule_new();

/**
 * Free policy rule
 */
void k8s_policy_rule_free(k8s_policy_rule_t* rule);

/**
 * Add verb to rule ("get", "create", "*", etc.)
 */
int k8s_policy_rule_add_verb(k8s_policy_rule_t* rule, const char* verb);

/**
 * Add API group to rule ("apps", "core", "*", etc.)
 */
int k8s_policy_rule_add_api_group(k8s_policy_rule_t* rule, const char* group);

/**
 * Add resource to rule ("pods", "deployments", "*", etc.)
 */
int k8s_policy_rule_add_resource(k8s_policy_rule_t* rule, const char* resource);

/**
 * Add resource name to rule (specific names like "my-pod")
 */
int k8s_policy_rule_add_resource_name(k8s_policy_rule_t* rule, const char* name);

#endif // K8S_RBAC_H
