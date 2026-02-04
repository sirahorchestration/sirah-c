// internal/apiserver/endpoints_etcd_integration.c
// Etcd-integrated pod endpoint handlers (Phase 2A Week 2)
// Integrates etcd_manager into pod CRUD operations
// Replaces in-process storage with etcd-backed persistence

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>
#include <json-c/json.h>
#include "endpoints.h"
#include "kubectl_apply.h"
#include "../../../internal/etcd/etcd_manager.h"
#include "../../pkg/types/pod.h"

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Build etcd key path for a pod
 * Format: /sirah/pods/{namespace}/{name}
 */
static void build_pod_etcd_key(const char* namespace, const char* pod_name, 
                               char* key, int key_len) {
    snprintf(key, key_len, "/sirah/pods/%s/%s",
             namespace ? namespace : "default",
             pod_name ? pod_name : "unknown");
}

/**
 * Generate a UUID for a new pod
 */
static char* generate_pod_uid(void) {
    uuid_t uuid;
    char* uid_str = malloc(37);  // 36 chars + null terminator
    if (!uid_str) return NULL;
    
    uuid_generate(uuid);
    uuid_unparse(uuid, uid_str);
    return uid_str;
}

/**
 * Extract resourceVersion from etcd response revision
 */
static char* revision_to_resource_version(uint64_t revision) {
    char* rv = malloc(32);
    if (!rv) return NULL;
    snprintf(rv, 32, "%lu", revision);
    return rv;
}

/**
 * Merge patch data into current pod (simple field merge)
 */
static int merge_pod_patch(k8s_pod_t* current, const char* patch_json) {
    if (!patch_json) return 0;
    
    json_object* patch_obj = json_tokener_parse(patch_json);
    if (!patch_obj) return -1;
    
    // Merge spec if present
    json_object* patch_spec = json_object_object_get(patch_obj, "spec");
    if (patch_spec) {
        // For simplicity, replace entire spec
        // In production, would do field-level merge
    }
    
    // Merge metadata if present
    json_object* patch_meta = json_object_object_get(patch_obj, "metadata");
    if (patch_meta) {
        // Merge labels, annotations, etc.
        json_object* labels = json_object_object_get(patch_meta, "labels");
        if (labels) {
            // Update pod labels
        }
    }
    
    json_object_put(patch_obj);
    return 0;
}

// ============================================================================
// POST /api/v1/namespaces/{namespace}/pods - Create Pod with etcd
// ============================================================================

int endpoint_create_pod_etcd(const char* namespace, const char* body,
                             char* response_buffer, int* response_code) {
    
    if (!body || strlen(body) == 0) {
        snprintf(response_buffer, 16384, "{\"error\":\"empty body\"}");
        *response_code = 400;
        return -1;
    }
    
    // Parse pod JSON
    json_object* pod_obj = json_tokener_parse(body);
    if (!pod_obj) {
        snprintf(response_buffer, 16384, "{\"error\":\"invalid pod JSON\"}");
        *response_code = 400;
        return -1;
    }
    
    // Extract pod name from metadata
    json_object* metadata = json_object_object_get(pod_obj, "metadata");
    if (!metadata) {
        json_object_put(pod_obj);
        snprintf(response_buffer, 16384, "{\"error\":\"metadata required\"}");
        *response_code = 400;
        return -1;
    }
    
    const char* pod_name = json_object_get_string(json_object_object_get(metadata, "name"));
    if (!pod_name) {
        json_object_put(pod_obj);
        snprintf(response_buffer, 16384, "{\"error\":\"pod name required in metadata\"}");
        *response_code = 400;
        return -1;
    }
    
    // Build etcd key
    char etcd_key[512];
    build_pod_etcd_key(namespace, pod_name, etcd_key, sizeof(etcd_key));
    
    // Add status with Pending phase (server-created field) BEFORE storing
    // This ensures the status is persisted to etcd so it's available on GET
    json_object* status_obj = json_object_new_object();
    json_object_object_add(status_obj, "phase", json_object_new_string("Pending"));
    json_object_object_add(status_obj, "conditions", json_object_new_array());
    json_object_object_add(status_obj, "containerStatuses", json_object_new_array());
    json_object_object_add(pod_obj, "status", status_obj);
    
    // Ensure apiVersion and kind are set before storing
    if (!json_object_object_get(pod_obj, "apiVersion")) {
        json_object_object_add(pod_obj, "apiVersion", json_object_new_string("v1"));
    }
    if (!json_object_object_get(pod_obj, "kind")) {
        json_object_object_add(pod_obj, "kind", json_object_new_string("Pod"));
    }
    
    // Store in etcd (now includes status field)
    const char* pod_json = json_object_to_json_string(pod_obj);
    etcd_response_t etcd_resp = {0};
    etcd_status_t status = etcd_manager_put(etcd_key, pod_json, &etcd_resp);
    
    if (status == ETCD_OK) {
        // Build response with resourceVersion from etcd
        char rv_str[32];
        snprintf(rv_str, sizeof(rv_str), "%lu", etcd_resp.revision);
        json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
        
        // Add UID if not present
        if (!json_object_object_get(metadata, "uid")) {
            char uid[37];
            uuid_t uuid;
            uuid_generate(uuid);
            uuid_unparse(uuid, uid);
            json_object_object_add(metadata, "uid", json_object_new_string(uid));
        }
        
        // Return the pod object we just stored (now has status field)
        const char* response_json = json_object_to_json_string(pod_obj);
        strncpy(response_buffer, response_json, 16384 - 1);
        *response_code = 201;  // Created
        
    } else if (status == ETCD_UNAVAILABLE) {
        snprintf(response_buffer, 16384, 
            "{\"error\":\"etcd unavailable\",\"details\":\"control plane storage is unavailable\"}");
        *response_code = 503;
        
    } else if (status == ETCD_CIRCUIT_BREAKER_OPEN) {
        snprintf(response_buffer, 16384,
            "{\"error\":\"service unavailable\",\"details\":\"etcd circuit breaker open\"}");
        *response_code = 503;
        
    } else {
        snprintf(response_buffer, 16384,
            "{\"error\":\"storage error\",\"details\":\"failed to persist pod to etcd\"}");
        *response_code = 500;
    }
    
    etcd_response_free(&etcd_resp);
    json_object_put(pod_obj);
    
    return (status == ETCD_OK) ? 0 : -1;
}

// ============================================================================
// GET /api/v1/namespaces/{namespace}/pods/{name} - Retrieve Pod from etcd
// ============================================================================

int endpoint_get_pod_etcd(const char* namespace, const char* pod_name,
                          char* response_buffer, int* response_code) {
    
    if (!pod_name || strlen(pod_name) == 0) {
        snprintf(response_buffer, 16384, "{\"error\":\"pod name required\"}");
        *response_code = 400;
        return -1;
    }
    
    // Build etcd key
    char etcd_key[512];
    build_pod_etcd_key(namespace, pod_name, etcd_key, sizeof(etcd_key));
    
    // Get from etcd
    etcd_response_t etcd_resp = {0};
    etcd_status_t status = etcd_manager_get(etcd_key, &etcd_resp);
    
    if (status == ETCD_OK) {
        // Return stored pod JSON with resourceVersion from etcd revision
        // Parse the stored JSON to inject resourceVersion
        json_object* pod_obj = json_tokener_parse(etcd_resp.value);
        if (!pod_obj) {
            snprintf(response_buffer, 16384, "{\"error\":\"invalid pod data in storage\"}");
            *response_code = 500;
            etcd_response_free(&etcd_resp);
            return -1;
        }
        
        // Update resourceVersion from etcd revision
        json_object* metadata = json_object_object_get(pod_obj, "metadata");
        if (metadata) {
            char rv_str[32];
            snprintf(rv_str, sizeof(rv_str), "%lu", etcd_resp.revision);
            json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
        }
        
        // Return the pod JSON
        const char* pod_json = json_object_to_json_string(pod_obj);
        strncpy(response_buffer, pod_json, 16384 - 1);
        json_object_put(pod_obj);
        
        *response_code = 200;  // OK
        
    } else if (status == ETCD_NOT_FOUND) {
        snprintf(response_buffer, 16384,
            "{\"error\":\"pod not found\",\"kind\":\"Pod\",\"name\":\"%s\"}", pod_name);
        *response_code = 404;
        
    } else if (status == ETCD_UNAVAILABLE) {
        snprintf(response_buffer, 16384,
            "{\"error\":\"etcd unavailable\",\"details\":\"control plane storage is unavailable\"}");
        *response_code = 503;
        
    } else {
        snprintf(response_buffer, 16384,
            "{\"error\":\"storage error\",\"details\":\"failed to retrieve pod from etcd\"}");
        *response_code = 500;
    }
    
    etcd_response_free(&etcd_resp);
    
    return (status == ETCD_OK) ? 0 : -1;
}

// ============================================================================
// GET /api/v1/namespaces/{namespace}/pods - List Pods from etcd
// ============================================================================

int endpoint_list_pods_etcd(const char* namespace, char* response_buffer, int* response_code) {
    
    // Build etcd prefix for listing
    char etcd_prefix[512];
    snprintf(etcd_prefix, sizeof(etcd_prefix), "/sirah/pods/%s/",
             namespace ? namespace : "default");
    
    // Get list from etcd
    etcd_response_t etcd_resp = {0};
    etcd_status_t status = etcd_manager_list(etcd_prefix, &etcd_resp);
    
    if (status == ETCD_OK) {
        // Build JSON response with pod array
        json_object* root = json_object_new_object();
        json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
        json_object_object_add(root, "kind", json_object_new_string("PodList"));
        
        json_object* items = json_object_new_array();
        
        // Process each pod from etcd
        for (int i = 0; i < etcd_resp.kvs_count; i++) {
            // Parse pod JSON directly without converting to k8s_pod_t
            json_object* pod_obj = json_tokener_parse(etcd_resp.kvs_values[i]);
            if (!pod_obj) continue;
            
            // Update resourceVersion from etcd revision
            json_object* metadata = json_object_object_get(pod_obj, "metadata");
            if (metadata) {
                char rv_str[32];
                snprintf(rv_str, sizeof(rv_str), "%lu", etcd_resp.kvs_versions[i]);
                json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
            }
            
            json_object_array_add(items, pod_obj);
        }
        
        json_object_object_add(root, "items", items);
        
        // Serialize response
        const char* response_json = json_object_to_json_string(root);
        strncpy(response_buffer, response_json, 16384 - 1);
        
        json_object_put(root);
        *response_code = 200;
        
    } else if (status == ETCD_UNAVAILABLE) {
        snprintf(response_buffer, 16384,
            "{\"error\":\"etcd unavailable\",\"details\":\"control plane storage is unavailable\"}");
        *response_code = 503;
        
    } else {
        snprintf(response_buffer, 16384,
            "{\"error\":\"storage error\",\"details\":\"failed to list pods from etcd\"}");
        *response_code = 500;
    }
    
    etcd_response_free(&etcd_resp);
    
    return (status == ETCD_OK) ? 0 : -1;
}

