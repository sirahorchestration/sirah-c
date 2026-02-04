/**
 * RBAC Middleware Implementation
 * 
 * Provides authorization checking middleware for API handlers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include "rbac_middleware.h"
#include "../rbac/rbac_manager.h"

/* External RBAC manager - defined in rbac_manager.c */
extern rbac_manager_t g_rbac_manager;

/**
 * Extract username from Authorization header
 * Format: "Bearer username"
 */
int rbac_middleware_extract_user(const char* auth_header, char* username_out) {
    if (!auth_header || !username_out) {
        return -1;
    }
    
    /* Look for "Bearer " prefix */
    const char* prefix = "Bearer ";
    if (strncmp(auth_header, prefix, strlen(prefix)) != 0) {
        return -1;
    }
    
    const char* user_start = auth_header + strlen(prefix);
    
    /* Extract until space or end of string */
    int i = 0;
    while (user_start[i] != '\0' && user_start[i] != ' ' && i < 255) {
        username_out[i] = user_start[i];
        i++;
    }
    username_out[i] = '\0';
    
    return (i > 0) ? 0 : -1;
}

/**
 * Map HTTP method to RBAC verb
 */
int rbac_middleware_method_to_verb(const char* method, char* verb_out) {
    if (!method || !verb_out) {
        return -1;
    }
    
    if (strcmp(method, "GET") == 0) {
        strcpy(verb_out, "get");
        return 0;
    }
    if (strcmp(method, "HEAD") == 0) {
        strcpy(verb_out, "get");  /* HEAD treated as GET for authorization */
        return 0;
    }
    if (strcmp(method, "POST") == 0) {
        strcpy(verb_out, "create");
        return 0;
    }
    if (strcmp(method, "PUT") == 0) {
        strcpy(verb_out, "update");
        return 0;
    }
    if (strcmp(method, "PATCH") == 0) {
        strcpy(verb_out, "patch");
        return 0;
    }
    if (strcmp(method, "DELETE") == 0) {
        strcpy(verb_out, "delete");
        return 0;
    }
    
    return -1;
}

/**
 * Format authorization denial response
 * Returns standard Kubernetes 403 Forbidden response
 */
int rbac_middleware_format_denial(
    const char* user,
    const char* resource,
    const char* namespace,
    const char* verb,
    const char* reason,
    char* response_buffer,
    int* response_code) {
    
    if (!response_buffer || !response_code) {
        return -1;
    }
    
    json_object* root = json_object_new_object();
    json_object* metadata = json_object_new_object();
    json_object* status = json_object_new_object();
    
    /* Kubernetes standard error response format */
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("Status"));
    
    /* Metadata */
    json_object_object_add(metadata, "name", json_object_new_string(resource));
    if (namespace && strlen(namespace) > 0) {
        json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    }
    json_object_object_add(root, "metadata", metadata);
    
    /* Status details */
    json_object_object_add(status, "group", json_object_new_string(""));
    json_object_object_add(status, "kind", json_object_new_string(resource));
    json_object_object_add(status, "name", 
        json_object_new_string((resource && strlen(resource) > 0) ? resource : ""));
    
    json_object_object_add(root, "status", json_object_new_string("Failure"));
    json_object_object_add(root, "message", 
        json_object_new_string(reason ? reason : "User not authorized to perform this action"));
    
    json_object_object_add(root, "reason", json_object_new_string("Forbidden"));
    json_object_object_add(root, "code", json_object_new_int(403));
    
    /* Details about the denial */
    json_object* details = json_object_new_object();
    json_object_object_add(details, "user", json_object_new_string(user ? user : "unknown"));
    json_object_object_add(details, "resource", json_object_new_string(resource ? resource : "unknown"));
    json_object_object_add(details, "action", json_object_new_string(verb ? verb : "unknown"));
    if (namespace && strlen(namespace) > 0) {
        json_object_object_add(details, "namespace", json_object_new_string(namespace));
    }
    json_object_object_add(root, "details", details);
    
    const char* json_str = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    strncpy(response_buffer, json_str, 16383);
    response_buffer[16383] = '\0';
    
    *response_code = 403;
    
    json_object_put(root);
    return 0;
}

/**
 * Check authorization for an API request
 * 
 * Main entry point for RBAC middleware in handler pipeline.
 */
int rbac_middleware_check_request(
    const char* auth_header,
    const char* method,
    const char* path,
    const char* api_group,
    const char* resource,
    const char* resource_name,
    const char* namespace,
    rbac_policy_decision_t* decision) {
    
    if (!auth_header || !method || !resource || !decision) {
        return -1;
    }
    
    /* Extract username from header */
    char username[256];
    if (rbac_middleware_extract_user(auth_header, username) != 0) {
        /* No valid user in header - deny */
        decision->decision = RBAC_DENY;
        strcpy(decision->reason, "Invalid or missing Authorization header");
        return -1;
    }
    
    /* Map HTTP method to RBAC verb */
    char verb[32];
    if (rbac_middleware_method_to_verb(method, verb) != 0) {
        decision->decision = RBAC_DENY;
        strcpy(decision->reason, "Unknown HTTP method");
        return -1;
    }
    
    /* Default namespace if not specified */
    const char* target_namespace = (namespace && strlen(namespace) > 0) ? namespace : "default";
    
    /* Check authorization with RBAC manager */
    int auth_result = rbac_can_perform_action(
        username,           /* user */
        "",                 /* groups (could extract from header) */
        verb,               /* verb */
        api_group ? api_group : "",  /* api_group */
        resource,           /* resource */
        target_namespace,   /* namespace */
        resource_name ? resource_name : "",  /* resource_name */
        decision
    );
    
    return auth_result;
}
