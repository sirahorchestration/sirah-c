/**
 * RBAC Middleware for API Handler
 * 
 * Provides authorization checking middleware for API handlers.
 * Integrates RBAC policy enforcement into the request processing pipeline.
 */

#ifndef SIRAH_RBAC_MIDDLEWARE_H
#define SIRAH_RBAC_MIDDLEWARE_H

#include "../rbac/rbac_manager.h"

/**
 * Check authorization for an API request
 * 
 * Extracts user identity from Authorization header,
 * determines the resource being accessed, and checks
 * if the user has permission to perform the action.
 * 
 * @param auth_header - Authorization header value (e.g., "Bearer username")
 * @param method - HTTP method (GET, POST, PATCH, DELETE)
 * @param path - Request path (e.g., "/api/v1/namespaces/default/pods/mypod")
 * @param api_group - API group ("" for core, "apps", "batch", etc)
 * @param resource - Resource type ("pods", "deployments", etc)
 * @param resource_name - Specific resource name, or empty for list/create
 * @param namespace - Target namespace
 * @param decision - OUT: Authorization decision with reason
 * 
 * @return 0 if authorized, -1 if denied, 1 if no opinion
 */
int rbac_middleware_check_request(
    const char* auth_header,
    const char* method,
    const char* path,
    const char* api_group,
    const char* resource,
    const char* resource_name,
    const char* namespace,
    rbac_policy_decision_t* decision
);

/**
 * Extract username from Authorization header
 * 
 * Supports formats:
 * - "Bearer username" -> extracts "username"
 * - "Bearer system:serviceaccount:namespace:name" -> keeps full format
 * 
 * @param auth_header - Authorization header value
 * @param username_out - OUT: Extracted username (max 256 bytes)
 * 
 * @return 0 on success, -1 on failure
 */
int rbac_middleware_extract_user(const char* auth_header, char* username_out);

/**
 * Map HTTP method to RBAC verb
 * 
 * @param method - HTTP method (GET, POST, PATCH, DELETE, etc)
 * @param verb_out - OUT: RBAC verb (get, list, create, update, patch, delete)
 * 
 * @return 0 on success, -1 on unmapped method
 */
int rbac_middleware_method_to_verb(const char* method, char* verb_out);

/**
 * Format authorization denial response
 * 
 * Creates a standard Kubernetes 403 Forbidden JSON response
 * with details about the authorization failure.
 * 
 * @param user - Username that was denied
 * @param resource - Resource being accessed
 * @param namespace - Namespace of resource
 * @param verb - Action being denied
 * @param reason - Authorization denial reason
 * @param response_buffer - OUT: JSON response body
 * @param response_code - OUT: HTTP response code (403)
 * 
 * @return 0 on success
 */
int rbac_middleware_format_denial(
    const char* user,
    const char* resource,
    const char* namespace,
    const char* verb,
    const char* reason,
    char* response_buffer,
    int* response_code
);

#endif /* SIRAH_RBAC_MIDDLEWARE_H */