// ============================================================================
// DELETE /api/v1/namespaces/{namespace}/pods/{name} - Delete Pod from etcd
// ============================================================================

int endpoint_delete_pod_etcd(const char* namespace, const char* pod_name,
                             char* response_buffer, int* response_code) {
    
    if (!pod_name || strlen(pod_name) == 0) {
        snprintf(response_buffer, 16384, "{\"error\":\"pod name required\"}");
        *response_code = 400;
        return -1;
    }
    
    // Build etcd key
    char etcd_key[512];
    build_pod_etcd_key(namespace, pod_name, etcd_key, sizeof(etcd_key));
    
    // Delete from etcd
    etcd_response_t etcd_resp = {0};
    etcd_status_t status = etcd_manager_delete(etcd_key, &etcd_resp);
    
    if (status == ETCD_OK) {
        // Return 204 No Content (successful deletion)
        response_buffer[0] = '\0';
        *response_code = 204;
        
    } else if (status == ETCD_NOT_FOUND) {
        snprintf(response_buffer, 16384,
            "{\"error\":\"pod not found\",\"kind\":\"Pod\",\"name\":\"%s\"}", pod_name);
        *response_code = 404;
        
    } else if (status == ETCD_UNAVAILABLE) {
        snprintf(response_buffer, 16384,
            "{\"error\":\"etcd unavailable\",\"details\":\"control plane storage is unavailable\"}");
        *response_code = 503;
        
    } else {
        snprintf(response_buffer, 16384,
            "{\"error\":\"storage error\",\"details\":\"failed to delete pod from etcd\"}");
        *response_code = 500;
    }
    
    etcd_response_free(&etcd_resp);
    
    return (status == ETCD_OK) ? 0 : -1;
}

// ============================================================================
// PATCH /api/v1/namespaces/{namespace}/pods/{name} - Update Pod (CAS)
// Supports both kubectl apply (strategic merge patch) and direct patch operations
// ============================================================================

int endpoint_patch_pod_etcd(const char* namespace, const char* pod_name, 
                            const char* body, const char* content_type,
                            char* response_buffer, int* response_code) {
    
    if (!pod_name || strlen(pod_name) == 0) {
        snprintf(response_buffer, 16384, "{\"error\":\"pod name required\"}");
        *response_code = 400;
        return -1;
    }
    
    if (!body || strlen(body) == 0) {
        snprintf(response_buffer, 16384, "{\"error\":\"patch body required\"}");
        *response_code = 400;
        return -1;
    }
    
    // Build etcd key
    char etcd_key[512];
    build_pod_etcd_key(namespace, pod_name, etcd_key, sizeof(etcd_key));
    
    // Step 1: GET current pod to obtain current revision
    etcd_response_t get_resp = {0};
    etcd_status_t get_status = etcd_manager_get(etcd_key, &get_resp);
    
    json_object* current_obj = NULL;
    if (get_status == ETCD_OK) {
        current_obj = json_tokener_parse(get_resp.value);
        if (!current_obj) {
            snprintf(response_buffer, 16384,
                "{\"error\":\"invalid pod data\",\"details\":\"failed to parse current pod\"}");
            *response_code = 500;
            etcd_response_free(&get_resp);
            return -1;
        }
    } else if (get_status != ETCD_NOT_FOUND) {
        snprintf(response_buffer, 16384,
            "{\"error\":\"storage error\",\"details\":\"failed to retrieve current pod\"}");
        *response_code = 500;
        etcd_response_free(&get_resp);
        return -1;
    }
    
    // Parse desired configuration from patch body
    json_object* patch_obj = json_tokener_parse(body);
    if (!patch_obj) {
        snprintf(response_buffer, 16384, "{\"error\":\"invalid patch JSON\"}");
        *response_code = 400;
        if (current_obj) json_object_put(current_obj);
        etcd_response_free(&get_resp);
        return -1;
    }
    
    uint64_t current_revision = get_resp.revision;
    
    // Determine if this is a kubectl apply operation
    // kubectl apply uses "application/apply-patch+json" or "application/strategic-merge-patch+json"
    int is_kubectl_apply = (content_type && 
        (strstr(content_type, "apply-patch") || 
         strstr(content_type, "strategic-merge-patch")));
    
    json_object* merged_obj = NULL;
    int http_status = 200;
    
    if (is_kubectl_apply && current_obj) {
        // Use kubectl apply 3-way merge
        kubectl_apply_result_t* apply_result = kubectl_apply_three_way_merge(
            current_obj, patch_obj, namespace, pod_name);
        
        if (!apply_result->success) {
            snprintf(response_buffer, 16384,
                "{\"error\":\"apply conflict\",\"message\":\"%s\"}", apply_result->error_message);
            *response_code = 409;
            
            kubectl_apply_result_free(apply_result);
            json_object_put(current_obj);
            json_object_put(patch_obj);
            etcd_response_free(&get_resp);
            return -1;
        }
        
        merged_obj = apply_result->result_obj;
        http_status = apply_result->response_code;
        free(apply_result);
    } else if (is_kubectl_apply && !current_obj) {
        // kubectl apply on non-existent object - create it
        json_object* copy = NULL;
        json_object_deep_copy(patch_obj, &copy, NULL);
        merged_obj = copy ? copy : patch_obj;
        
        // Store last-applied annotation
        const char* patch_json = json_object_to_json_string(patch_obj);
        kubectl_apply_set_last_applied(merged_obj, patch_json);
        
        http_status = 201;  // Created
    } else {
        // Regular PATCH operation (not kubectl apply) - simple field merge
        if (!current_obj) {
            // Can't patch non-existent object
            snprintf(response_buffer, 16384,
                "{\"error\":\"pod not found\",\"kind\":\"Pod\",\"name\":\"%s\"}", pod_name);
            *response_code = 404;
            json_object_put(patch_obj);
            etcd_response_free(&get_resp);
            return -1;
        }
        
        merged_obj = current_obj;
        
        // Simple merge: apply patch fields to current
        json_object_object_foreach(patch_obj, key, val) {
            json_object_object_add(merged_obj, key, json_object_get(val));
        }
    }
    
    // Check for resourceVersion in patch (for CAS validation)
    json_object* patch_meta = json_object_object_get(patch_obj, "metadata");
    if (patch_meta && current_obj) {
        json_object* patch_rv = json_object_object_get(patch_meta, "resourceVersion");
        if (patch_rv) {
            const char* patch_rv_str = json_object_get_string(patch_rv);
            if (patch_rv_str && strlen(patch_rv_str) > 0) {
                uint64_t patch_revision = strtoull(patch_rv_str, NULL, 10);
                if (patch_revision == 0 || patch_revision != current_revision) {
                    // CAS mismatch - return 409 Conflict
                    snprintf(response_buffer, 16384,
                        "{\"error\":\"Conflict\",\"message\":\"the object has been modified; please apply your changes to the latest version and try again\"}");
                    *response_code = 409;
                    
                    if (merged_obj && merged_obj != current_obj) {
                        json_object_put(merged_obj);
                    }
                    if (current_obj) json_object_put(current_obj);
                    json_object_put(patch_obj);
                    etcd_response_free(&get_resp);
                    return -1;
                }
            }
        }
    }
    
    // Store in etcd with CAS
    const char* merged_json = json_object_to_json_string(merged_obj);
    
    etcd_response_t patch_resp = {0};
    etcd_status_t patch_status = etcd_manager_patch(
        etcd_key,
        merged_json,
        current_revision,
        &patch_resp
    );
    
    if (patch_status == ETCD_CAS_FAILED) {
        snprintf(response_buffer, 16384,
            "{\"error\":\"Conflict\",\"message\":\"the object has been modified; please apply your changes to the latest version and try again\"}");
        *response_code = 409;
        
    } else if (patch_status == ETCD_OK) {
        // Update successful - return updated pod with new resourceVersion
        json_object* metadata = json_object_object_get(merged_obj, "metadata");
        if (metadata) {
            char rv_str[32];
            snprintf(rv_str, sizeof(rv_str), "%lu", patch_resp.revision);
            json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
        }
        
        const char* response_json = json_object_to_json_string(merged_obj);
        strncpy(response_buffer, response_json, 16384 - 1);
        
        *response_code = http_status;
        
    } else if (patch_status == ETCD_UNAVAILABLE) {
        snprintf(response_buffer, 16384,
            "{\"error\":\"etcd unavailable\",\"details\":\"control plane storage is unavailable\"}");
        *response_code = 503;
        
    } else {
        snprintf(response_buffer, 16384,
            "{\"error\":\"storage error\",\"details\":\"failed to update pod in etcd\"}");
        *response_code = 500;
    }
    
    // Cleanup
    if (merged_obj && merged_obj != current_obj) {
        json_object_put(merged_obj);
    }
    if (current_obj) {
        json_object_put(current_obj);
    }
    json_object_put(patch_obj);
    etcd_response_free(&get_resp);
    etcd_response_free(&patch_resp);
    
    return (patch_status == ETCD_OK) ? 0 : -1;
}

// ============================================================================
// Integration Note:
// ============================================================================
// These functions should replace the corresponding endpoints in endpoints.c:
//   - endpoint_create_pod → endpoint_create_pod_etcd
//   - endpoint_get_pod → endpoint_get_pod_etcd
//   - endpoint_list_pods → endpoint_list_pods_etcd
//   - endpoint_delete_pod → endpoint_delete_pod_etcd
//   - endpoint_patch_pod → endpoint_patch_pod_etcd (needs version check)
//
// Call these from handler.c after etcd_manager_init() is called at startup.
// ============================================================================
// ============================================================================
// SERVICE ETCD-BACKED ENDPOINTS (Phase 2C Integration)
// ============================================================================

static void build_service_etcd_key(const char* namespace, const char* service_name,
                                   char* key, int key_len) {
    snprintf(key, key_len, "/sirah/services/%s/%s",
             namespace ? namespace : "default",
             service_name ? service_name : "unknown");
}

/**
 * POST /api/v1/namespaces/{namespace}/services - Create Service with etcd
 */
