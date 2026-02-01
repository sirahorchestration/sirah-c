#ifndef K8S_AUTHZ_H
#define K8S_AUTHZ_H

#include <types/rbac.h>

/**
 * Authorization Engine for RBAC
 * 
 * Handles authorization decisions based on Roles, RoleBindings,
 * ClusterRoles, and ClusterRoleBindings.
 */

typedef struct k8s_authz_engine k8s_authz_engine_t;

/**
 * Create new authorization engine
 */
k8s_authz_engine_t* k8s_authz_engine_new();

/**
 * Free authorization engine
 */
void k8s_authz_engine_free(k8s_authz_engine_t* engine);

/**
 * Store a Role in the engine
 */
int k8s_authz_add_role(k8s_authz_engine_t* engine, k8s_role_t* role);

/**
 * Store a RoleBinding in the engine
 */
int k8s_authz_add_role_binding(k8s_authz_engine_t* engine, k8s_role_binding_t* binding);

/**
 * Store a ClusterRole in the engine
 */
int k8s_authz_add_cluster_role(k8s_authz_engine_t* engine, k8s_cluster_role_t* role);

/**
 * Store a ClusterRoleBinding in the engine
 */
int k8s_authz_add_cluster_role_binding(k8s_authz_engine_t* engine, 
                                      k8s_cluster_role_binding_t* binding);

/**
 * Authorize a request
 * 
 * Returns true if the user is allowed to perform the action.
 * The reason field contains explanation of why allowed or denied.
 */
bool k8s_authz_authorize(k8s_authz_engine_t* engine,
                        k8s_authz_request_t* req,
                        char** reason);

/**
 * Initialize default roles (cluster-admin, edit, view)
 */
int k8s_authz_create_default_roles(k8s_authz_engine_t* engine);

/**
 * Enable or disable RBAC enforcement
 */
void k8s_authz_set_enabled(k8s_authz_engine_t* engine, bool enabled);

/**
 * Get authorization state
 */
bool k8s_authz_is_enabled(k8s_authz_engine_t* engine);

#endif // K8S_AUTHZ_H