int endpoint_create_service_etcd(const char* namespace, const char* body,
                                 char* response_buffer, int* response_code) {
    if (!body || strlen(body) == 0) {
        snprintf(response_buffer, 16384, "{\"error\":\"empty body\"}");
        *response_code = 400;
        return -1;
    }
    
    json_object* req = json_tokener_parse(body);
    if (!req) {
        snprintf(response_buffer, 16384, "{\"error\":\"invalid JSON\"}");
        *response_code = 400;
        return -1;
    }
    
    // Extract service name
    json_object* metadata = json_object_object_get(req, "metadata");
    if (!metadata) {
        snprintf(response_buffer, 16384, "{\"error\":\"metadata required\"}");
        *response_code = 400;
        json_object_put(req);
        return -1;
    }
    
    const char* svc_name = json_object_get_string(
        json_object_object_get(metadata, "name"));
    if (!svc_name) {
        snprintf(response_buffer, 16384, "{\"error\":\"service name required\"}");
        *response_code = 400;
        json_object_put(req);
        return -1;
    }
    
    // Add resourceVersion and timestamps
    char rv_str[32];
    snprintf(rv_str, sizeof(rv_str), "%lu", (unsigned long)time(NULL) * 1000);
    json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
    json_object_object_add(metadata, "generation", json_object_new_int64(0));
    
    const char* json_str = json_object_to_json_string(req);
    
    // Store in etcd
    char etcd_key[256];
    build_service_etcd_key(namespace, svc_name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t put_resp = {0};
    int status = etcd_manager_put(etcd_key, json_str, &put_resp);
    
    if (status == ETCD_OK) {
        // Update resourceVersion with etcd revision
        snprintf(rv_str, sizeof(rv_str), "%lu", put_resp.revision);
        json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
        
        // Ensure apiVersion and kind are present
        if (!json_object_object_get(req, "apiVersion")) {
            json_object_object_add(req, "apiVersion", json_object_new_string("v1"));
        }
        if (!json_object_object_get(req, "kind")) {
            json_object_object_add(req, "kind", json_object_new_string("Service"));
        }
        
        const char* response_json = json_object_to_json_string(req);
        strncpy(response_buffer, response_json, 16384 - 1);
        *response_code = 201;  // Created
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"failed to store service\"}");
        *response_code = 500;
    }
    
    json_object_put(req);
    etcd_response_free(&put_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

/**
 * GET /api/v1/namespaces/{namespace}/services/{name}
 */
int endpoint_get_service_etcd(const char* namespace, const char* name,
                              char* response_buffer, int* response_code) {
    char etcd_key[256];
    build_service_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t get_resp = {0};
    int status = etcd_manager_get(etcd_key, &get_resp);
    
    if (status == ETCD_OK && get_resp.value) {
        strncpy(response_buffer, get_resp.value, 16384 - 1);
        *response_code = 200;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"service not found\"}");
        *response_code = 404;
    }
    
    etcd_response_free(&get_resp);
    return (status == ETCD_OK && get_resp.value) ? 0 : -1;
}

/**
 * GET /api/v1/namespaces/{namespace}/services - List all services
 */
int endpoint_list_services_etcd(const char* namespace, char* response_buffer,
                                int* response_code) {
    char etcd_prefix[256];
    snprintf(etcd_prefix, sizeof(etcd_prefix), "/sirah/services/%s/",
             namespace ? namespace : "default");
    
    etcd_response_t list_resp = {0};
    int status = etcd_manager_list(etcd_prefix, &list_resp);
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("ServiceList"));
    json_object* items = json_object_new_array();
    
    if (status == ETCD_OK && list_resp.value) {
        // Parse each service from etcd list
        char* copy = strdup(list_resp.value);
        char* service_json = strtok(copy, "\n");
        while (service_json != NULL) {
            if (strlen(service_json) > 0) {
                json_object* svc_obj = json_tokener_parse(service_json);
                if (svc_obj) {
                    json_object_array_add(items, svc_obj);
                }
            }
            service_json = strtok(NULL, "\n");
        }
        free(copy);
    }
    
    json_object_object_add(root, "items", items);
    const char* json_str = json_object_to_json_string(root);
    strncpy(response_buffer, json_str, 16384 - 1);
    *response_code = 200;
    
    json_object_put(root);
    etcd_response_free(&list_resp);
    return 0;
}

/**
 * PATCH /api/v1/namespaces/{namespace}/services/{name}
 */
int endpoint_patch_service_etcd(const char* namespace, const char* name, const char* body,
                                const char* content_type, char* response_buffer,
                                int* response_code) {
    
    if (!body) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"empty body\"}");
        return -1;
    }
    
    char etcd_key[256];
    build_service_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    // Get current version
    etcd_response_t get_resp = {0};
    int get_status = etcd_manager_get(etcd_key, &get_resp);
    
    json_object* current_obj = NULL;
    if (get_status == ETCD_OK) {
        current_obj = json_tokener_parse(get_resp.value);
        if (!current_obj) {
            *response_code = 500;
            strcpy(response_buffer, "{\"error\":\"invalid service data\"}");
            etcd_response_free(&get_resp);
            return -1;
        }
    } else if (get_status != ETCD_NOT_FOUND) {
        *response_code = 500;
        strcpy(response_buffer, "{\"error\":\"storage error\"}");
        etcd_response_free(&get_resp);
        return -1;
    }
    
    json_object* patch_obj = json_tokener_parse(body);
    if (!patch_obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        if (current_obj) json_object_put(current_obj);
        etcd_response_free(&get_resp);
        return -1;
    }
    
    int is_kubectl_apply = (content_type && 
        (strstr(content_type, "apply-patch") || strstr(content_type, "strategic-merge-patch")));
    
    json_object* merged_obj = NULL;
    int http_status = 200;
    
    if (is_kubectl_apply && current_obj) {
        kubectl_apply_result_t* apply_result = kubectl_apply_three_way_merge(
            current_obj, patch_obj, namespace, name);
        
        if (!apply_result->success) {
            *response_code = apply_result->response_code;
            strncpy(response_buffer, apply_result->error_message, 16384 - 1);
            kubectl_apply_result_free(apply_result);
            json_object_put(current_obj);
            json_object_put(patch_obj);
            etcd_response_free(&get_resp);
            return -1;
        }
        
        merged_obj = apply_result->result_obj;
        http_status = apply_result->response_code;
        free(apply_result);
    } else if (is_kubectl_apply && !current_obj) {
        merged_obj = NULL;
        json_object_deep_copy(patch_obj, &merged_obj, NULL);
        if (!merged_obj) merged_obj = patch_obj;
        const char* patch_json = json_object_to_json_string(patch_obj);
        kubectl_apply_set_last_applied(merged_obj, patch_json);
        http_status = 201;
    } else {
        if (!current_obj) {
            *response_code = 404;
            strcpy(response_buffer, "{\"error\":\"service not found\"}");
            json_object_put(patch_obj);
            etcd_response_free(&get_resp);
            return -1;
        }
        
        merged_obj = current_obj;
        json_object_object_foreach(patch_obj, key, val) {
            json_object_object_add(merged_obj, key, json_object_get(val));
        }
    }
    
    const char* merged_json = json_object_to_json_string(merged_obj);
    
    etcd_response_t patch_resp = {0};
    int patch_status = etcd_manager_patch(
        etcd_key,
        merged_json,
        get_resp.revision,
        &patch_resp
    );
    
    if (patch_status == ETCD_OK) {
        json_object* metadata = json_object_object_get(merged_obj, "metadata");
        if (metadata) {
            char rv_str[32];
            snprintf(rv_str, sizeof(rv_str), "%lu", patch_resp.revision);
            json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
        }
        
        const char* response_json = json_object_to_json_string(merged_obj);
        strncpy(response_buffer, response_json, 16384 - 1);
        *response_code = http_status;
    } else if (patch_status == ETCD_CAS_FAILED) {
        *response_code = 409;
        strcpy(response_buffer, "{\"error\":\"Conflict\",\"message\":\"service was modified\"}");
    } else {
        *response_code = 500;
        strcpy(response_buffer, "{\"error\":\"internal error\"}");
    }
    
    if (merged_obj && merged_obj != current_obj) {
        json_object_put(merged_obj);
    }
    if (current_obj) json_object_put(current_obj);
    json_object_put(patch_obj);
    etcd_response_free(&get_resp);
    etcd_response_free(&patch_resp);
    
    return (patch_status == ETCD_OK) ? 0 : -1;
}

/**
 * DELETE /api/v1/namespaces/{namespace}/services/{name}
 */
int endpoint_delete_service_etcd(const char* namespace, const char* name,
                                 char* response_buffer, int* response_code) {
    char etcd_key[256];
    build_service_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t del_resp = {0};
    int status = etcd_manager_delete(etcd_key, &del_resp);
    
    if (status == ETCD_OK) {
        *response_code = 204;  // No Content
        response_buffer[0] = '\0';
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"failed to delete service\"}");
        *response_code = 500;
    }
    
    etcd_response_free(&del_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

// ============================================================================
// DEPLOYMENT ETCD-BACKED ENDPOINTS (Phase 2C Integration)
// ============================================================================

static void build_deployment_etcd_key(const char* namespace, const char* deployment_name,
                                      char* key, int key_len) {
    snprintf(key, key_len, "/sirah/deployments/%s/%s",
             namespace ? namespace : "default",
             deployment_name ? deployment_name : "unknown");
}

/**
 * POST /apis/apps/v1/namespaces/{namespace}/deployments - Create Deployment
 */
int endpoint_create_deployment_etcd(const char* namespace, const char* body,
                                    char* response_buffer, int* response_code) {
    if (!body || strlen(body) == 0) {
        snprintf(response_buffer, 16384, "{\"error\":\"empty body\"}");
        *response_code = 400;
        return -1;
    }
    
    json_object* req = json_tokener_parse(body);
    if (!req) {
        snprintf(response_buffer, 16384, "{\"error\":\"invalid JSON\"}");
        *response_code = 400;
        return -1;
    }
    
    json_object* metadata = json_object_object_get(req, "metadata");
    if (!metadata) {
        snprintf(response_buffer, 16384, "{\"error\":\"metadata required\"}");
        *response_code = 400;
        json_object_put(req);
        return -1;
    }
    
    const char* deploy_name = json_object_get_string(
        json_object_object_get(metadata, "name"));
    if (!deploy_name) {
        snprintf(response_buffer, 16384, "{\"error\":\"deployment name required\"}");
        *response_code = 400;
        json_object_put(req);
        return -1;
    }
    
    // Add versioning metadata
    char rv_str[32];
    snprintf(rv_str, sizeof(rv_str), "%lu", (unsigned long)time(NULL) * 1000);
    json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
    json_object_object_add(metadata, "generation", json_object_new_int64(1));
    
    const char* json_str = json_object_to_json_string(req);
    
    char etcd_key[256];
    build_deployment_etcd_key(namespace, deploy_name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t put_resp = {0};
    int status = etcd_manager_put(etcd_key, json_str, &put_resp);
    
    if (status == ETCD_OK) {
        snprintf(rv_str, sizeof(rv_str), "%lu", put_resp.revision);
        json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
        
        // Ensure apiVersion and kind are present
        if (!json_object_object_get(req, "apiVersion")) {
            json_object_object_add(req, "apiVersion", json_object_new_string("apps/v1"));
        }
        if (!json_object_object_get(req, "kind")) {
            json_object_object_add(req, "kind", json_object_new_string("Deployment"));
        }
        
        const char* response_json = json_object_to_json_string(req);
        strncpy(response_buffer, response_json, 16384 - 1);
        *response_code = 201;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"failed to store deployment\"}");
        *response_code = 500;
    }
    
    json_object_put(req);
    etcd_response_free(&put_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

/**
 * GET /apis/apps/v1/namespaces/{namespace}/deployments/{name}
 */
int endpoint_get_deployment_etcd(const char* namespace, const char* name,
                                 char* response_buffer, int* response_code) {
    char etcd_key[256];
    build_deployment_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t get_resp = {0};
    int status = etcd_manager_get(etcd_key, &get_resp);
    
    if (status == ETCD_OK && get_resp.value) {
        strncpy(response_buffer, get_resp.value, 16384 - 1);
        *response_code = 200;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"deployment not found\"}");
        *response_code = 404;
    }
    
    etcd_response_free(&get_resp);
    return (status == ETCD_OK && get_resp.value) ? 0 : -1;
}

/**
 * GET /apis/apps/v1/namespaces/{namespace}/deployments
 */
int endpoint_list_deployments_etcd(const char* namespace, char* response_buffer,
                                   int* response_code) {
    char etcd_prefix[256];
    snprintf(etcd_prefix, sizeof(etcd_prefix), "/sirah/deployments/%s/",
             namespace ? namespace : "default");
    
    etcd_response_t list_resp = {0};
    int status = etcd_manager_list(etcd_prefix, &list_resp);
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("apps/v1"));
    json_object_object_add(root, "kind", json_object_new_string("DeploymentList"));
    json_object* items = json_object_new_array();
    
    if (status == ETCD_OK && list_resp.value) {
        char* copy = strdup(list_resp.value);
        char* deploy_json = strtok(copy, "\n");
        while (deploy_json != NULL) {
            if (strlen(deploy_json) > 0) {
                json_object* deploy_obj = json_tokener_parse(deploy_json);
                if (deploy_obj) {
                    json_object_array_add(items, deploy_obj);
                }
            }
            deploy_json = strtok(NULL, "\n");
        }
        free(copy);
    }
    
    json_object_object_add(root, "items", items);
    const char* json_str = json_object_to_json_string(root);
    strncpy(response_buffer, json_str, 16384 - 1);
    *response_code = 200;
    
    json_object_put(root);
    etcd_response_free(&list_resp);
    return 0;
}

/**
 * PATCH /apis/apps/v1/namespaces/{namespace}/deployments/{name}
 */
int endpoint_patch_deployment_etcd(const char* namespace, const char* name, const char* body,
                                   const char* content_type, char* response_buffer,
                                   int* response_code) {
    
    if (!body) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"empty body\"}");
        return -1;
    }
    
    char etcd_key[256];
    build_deployment_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    // Get current version
    etcd_response_t get_resp = {0};
    int get_status = etcd_manager_get(etcd_key, &get_resp);
    
    json_object* current_obj = NULL;
    if (get_status == ETCD_OK) {
        current_obj = json_tokener_parse(get_resp.value);
        if (!current_obj) {
            *response_code = 500;
            strcpy(response_buffer, "{\"error\":\"invalid deployment data\"}");
            etcd_response_free(&get_resp);
            return -1;
        }
    } else if (get_status != ETCD_NOT_FOUND) {
        *response_code = 500;
        strcpy(response_buffer, "{\"error\":\"storage error\"}");
        etcd_response_free(&get_resp);
        return -1;
    }
    
    json_object* patch_obj = json_tokener_parse(body);
    if (!patch_obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        if (current_obj) json_object_put(current_obj);
        etcd_response_free(&get_resp);
        return -1;
    }
    
    int is_kubectl_apply = (content_type && 
        (strstr(content_type, "apply-patch") || strstr(content_type, "strategic-merge-patch")));
    
    json_object* merged_obj = NULL;
    int http_status = 200;
    
    if (is_kubectl_apply && current_obj) {
        kubectl_apply_result_t* apply_result = kubectl_apply_three_way_merge(
            current_obj, patch_obj, namespace, name);
        
        if (!apply_result->success) {
            *response_code = apply_result->response_code;
            strncpy(response_buffer, apply_result->error_message, 16384 - 1);
            kubectl_apply_result_free(apply_result);
            json_object_put(current_obj);
            json_object_put(patch_obj);
            etcd_response_free(&get_resp);
            return -1;
        }
        
        merged_obj = apply_result->result_obj;
        http_status = apply_result->response_code;
        free(apply_result);
    } else if (is_kubectl_apply && !current_obj) {
        merged_obj = NULL;
        json_object_deep_copy(patch_obj, &merged_obj, NULL);
        if (!merged_obj) merged_obj = patch_obj;
        const char* patch_json = json_object_to_json_string(patch_obj);
        kubectl_apply_set_last_applied(merged_obj, patch_json);
        http_status = 201;
    } else {
        if (!current_obj) {
            *response_code = 404;
            strcpy(response_buffer, "{\"error\":\"deployment not found\"}");
            json_object_put(patch_obj);
            etcd_response_free(&get_resp);
            return -1;
        }
        
        merged_obj = current_obj;
        
        // Check CAS - resourceVersion validation
        json_object* patch_meta = json_object_object_get(patch_obj, "metadata");
        if (patch_meta) {
            json_object* patch_rv_obj = json_object_object_get(patch_meta, "resourceVersion");
            if (patch_rv_obj) {
                const char* patch_rv_str = json_object_get_string(patch_rv_obj);
                json_object* current_meta = json_object_object_get(current_obj, "metadata");
                json_object* current_rv_obj = json_object_object_get(current_meta, "resourceVersion");
                const char* current_rv_str = json_object_get_string(current_rv_obj);
                
                if (!patch_rv_str || !current_rv_str || strcmp(patch_rv_str, current_rv_str) != 0) {
                    *response_code = 409;
                    strcpy(response_buffer, "{\"error\":\"Conflict\",\"message\":\"resourceVersion mismatch\"}");
                    json_object_put(current_obj);
                    json_object_put(patch_obj);
                    etcd_response_free(&get_resp);
                    return -1;
                }
            }
        }
        
        // Merge patch
        if (patch_meta) {
            json_object* current_meta = json_object_object_get(current_obj, "metadata");
            json_object* patch_gen = json_object_object_get(patch_meta, "generation");
            if (patch_gen) {
                json_object_object_add(current_meta, "generation", patch_gen);
            }
        }
        json_object* patch_spec = json_object_object_get(patch_obj, "spec");
        if (patch_spec) {
            // Spec change increments generation
            json_object* current_meta = json_object_object_get(current_obj, "metadata");
            json_object* gen_obj = json_object_object_get(current_meta, "generation");
            int64_t current_gen = json_object_get_int64(gen_obj);
            json_object_object_add(current_meta, "generation", json_object_new_int64(current_gen + 1));
            
            json_object_object_add(current_obj, "spec", patch_spec);
        }
    }
    
    const char* merged_json = json_object_to_json_string(merged_obj);
    
    // Store in etcd with CAS (patch with version checking)
    etcd_response_t patch_resp = {0};
    int patch_status = etcd_manager_patch(etcd_key, merged_json, get_resp.revision, &patch_resp);
    
    if (patch_status == ETCD_OK) {
        json_object* metadata = json_object_object_get(merged_obj, "metadata");
        if (metadata) {
            char rv_str[32];
            snprintf(rv_str, sizeof(rv_str), "%lu", patch_resp.revision);
            json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
        }
        
        const char* response_json = json_object_to_json_string(merged_obj);
        strncpy(response_buffer, response_json, 16384 - 1);
        *response_code = http_status;
    } else if (patch_status == ETCD_CAS_FAILED) {
        *response_code = 409;
        strcpy(response_buffer, "{\"error\":\"Conflict\",\"message\":\"deployment was modified\"}");
    } else {
        *response_code = 500;
        strcpy(response_buffer, "{\"error\":\"internal error\"}");
    }
    
    if (merged_obj && merged_obj != current_obj) {
        json_object_put(merged_obj);
    }
    if (current_obj) json_object_put(current_obj);
    json_object_put(patch_obj);
    etcd_response_free(&get_resp);
    etcd_response_free(&patch_resp);
    
    return (patch_status == ETCD_OK) ? 0 : -1;
}

/**
 * DELETE /apis/apps/v1/namespaces/{namespace}/deployments/{name}
 */
int endpoint_delete_deployment_etcd(const char* namespace, const char* name,
                                    char* response_buffer, int* response_code) {
    char etcd_key[256];
    build_deployment_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t del_resp = {0};
    int status = etcd_manager_delete(etcd_key, &del_resp);
    
    if (status == ETCD_OK) {
        *response_code = 204;
        response_buffer[0] = '\0';
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"failed to delete deployment\"}");
        *response_code = 500;
    }
    
    etcd_response_free(&del_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

// ============================================================================
// STATEFULSET ETCD-BACKED ENDPOINTS (Phase 5)
// ============================================================================

static void build_statefulset_etcd_key(const char* namespace, const char* name,
                                       char* key, int key_len) {
    snprintf(key, key_len, "/sirah/statefulsets/%s/%s",
             namespace ? namespace : "default",
             name ? name : "unknown");
}

int endpoint_create_statefulset_etcd(const char* namespace, const char* body,
                                     char* response_buffer, int* response_code) {
    if (!body || strlen(body) == 0) {
        snprintf(response_buffer, 16384, "{\"error\":\"empty body\"}");
        *response_code = 400;
        return -1;
    }
    
    json_object* req = json_tokener_parse(body);
    if (!req) {
        snprintf(response_buffer, 16384, "{\"error\":\"invalid JSON\"}");
        *response_code = 400;
        return -1;
    }
    
    json_object* metadata = json_object_object_get(req, "metadata");
    if (!metadata) {
        snprintf(response_buffer, 16384, "{\"error\":\"metadata required\"}");
        *response_code = 400;
        json_object_put(req);
        return -1;
    }
    
    const char* name = json_object_get_string(json_object_object_get(metadata, "name"));
    if (!name) {
        snprintf(response_buffer, 16384, "{\"error\":\"statefulset name required\"}");
        *response_code = 400;
        json_object_put(req);
        return -1;
    }
    
    char etcd_key[256];
    build_statefulset_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    const char* json_str = json_object_to_json_string(req);
    etcd_response_t put_resp = {0};
    int status = etcd_manager_put(etcd_key, json_str, &put_resp);
    
    if (status == ETCD_OK) {
        char rv_str[32];
        snprintf(rv_str, sizeof(rv_str), "%lu", put_resp.revision);
        json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
        
        const char* response_json = json_object_to_json_string(req);
        strncpy(response_buffer, response_json, 16384 - 1);
        *response_code = 201;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"failed to store statefulset\"}");
        *response_code = 500;
    }
    
    json_object_put(req);
    etcd_response_free(&put_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

int endpoint_get_statefulset_etcd(const char* namespace, const char* name,
                                  char* response_buffer, int* response_code) {
    char etcd_key[256];
    build_statefulset_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t get_resp = {0};
    int status = etcd_manager_get(etcd_key, &get_resp);
    
    if (status == ETCD_OK && get_resp.value) {
        strncpy(response_buffer, get_resp.value, 16384 - 1);
        *response_code = 200;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"statefulset not found\"}");
        *response_code = 404;
    }
    
    etcd_response_free(&get_resp);
    return (status == ETCD_OK && get_resp.value) ? 0 : -1;
}

int endpoint_list_statefulsets_etcd(const char* namespace, char* response_buffer,
                                    int* response_code) {
    char etcd_prefix[256];
    snprintf(etcd_prefix, sizeof(etcd_prefix), "/sirah/statefulsets/%s/",
             namespace ? namespace : "default");
    
    etcd_response_t list_resp = {0};
    int status = etcd_manager_list(etcd_prefix, &list_resp);
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("apps/v1"));
    json_object_object_add(root, "kind", json_object_new_string("StatefulSetList"));
    json_object* items = json_object_new_array();
    
    if (status == ETCD_OK && list_resp.value) {
        char* copy = strdup(list_resp.value);
        char* sts_json = strtok(copy, "\n");
        while (sts_json != NULL) {
            if (strlen(sts_json) > 0) {
                json_object* sts_obj = json_tokener_parse(sts_json);
                if (sts_obj) {
                    json_object_array_add(items, sts_obj);
                }
            }
            sts_json = strtok(NULL, "\n");
        }
        free(copy);
    }
    
    json_object_object_add(root, "items", items);
    const char* json_str = json_object_to_json_string(root);
    strncpy(response_buffer, json_str, 16384 - 1);
    *response_code = 200;
    
    json_object_put(root);
    etcd_response_free(&list_resp);
    return 0;
}

int endpoint_delete_statefulset_etcd(const char* namespace, const char* name,
                                     char* response_buffer, int* response_code) {
    char etcd_key[256];
    build_statefulset_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t del_resp = {0};
    int status = etcd_manager_delete(etcd_key, &del_resp);
    
    if (status == ETCD_OK) {
        *response_code = 204;
        response_buffer[0] = '\0';
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"failed to delete statefulset\"}");
        *response_code = 500;
    }
    
    etcd_response_free(&del_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

// ============================================================================
// JOB ETCD-BACKED ENDPOINTS (Phase 5)
// ============================================================================

static void build_job_etcd_key(const char* namespace, const char* name,
                               char* key, int key_len) {
    snprintf(key, key_len, "/sirah/jobs/%s/%s",
             namespace ? namespace : "default",
             name ? name : "unknown");
}

int endpoint_create_job_etcd(const char* namespace, const char* body,
                             char* response_buffer, int* response_code) {
    if (!body || strlen(body) == 0) {
        snprintf(response_buffer, 16384, "{\"error\":\"empty body\"}");
        *response_code = 400;
        return -1;
    }
    
    json_object* req = json_tokener_parse(body);
    if (!req) {
        snprintf(response_buffer, 16384, "{\"error\":\"invalid JSON\"}");
        *response_code = 400;
        return -1;
    }
    
    json_object* metadata = json_object_object_get(req, "metadata");
    if (!metadata) {
        snprintf(response_buffer, 16384, "{\"error\":\"metadata required\"}");
        *response_code = 400;
        json_object_put(req);
        return -1;
    }
    
    const char* name = json_object_get_string(json_object_object_get(metadata, "name"));
    if (!name) {
        snprintf(response_buffer, 16384, "{\"error\":\"job name required\"}");
        *response_code = 400;
        json_object_put(req);
        return -1;
    }
    
    char etcd_key[256];
    build_job_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    const char* json_str = json_object_to_json_string(req);
    etcd_response_t put_resp = {0};
    int status = etcd_manager_put(etcd_key, json_str, &put_resp);
    
    if (status == ETCD_OK) {
        char rv_str[32];
        snprintf(rv_str, sizeof(rv_str), "%lu", put_resp.revision);
        json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
        
        const char* response_json = json_object_to_json_string(req);
        strncpy(response_buffer, response_json, 16384 - 1);
        *response_code = 201;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"failed to store job\"}");
        *response_code = 500;
    }
    
    json_object_put(req);
    etcd_response_free(&put_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

int endpoint_get_job_etcd(const char* namespace, const char* name,
                          char* response_buffer, int* response_code) {
    char etcd_key[256];
    build_job_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t get_resp = {0};
    int status = etcd_manager_get(etcd_key, &get_resp);
    
    if (status == ETCD_OK && get_resp.value) {
        strncpy(response_buffer, get_resp.value, 16384 - 1);
        *response_code = 200;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"job not found\"}");
        *response_code = 404;
    }
    
    etcd_response_free(&get_resp);
    return (status == ETCD_OK && get_resp.value) ? 0 : -1;
}

int endpoint_list_jobs_etcd(const char* namespace, char* response_buffer,
                            int* response_code) {
    char etcd_prefix[256];
    snprintf(etcd_prefix, sizeof(etcd_prefix), "/sirah/jobs/%s/",
             namespace ? namespace : "default");
    
    etcd_response_t list_resp = {0};
    int status = etcd_manager_list(etcd_prefix, &list_resp);
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("batch/v1"));
    json_object_object_add(root, "kind", json_object_new_string("JobList"));
    json_object* items = json_object_new_array();
    
    if (status == ETCD_OK && list_resp.value) {
        char* copy = strdup(list_resp.value);
        char* job_json = strtok(copy, "\n");
        while (job_json != NULL) {
            if (strlen(job_json) > 0) {
                json_object* job_obj = json_tokener_parse(job_json);
                if (job_obj) {
                    json_object_array_add(items, job_obj);
                }
            }
            job_json = strtok(NULL, "\n");
        }
        free(copy);
    }
    
    json_object_object_add(root, "items", items);
    const char* json_str = json_object_to_json_string(root);
    strncpy(response_buffer, json_str, 16384 - 1);
    *response_code = 200;
    
    json_object_put(root);
    etcd_response_free(&list_resp);
    return 0;
}

int endpoint_delete_job_etcd(const char* namespace, const char* name,
                             char* response_buffer, int* response_code) {
    char etcd_key[256];
    build_job_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t del_resp = {0};
    int status = etcd_manager_delete(etcd_key, &del_resp);
    
    if (status == ETCD_OK) {
        *response_code = 204;
        response_buffer[0] = '\0';
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"failed to delete job\"}");
        *response_code = 500;
    }
    
    etcd_response_free(&del_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

// ============================================================================
// CONFIGMAP ETCD-BACKED ENDPOINTS (Phase 6A)
// ============================================================================

static void build_configmap_etcd_key(const char* namespace, const char* name,
                                      char* key, int key_len) {
    snprintf(key, key_len, "/sirah/configmaps/%s/%s",
             namespace ? namespace : "default",
             name ? name : "unknown");
}

int endpoint_create_configmap_etcd(const char* namespace, const char* body,
                                   char* response_buffer, int* response_code) {
    if (!body || strlen(body) == 0) {
        snprintf(response_buffer, 16384, "{\"error\":\"empty body\"}");
        *response_code = 400;
        return -1;
    }

    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        snprintf(response_buffer, 16384, "{\"error\":\"invalid JSON\"}");
        *response_code = 400;
        return -1;
    }

    json_object* metadata = json_object_object_get(obj, "metadata");
    if (!metadata) {
        snprintf(response_buffer, 16384, "{\"error\":\"metadata required\"}");
        *response_code = 400;
        json_object_put(obj);
        return -1;
    }

    const char* name = json_object_get_string(json_object_object_get(metadata, "name"));
    if (!name) {
        snprintf(response_buffer, 16384, "{\"error\":\"name required in metadata\"}");
        *response_code = 400;
        json_object_put(obj);
        return -1;
    }

    char etcd_key[512];
    build_configmap_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));

    const char* json_str = json_object_to_json_string(obj);
    etcd_response_t put_resp = {0};
    int status = etcd_manager_put(etcd_key, json_str, &put_resp);

    if (status == ETCD_OK) {
        char rv_str[32];
        snprintf(rv_str, sizeof(rv_str), "%lu", put_resp.revision);
        json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));

        const char* response_json = json_object_to_json_string(obj);
        strncpy(response_buffer, response_json, 16384 - 1);
        *response_code = 201;
    } else if (status == ETCD_UNAVAILABLE) {
        snprintf(response_buffer, 16384, "{\"error\":\"etcd unavailable\"}");
        *response_code = 503;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"storage error\"}");
        *response_code = 500;
    }

    json_object_put(obj);
    etcd_response_free(&put_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

int endpoint_get_configmap_etcd(const char* namespace, const char* name,
                                char* response_buffer, int* response_code) {
    char etcd_key[512];
    build_configmap_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));

    etcd_response_t get_resp = {0};
    int status = etcd_manager_get(etcd_key, &get_resp);

    if (status == ETCD_OK) {
        json_object* obj = json_tokener_parse(get_resp.value);
        if (obj) {
            json_object* meta = json_object_object_get(obj, "metadata");
            if (meta) {
                char rv_str[32];
                snprintf(rv_str, sizeof(rv_str), "%lu", get_resp.revision);
                json_object_object_add(meta, "resourceVersion", json_object_new_string(rv_str));
            }
            const char* json_str = json_object_to_json_string(obj);
            strncpy(response_buffer, json_str, 16384 - 1);
            json_object_put(obj);
        }
        *response_code = 200;
    } else if (status == ETCD_NOT_FOUND) {
        snprintf(response_buffer, 16384, "{\"error\":\"ConfigMap not found\"}");
        *response_code = 404;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"storage error\"}");
        *response_code = 503;
    }

    etcd_response_free(&get_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

int endpoint_list_configmaps_etcd(const char* namespace, char* response_buffer, int* response_code) {
    char etcd_prefix[512];
    snprintf(etcd_prefix, sizeof(etcd_prefix), "/sirah/configmaps/%s/",
             namespace ? namespace : "default");

    etcd_response_t list_resp = {0};
    int status = etcd_manager_list(etcd_prefix, &list_resp);

    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("ConfigMapList"));
    json_object* items = json_object_new_array();

    if (status == ETCD_OK && list_resp.kvs_count > 0) {
        for (int i = 0; i < list_resp.kvs_count; i++) {
            json_object* obj = json_tokener_parse(list_resp.kvs_values[i]);
            if (obj) {
                json_object* meta = json_object_object_get(obj, "metadata");
                if (meta) {
                    char rv_str[32];
                    snprintf(rv_str, sizeof(rv_str), "%lu", list_resp.kvs_versions[i]);
                    json_object_object_add(meta, "resourceVersion", json_object_new_string(rv_str));
                }
                json_object_array_add(items, obj);
            }
        }
    }

    json_object_object_add(root, "items", items);
    const char* json_str = json_object_to_json_string(root);
    strncpy(response_buffer, json_str, 16384 - 1);
    *response_code = 200;

    json_object_put(root);
    etcd_response_free(&list_resp);
    return 0;
}

int endpoint_patch_configmap_etcd(const char* namespace, const char* name, const char* body,
                                  const char* content_type, char* response_buffer, int* response_code) {
    if (!body || strlen(body) == 0) {
        snprintf(response_buffer, 16384, "{\"error\":\"empty body\"}");
        *response_code = 400;
        return -1;
    }

    char etcd_key[512];
    build_configmap_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));

    // Get current version
    etcd_response_t get_resp = {0};
    int get_status = etcd_manager_get(etcd_key, &get_resp);

    json_object* current_obj = NULL;
    if (get_status == ETCD_OK) {
        current_obj = json_tokener_parse(get_resp.value);
        if (!current_obj) {
            snprintf(response_buffer, 16384, "{\"error\":\"invalid configmap data\"}");
            *response_code = 500;
            etcd_response_free(&get_resp);
            return -1;
        }
    } else if (get_status != ETCD_NOT_FOUND) {
        snprintf(response_buffer, 16384, "{\"error\":\"storage error\"}");
        *response_code = 500;
        etcd_response_free(&get_resp);
        return -1;
    }

    json_object* patch_obj = json_tokener_parse(body);
    if (!patch_obj) {
        snprintf(response_buffer, 16384, "{\"error\":\"invalid JSON\"}");
        *response_code = 400;
        if (current_obj) json_object_put(current_obj);
        etcd_response_free(&get_resp);
        return -1;
    }

    int is_kubectl_apply = (content_type && 
        (strstr(content_type, "apply-patch") || strstr(content_type, "strategic-merge-patch")));

    json_object* merged_obj = NULL;
    int http_status = 200;

    if (is_kubectl_apply && current_obj) {
        kubectl_apply_result_t* apply_result = kubectl_apply_three_way_merge(
            current_obj, patch_obj, namespace, name);

        if (!apply_result->success) {
            snprintf(response_buffer, 16384, "%s", apply_result->error_message);
            *response_code = apply_result->response_code;
            kubectl_apply_result_free(apply_result);
            json_object_put(current_obj);
            json_object_put(patch_obj);
            etcd_response_free(&get_resp);
            return -1;
        }

        merged_obj = apply_result->result_obj;
        http_status = apply_result->response_code;
        free(apply_result);
    } else if (is_kubectl_apply && !current_obj) {
        merged_obj = NULL;
        json_object_deep_copy(patch_obj, &merged_obj, NULL);
        if (!merged_obj) merged_obj = patch_obj;
        const char* patch_json = json_object_to_json_string(patch_obj);
        kubectl_apply_set_last_applied(merged_obj, patch_json);
        http_status = 201;
    } else {
        if (!current_obj) {
            snprintf(response_buffer, 16384, "{\"error\":\"ConfigMap not found\"}");
            *response_code = 404;
            json_object_put(patch_obj);
            etcd_response_free(&get_resp);
            return -1;
        }

        merged_obj = current_obj;
        
        // Simple merge
        json_object_object_foreach(patch_obj, key, val) {
            json_object_object_add(merged_obj, key, json_object_get(val));
        }
    }

    const char* merged_json = json_object_to_json_string(merged_obj);
    
    etcd_response_t patch_resp = {0};
    int patch_status = etcd_manager_patch(etcd_key, merged_json, get_resp.revision, &patch_resp);

    if (patch_status == ETCD_OK) {
        json_object* metadata = json_object_object_get(merged_obj, "metadata");
        if (metadata) {
            char rv_str[32];
            snprintf(rv_str, sizeof(rv_str), "%lu", patch_resp.revision);
            json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
        }

        const char* response_json = json_object_to_json_string(merged_obj);
        strncpy(response_buffer, response_json, 16384 - 1);
        *response_code = http_status;
    } else if (patch_status == ETCD_CAS_FAILED) {
        snprintf(response_buffer, 16384, "{\"error\":\"Conflict\",\"message\":\"ConfigMap was modified\"}");
        *response_code = 409;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"storage error\"}");
        *response_code = 500;
    }

    if (merged_obj && merged_obj != current_obj) {
        json_object_put(merged_obj);
    }
    if (current_obj) json_object_put(current_obj);
    json_object_put(patch_obj);
    etcd_response_free(&get_resp);
    etcd_response_free(&patch_resp);

    return (patch_status == ETCD_OK) ? 0 : -1;
}

int endpoint_delete_configmap_etcd(const char* namespace, const char* name,
                                   char* response_buffer, int* response_code) {
    char etcd_key[512];
    build_configmap_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));

    etcd_response_t del_resp = {0};
    int status = etcd_manager_delete(etcd_key, &del_resp);

    if (status == ETCD_OK) {
        *response_code = 204;
        response_buffer[0] = '\0';
    } else if (status == ETCD_NOT_FOUND) {
        snprintf(response_buffer, 16384, "{\"error\":\"ConfigMap not found\"}");
        *response_code = 404;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"storage error\"}");
        *response_code = 500;
    }

    etcd_response_free(&del_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

// ============================================================================
// SECRET ETCD-BACKED ENDPOINTS (Phase 6A)
// ============================================================================

static void build_secret_etcd_key(const char* namespace, const char* name,
                                  char* key, int key_len) {
    snprintf(key, key_len, "/sirah/secrets/%s/%s",
             namespace ? namespace : "default",
             name ? name : "unknown");
}

int endpoint_create_secret_etcd(const char* namespace, const char* body,
                                char* response_buffer, int* response_code) {
    if (!body || strlen(body) == 0) {
        snprintf(response_buffer, 16384, "{\"error\":\"empty body\"}");
        *response_code = 400;
        return -1;
    }

    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        snprintf(response_buffer, 16384, "{\"error\":\"invalid JSON\"}");
        *response_code = 400;
        return -1;
    }

    json_object* metadata = json_object_object_get(obj, "metadata");
    if (!metadata) {
        snprintf(response_buffer, 16384, "{\"error\":\"metadata required\"}");
        *response_code = 400;
        json_object_put(obj);
        return -1;
    }

    const char* name = json_object_get_string(json_object_object_get(metadata, "name"));
    if (!name) {
        snprintf(response_buffer, 16384, "{\"error\":\"name required in metadata\"}");
        *response_code = 400;
        json_object_put(obj);
        return -1;
    }

    char etcd_key[512];
    build_secret_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));

    const char* json_str = json_object_to_json_string(obj);
    etcd_response_t put_resp = {0};
    int status = etcd_manager_put(etcd_key, json_str, &put_resp);

    if (status == ETCD_OK) {
        char rv_str[32];
        snprintf(rv_str, sizeof(rv_str), "%lu", put_resp.revision);
        json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));

        const char* response_json = json_object_to_json_string(obj);
        strncpy(response_buffer, response_json, 16384 - 1);
        *response_code = 201;
    } else if (status == ETCD_UNAVAILABLE) {
        snprintf(response_buffer, 16384, "{\"error\":\"etcd unavailable\"}");
        *response_code = 503;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"storage error\"}");
        *response_code = 500;
    }

    json_object_put(obj);
    etcd_response_free(&put_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

int endpoint_get_secret_etcd(const char* namespace, const char* name,
                             char* response_buffer, int* response_code) {
    char etcd_key[512];
    build_secret_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));

    etcd_response_t get_resp = {0};
    int status = etcd_manager_get(etcd_key, &get_resp);

    if (status == ETCD_OK) {
        json_object* obj = json_tokener_parse(get_resp.value);
        if (obj) {
            json_object* meta = json_object_object_get(obj, "metadata");
            if (meta) {
                char rv_str[32];
                snprintf(rv_str, sizeof(rv_str), "%lu", get_resp.revision);
                json_object_object_add(meta, "resourceVersion", json_object_new_string(rv_str));
            }
            const char* json_str = json_object_to_json_string(obj);
            strncpy(response_buffer, json_str, 16384 - 1);
            json_object_put(obj);
        }
        *response_code = 200;
    } else if (status == ETCD_NOT_FOUND) {
        snprintf(response_buffer, 16384, "{\"error\":\"Secret not found\"}");
        *response_code = 404;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"storage error\"}");
        *response_code = 503;
    }

    etcd_response_free(&get_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

int endpoint_list_secrets_etcd(const char* namespace, char* response_buffer, int* response_code) {
    char etcd_prefix[512];
    snprintf(etcd_prefix, sizeof(etcd_prefix), "/sirah/secrets/%s/",
             namespace ? namespace : "default");

    etcd_response_t list_resp = {0};
    int status = etcd_manager_list(etcd_prefix, &list_resp);

    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("SecretList"));
    json_object* items = json_object_new_array();

    if (status == ETCD_OK && list_resp.kvs_count > 0) {
        for (int i = 0; i < list_resp.kvs_count; i++) {
            json_object* obj = json_tokener_parse(list_resp.kvs_values[i]);
            if (obj) {
                json_object* meta = json_object_object_get(obj, "metadata");
                if (meta) {
                    char rv_str[32];
                    snprintf(rv_str, sizeof(rv_str), "%lu", list_resp.kvs_versions[i]);
                    json_object_object_add(meta, "resourceVersion", json_object_new_string(rv_str));
                }
                json_object_array_add(items, obj);
            }
        }
    }

    json_object_object_add(root, "items", items);
    const char* json_str = json_object_to_json_string(root);
    strncpy(response_buffer, json_str, 16384 - 1);
    *response_code = 200;

    json_object_put(root);
    etcd_response_free(&list_resp);
    return 0;
}

int endpoint_patch_secret_etcd(const char* namespace, const char* name, const char* body,
                               const char* content_type, char* response_buffer, int* response_code) {
    if (!body || strlen(body) == 0) {
        snprintf(response_buffer, 16384, "{\"error\":\"empty body\"}");
        *response_code = 400;
        return -1;
    }

    char etcd_key[512];
    build_secret_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));

    // Get current version
    etcd_response_t get_resp = {0};
    int get_status = etcd_manager_get(etcd_key, &get_resp);

    json_object* current_obj = NULL;
    if (get_status == ETCD_OK) {
        current_obj = json_tokener_parse(get_resp.value);
        if (!current_obj) {
            snprintf(response_buffer, 16384, "{\"error\":\"invalid secret data\"}");
            *response_code = 500;
            etcd_response_free(&get_resp);
            return -1;
        }
    } else if (get_status != ETCD_NOT_FOUND) {
        snprintf(response_buffer, 16384, "{\"error\":\"storage error\"}");
        *response_code = 500;
        etcd_response_free(&get_resp);
        return -1;
    }

    json_object* patch_obj = json_tokener_parse(body);
    if (!patch_obj) {
        snprintf(response_buffer, 16384, "{\"error\":\"invalid JSON\"}");
        *response_code = 400;
        if (current_obj) json_object_put(current_obj);
        etcd_response_free(&get_resp);
        return -1;
    }

    int is_kubectl_apply = (content_type && 
        (strstr(content_type, "apply-patch") || strstr(content_type, "strategic-merge-patch")));

    json_object* merged_obj = NULL;
    int http_status = 200;

    if (is_kubectl_apply && current_obj) {
        kubectl_apply_result_t* apply_result = kubectl_apply_three_way_merge(
            current_obj, patch_obj, namespace, name);

        if (!apply_result->success) {
            snprintf(response_buffer, 16384, "%s", apply_result->error_message);
            *response_code = apply_result->response_code;
            kubectl_apply_result_free(apply_result);
            json_object_put(current_obj);
            json_object_put(patch_obj);
            etcd_response_free(&get_resp);
            return -1;
        }

        merged_obj = apply_result->result_obj;
        http_status = apply_result->response_code;
        free(apply_result);
    } else if (is_kubectl_apply && !current_obj) {
        merged_obj = NULL;
        json_object_deep_copy(patch_obj, &merged_obj, NULL);
        if (!merged_obj) merged_obj = patch_obj;
        const char* patch_json = json_object_to_json_string(patch_obj);
        kubectl_apply_set_last_applied(merged_obj, patch_json);
        http_status = 201;
    } else {
        if (!current_obj) {
            snprintf(response_buffer, 16384, "{\"error\":\"Secret not found\"}");
            *response_code = 404;
            json_object_put(patch_obj);
            etcd_response_free(&get_resp);
            return -1;
        }

        merged_obj = current_obj;
        
        // Simple merge
        json_object_object_foreach(patch_obj, key, val) {
            json_object_object_add(merged_obj, key, json_object_get(val));
        }
    }

    const char* merged_json = json_object_to_json_string(merged_obj);
    
    etcd_response_t patch_resp = {0};
    int patch_status = etcd_manager_patch(etcd_key, merged_json, get_resp.revision, &patch_resp);

    if (patch_status == ETCD_OK) {
        json_object* metadata = json_object_object_get(merged_obj, "metadata");
        if (metadata) {
            char rv_str[32];
            snprintf(rv_str, sizeof(rv_str), "%lu", patch_resp.revision);
            json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
        }

        const char* response_json = json_object_to_json_string(merged_obj);
        strncpy(response_buffer, response_json, 16384 - 1);
        *response_code = http_status;
    } else if (patch_status == ETCD_CAS_FAILED) {
        snprintf(response_buffer, 16384, "{\"error\":\"Conflict\",\"message\":\"Secret was modified\"}");
        *response_code = 409;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"storage error\"}");
        *response_code = 500;
    }

    if (merged_obj && merged_obj != current_obj) {
        json_object_put(merged_obj);
    }
    if (current_obj) json_object_put(current_obj);
    json_object_put(patch_obj);
    etcd_response_free(&get_resp);
    etcd_response_free(&patch_resp);

    return (patch_status == ETCD_OK) ? 0 : -1;
}

int endpoint_delete_secret_etcd(const char* namespace, const char* name,
                                char* response_buffer, int* response_code) {
    char etcd_key[512];
    build_secret_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));

    etcd_response_t del_resp = {0};
    int status = etcd_manager_delete(etcd_key, &del_resp);

    if (status == ETCD_OK) {
        *response_code = 204;
        response_buffer[0] = '\0';
    } else if (status == ETCD_NOT_FOUND) {
        snprintf(response_buffer, 16384, "{\"error\":\"Secret not found\"}");
        *response_code = 404;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"storage error\"}");
        *response_code = 500;
    }

    etcd_response_free(&del_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

// ============================================================================
// PERSISTENTVOLUME ETCD-BACKED ENDPOINTS (Phase 6A)
// ============================================================================

static void build_pv_etcd_key(const char* name, char* key, int key_len) {
    snprintf(key, key_len, "/sirah/pv/%s", name ? name : "unknown");
}

int endpoint_create_pv_etcd(const char* body, char* response_buffer, int* response_code) {
    if (!body || strlen(body) == 0) {
        snprintf(response_buffer, 16384, "{\"error\":\"empty body\"}");
        *response_code = 400;
        return -1;
    }

    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        snprintf(response_buffer, 16384, "{\"error\":\"invalid JSON\"}");
        *response_code = 400;
        return -1;
    }

    json_object* metadata = json_object_object_get(obj, "metadata");
    if (!metadata) {
        snprintf(response_buffer, 16384, "{\"error\":\"metadata required\"}");
        *response_code = 400;
        json_object_put(obj);
        return -1;
    }

    const char* name = json_object_get_string(json_object_object_get(metadata, "name"));
    if (!name) {
        snprintf(response_buffer, 16384, "{\"error\":\"name required in metadata\"}");
        *response_code = 400;
        json_object_put(obj);
        return -1;
    }

    char etcd_key[512];
    build_pv_etcd_key(name, etcd_key, sizeof(etcd_key));

    const char* json_str = json_object_to_json_string(obj);
    etcd_response_t put_resp = {0};
    int status = etcd_manager_put(etcd_key, json_str, &put_resp);

    if (status == ETCD_OK) {
        char rv_str[32];
        snprintf(rv_str, sizeof(rv_str), "%lu", put_resp.revision);
        json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));

        const char* response_json = json_object_to_json_string(obj);
        strncpy(response_buffer, response_json, 16384 - 1);
        *response_code = 201;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"storage error\"}");
        *response_code = 500;
    }

    json_object_put(obj);
    etcd_response_free(&put_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

int endpoint_get_pv_etcd(const char* name, char* response_buffer, int* response_code) {
    char etcd_key[512];
    build_pv_etcd_key(name, etcd_key, sizeof(etcd_key));

    etcd_response_t get_resp = {0};
    int status = etcd_manager_get(etcd_key, &get_resp);

    if (status == ETCD_OK) {
        json_object* obj = json_tokener_parse(get_resp.value);
        if (obj) {
            json_object* meta = json_object_object_get(obj, "metadata");
            if (meta) {
                char rv_str[32];
                snprintf(rv_str, sizeof(rv_str), "%lu", get_resp.revision);
                json_object_object_add(meta, "resourceVersion", json_object_new_string(rv_str));
            }
            const char* json_str = json_object_to_json_string(obj);
            strncpy(response_buffer, json_str, 16384 - 1);
            json_object_put(obj);
        }
        *response_code = 200;
    } else if (status == ETCD_NOT_FOUND) {
        snprintf(response_buffer, 16384, "{\"error\":\"PersistentVolume not found\"}");
        *response_code = 404;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"storage error\"}");
        *response_code = 500;
    }

    etcd_response_free(&get_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

/**
 * PATCH /api/v1/persistentvolumes/{name}
 */
int endpoint_patch_pv_etcd(const char* name, const char* body, const char* content_type,
                           char* response_buffer, int* response_code) {
    if (!body || strlen(body) == 0) {
        snprintf(response_buffer, 16384, "{\"error\":\"empty body\"}");
        *response_code = 400;
        return -1;
    }

    char etcd_key[512];
    build_pv_etcd_key(name, etcd_key, sizeof(etcd_key));

    // Get current version
    etcd_response_t get_resp = {0};
    int get_status = etcd_manager_get(etcd_key, &get_resp);

    json_object* current_obj = NULL;
    if (get_status == ETCD_OK) {
        current_obj = json_tokener_parse(get_resp.value);
        if (!current_obj) {
            snprintf(response_buffer, 16384, "{\"error\":\"invalid pv data\"}");
            *response_code = 500;
            etcd_response_free(&get_resp);
            return -1;
        }
    } else if (get_status != ETCD_NOT_FOUND) {
        snprintf(response_buffer, 16384, "{\"error\":\"storage error\"}");
        *response_code = 500;
        etcd_response_free(&get_resp);
        return -1;
    }

    json_object* patch_obj = json_tokener_parse(body);
    if (!patch_obj) {
        snprintf(response_buffer, 16384, "{\"error\":\"invalid JSON\"}");
        *response_code = 400;
        if (current_obj) json_object_put(current_obj);
        etcd_response_free(&get_resp);
        return -1;
    }

    int is_kubectl_apply = (content_type && 
        (strstr(content_type, "apply-patch") || strstr(content_type, "strategic-merge-patch")));

    json_object* merged_obj = NULL;
    int http_status = 200;

    if (is_kubectl_apply && current_obj) {
        kubectl_apply_result_t* apply_result = kubectl_apply_three_way_merge(
            current_obj, patch_obj, "", name);

        if (!apply_result->success) {
            snprintf(response_buffer, 16384, "%s", apply_result->error_message);
            *response_code = apply_result->response_code;
            kubectl_apply_result_free(apply_result);
            json_object_put(current_obj);
            json_object_put(patch_obj);
            etcd_response_free(&get_resp);
            return -1;
        }

        merged_obj = apply_result->result_obj;
        http_status = apply_result->response_code;
        free(apply_result);
    } else if (is_kubectl_apply && !current_obj) {
        merged_obj = NULL;
        json_object_deep_copy(patch_obj, &merged_obj, NULL);
        if (!merged_obj) merged_obj = patch_obj;
        const char* patch_json = json_object_to_json_string(patch_obj);
        kubectl_apply_set_last_applied(merged_obj, patch_json);
        http_status = 201;
    } else {
        if (!current_obj) {
            snprintf(response_buffer, 16384, "{\"error\":\"PersistentVolume not found\"}");
            *response_code = 404;
            json_object_put(patch_obj);
            etcd_response_free(&get_resp);
            return -1;
        }

        merged_obj = current_obj;
        
        // Simple merge
        json_object_object_foreach(patch_obj, key, val) {
            json_object_object_add(merged_obj, key, json_object_get(val));
        }
    }

    const char* merged_json = json_object_to_json_string(merged_obj);
    
    etcd_response_t patch_resp = {0};
    int patch_status = etcd_manager_patch(etcd_key, merged_json, get_resp.revision, &patch_resp);

    if (patch_status == ETCD_OK) {
        json_object* metadata = json_object_object_get(merged_obj, "metadata");
        if (metadata) {
            char rv_str[32];
            snprintf(rv_str, sizeof(rv_str), "%lu", patch_resp.revision);
            json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
        }

        const char* response_json = json_object_to_json_string(merged_obj);
        strncpy(response_buffer, response_json, 16384 - 1);
        *response_code = http_status;
    } else if (patch_status == ETCD_CAS_FAILED) {
        snprintf(response_buffer, 16384, "{\"error\":\"Conflict\",\"message\":\"PersistentVolume was modified\"}");
        *response_code = 409;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"storage error\"}");
        *response_code = 500;
    }

    if (merged_obj && merged_obj != current_obj) {
        json_object_put(merged_obj);
    }
    if (current_obj) json_object_put(current_obj);
    json_object_put(patch_obj);
    etcd_response_free(&get_resp);
    etcd_response_free(&patch_resp);

    return (patch_status == ETCD_OK) ? 0 : -1;
}

int endpoint_list_pv_etcd(char* response_buffer, int* response_code) {
    const char* etcd_prefix = "/sirah/pv/";

    etcd_response_t list_resp = {0};
    int status = etcd_manager_list(etcd_prefix, &list_resp);

    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("PersistentVolumeList"));
    json_object* items = json_object_new_array();

    if (status == ETCD_OK && list_resp.kvs_count > 0) {
        for (int i = 0; i < list_resp.kvs_count; i++) {
            json_object* obj = json_tokener_parse(list_resp.kvs_values[i]);
            if (obj) {
                json_object* meta = json_object_object_get(obj, "metadata");
                if (meta) {
                    char rv_str[32];
                    snprintf(rv_str, sizeof(rv_str), "%lu", list_resp.kvs_versions[i]);
                    json_object_object_add(meta, "resourceVersion", json_object_new_string(rv_str));
                }
                json_object_array_add(items, obj);
            }
        }
    }

    json_object_object_add(root, "items", items);
    const char* json_str = json_object_to_json_string(root);
    strncpy(response_buffer, json_str, 16384 - 1);
    *response_code = 200;

    json_object_put(root);
    etcd_response_free(&list_resp);
    return 0;
}

int endpoint_delete_pv_etcd(const char* name, char* response_buffer, int* response_code) {
    char etcd_key[512];
    build_pv_etcd_key(name, etcd_key, sizeof(etcd_key));

    etcd_response_t del_resp = {0};
    int status = etcd_manager_delete(etcd_key, &del_resp);

    if (status == ETCD_OK) {
        *response_code = 204;
        response_buffer[0] = '\0';
    } else if (status == ETCD_NOT_FOUND) {
        snprintf(response_buffer, 16384, "{\"error\":\"PersistentVolume not found\"}");
        *response_code = 404;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"storage error\"}");
        *response_code = 500;
    }

    etcd_response_free(&del_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

// ============================================================================
// PERSISTENTVOLUMECLAIM ETCD-BACKED ENDPOINTS (Phase 6A)
// ============================================================================

static void build_pvc_etcd_key(const char* namespace, const char* name,
                               char* key, int key_len) {
    snprintf(key, key_len, "/sirah/pvc/%s/%s",
             namespace ? namespace : "default",
             name ? name : "unknown");
}

int endpoint_create_pvc_etcd(const char* namespace, const char* body,
                             char* response_buffer, int* response_code) {
    if (!body || strlen(body) == 0) {
        snprintf(response_buffer, 16384, "{\"error\":\"empty body\"}");
        *response_code = 400;
        return -1;
    }

    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        snprintf(response_buffer, 16384, "{\"error\":\"invalid JSON\"}");
        *response_code = 400;
        return -1;
    }

    json_object* metadata = json_object_object_get(obj, "metadata");
    if (!metadata) {
        snprintf(response_buffer, 16384, "{\"error\":\"metadata required\"}");
        *response_code = 400;
        json_object_put(obj);
        return -1;
    }

    const char* name = json_object_get_string(json_object_object_get(metadata, "name"));
    if (!name) {
        snprintf(response_buffer, 16384, "{\"error\":\"name required in metadata\"}");
        *response_code = 400;
        json_object_put(obj);
        return -1;
    }

    char etcd_key[512];
    build_pvc_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));

    const char* json_str = json_object_to_json_string(obj);
    etcd_response_t put_resp = {0};
    int status = etcd_manager_put(etcd_key, json_str, &put_resp);

    if (status == ETCD_OK) {
        char rv_str[32];
        snprintf(rv_str, sizeof(rv_str), "%lu", put_resp.revision);
        json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));

        const char* response_json = json_object_to_json_string(obj);
        strncpy(response_buffer, response_json, 16384 - 1);
        *response_code = 201;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"storage error\"}");
        *response_code = 500;
    }

    json_object_put(obj);
    etcd_response_free(&put_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

int endpoint_get_pvc_etcd(const char* namespace, const char* name,
                          char* response_buffer, int* response_code) {
    char etcd_key[512];
    build_pvc_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));

    etcd_response_t get_resp = {0};
    int status = etcd_manager_get(etcd_key, &get_resp);

    if (status == ETCD_OK) {
        json_object* obj = json_tokener_parse(get_resp.value);
        if (obj) {
            json_object* meta = json_object_object_get(obj, "metadata");
            if (meta) {
                char rv_str[32];
                snprintf(rv_str, sizeof(rv_str), "%lu", get_resp.revision);
                json_object_object_add(meta, "resourceVersion", json_object_new_string(rv_str));
            }
            const char* json_str = json_object_to_json_string(obj);
            strncpy(response_buffer, json_str, 16384 - 1);
            json_object_put(obj);
        }
        *response_code = 200;
    } else if (status == ETCD_NOT_FOUND) {
        snprintf(response_buffer, 16384, "{\"error\":\"PersistentVolumeClaim not found\"}");
        *response_code = 404;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"storage error\"}");
        *response_code = 500;
    }

    etcd_response_free(&get_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

/**
 * PATCH /api/v1/namespaces/{namespace}/persistentvolumeclaims/{name}
 */
int endpoint_patch_pvc_etcd(const char* namespace, const char* name,
                             const char* body, const char* content_type,
                             char* response_buffer, int* response_code) {
    if (!body || strlen(body) == 0) {
        snprintf(response_buffer, 16384, "{\"error\":\"empty body\"}");
        *response_code = 400;
        return -1;
    }

    char etcd_key[512];
    build_pvc_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));

    // Get current version
    etcd_response_t get_resp = {0};
    int get_status = etcd_manager_get(etcd_key, &get_resp);

    json_object* current_obj = NULL;
    if (get_status == ETCD_OK) {
        current_obj = json_tokener_parse(get_resp.value);
        if (!current_obj) {
            snprintf(response_buffer, 16384, "{\"error\":\"invalid pvc data\"}");
            *response_code = 500;
            etcd_response_free(&get_resp);
            return -1;
        }
    } else if (get_status != ETCD_NOT_FOUND) {
        snprintf(response_buffer, 16384, "{\"error\":\"storage error\"}");
        *response_code = 500;
        etcd_response_free(&get_resp);
        return -1;
    }

    json_object* patch_obj = json_tokener_parse(body);
    if (!patch_obj) {
        snprintf(response_buffer, 16384, "{\"error\":\"invalid JSON\"}");
        *response_code = 400;
        if (current_obj) json_object_put(current_obj);
        etcd_response_free(&get_resp);
        return -1;
    }

    int is_kubectl_apply = (content_type && 
        (strstr(content_type, "apply-patch") || strstr(content_type, "strategic-merge-patch")));

    json_object* merged_obj = NULL;
    int http_status = 200;

    if (is_kubectl_apply && current_obj) {
        kubectl_apply_result_t* apply_result = kubectl_apply_three_way_merge(
            current_obj, patch_obj, namespace, name);

        if (!apply_result->success) {
            snprintf(response_buffer, 16384, "%s", apply_result->error_message);
            *response_code = apply_result->response_code;
            kubectl_apply_result_free(apply_result);
            json_object_put(current_obj);
            json_object_put(patch_obj);
            etcd_response_free(&get_resp);
            return -1;
        }

        merged_obj = apply_result->result_obj;
        http_status = apply_result->response_code;
        free(apply_result);
    } else if (is_kubectl_apply && !current_obj) {
        merged_obj = NULL;
        json_object_deep_copy(patch_obj, &merged_obj, NULL);
        if (!merged_obj) merged_obj = patch_obj;
        const char* patch_json = json_object_to_json_string(patch_obj);
        kubectl_apply_set_last_applied(merged_obj, patch_json);
        http_status = 201;
    } else {
        if (!current_obj) {
            snprintf(response_buffer, 16384, "{\"error\":\"PersistentVolumeClaim not found\"}");
            *response_code = 404;
            json_object_put(patch_obj);
            etcd_response_free(&get_resp);
            return -1;
        }

        merged_obj = current_obj;
        
        // Simple merge
        json_object_object_foreach(patch_obj, key, val) {
            json_object_object_add(merged_obj, key, json_object_get(val));
        }
    }

    const char* merged_json = json_object_to_json_string(merged_obj);
    
    etcd_response_t patch_resp = {0};
    int patch_status = etcd_manager_patch(etcd_key, merged_json, get_resp.revision, &patch_resp);

    if (patch_status == ETCD_OK) {
        json_object* metadata = json_object_object_get(merged_obj, "metadata");
        if (metadata) {
            char rv_str[32];
            snprintf(rv_str, sizeof(rv_str), "%lu", patch_resp.revision);
            json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
        }

        const char* response_json = json_object_to_json_string(merged_obj);
        strncpy(response_buffer, response_json, 16384 - 1);
        *response_code = http_status;
    } else if (patch_status == ETCD_CAS_FAILED) {
        snprintf(response_buffer, 16384, "{\"error\":\"Conflict\",\"message\":\"PersistentVolumeClaim was modified\"}");
        *response_code = 409;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"storage error\"}");
        *response_code = 500;
    }

    if (merged_obj && merged_obj != current_obj) {
        json_object_put(merged_obj);
    }
    if (current_obj) json_object_put(current_obj);
    json_object_put(patch_obj);
    etcd_response_free(&get_resp);
    etcd_response_free(&patch_resp);

    return (patch_status == ETCD_OK) ? 0 : -1;
}

int endpoint_list_pvc_etcd(const char* namespace, char* response_buffer, int* response_code) {
    char etcd_prefix[512];
    snprintf(etcd_prefix, sizeof(etcd_prefix), "/sirah/pvc/%s/",
             namespace ? namespace : "default");

    etcd_response_t list_resp = {0};
    int status = etcd_manager_list(etcd_prefix, &list_resp);

    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("PersistentVolumeClaimList"));
    json_object* items = json_object_new_array();

    if (status == ETCD_OK && list_resp.kvs_count > 0) {
        for (int i = 0; i < list_resp.kvs_count; i++) {
            json_object* obj = json_tokener_parse(list_resp.kvs_values[i]);
            if (obj) {
                json_object* meta = json_object_object_get(obj, "metadata");
                if (meta) {
                    char rv_str[32];
                    snprintf(rv_str, sizeof(rv_str), "%lu", list_resp.kvs_versions[i]);
                    json_object_object_add(meta, "resourceVersion", json_object_new_string(rv_str));
                }
                json_object_array_add(items, obj);
            }
        }
    }

    json_object_object_add(root, "items", items);
    const char* json_str = json_object_to_json_string(root);
    strncpy(response_buffer, json_str, 16384 - 1);
    *response_code = 200;

    json_object_put(root);
    etcd_response_free(&list_resp);
    return 0;
}

int endpoint_delete_pvc_etcd(const char* namespace, const char* name,
                             char* response_buffer, int* response_code) {
    char etcd_key[512];
    build_pvc_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));

    etcd_response_t del_resp = {0};
    int status = etcd_manager_delete(etcd_key, &del_resp);

    if (status == ETCD_OK) {
        *response_code = 204;
        response_buffer[0] = '\0';
    } else if (status == ETCD_NOT_FOUND) {
        snprintf(response_buffer, 16384, "{\"error\":\"PersistentVolumeClaim not found\"}");
        *response_code = 404;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"storage error\"}");
        *response_code = 500;
    }

    etcd_response_free(&del_resp);
    return (status == ETCD_OK) ? 0 : -1;
}