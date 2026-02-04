// internal/apiserver/handler_integration_example.c
// Example: How to integrate Phase 2 features into existing handlers
// This file shows the patterns to apply to endpoint_*_pod(), endpoint_*_service(), etc.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include "handler.h"
#include "resource_version.h"
#include "event_system.h"
#include "watch_manager.h"
#include "cas_handler.h"
#include "validation.h"

// ============================================================================
// EXAMPLE 1: POST Create Pod Handler
// ============================================================================

// Before calling this, ensure:
// - resource_version_init() called in main()
// - event_system_init() called in main()
// - watch_manager_init() called in main()
// - cas_handler_init() called in main()

void endpoint_create_pod_with_phase2(const char* namespace, const char* body,
                                     char* response_buffer, int* response_code) {
    // 1. Parse request body
    json_object* request_obj = json_tokener_parse(body);
    if (!request_obj) {
        *response_code = 400;
        sprintf(response_buffer, "{\"error\": \"Invalid JSON\"}");
        return;
    }
    
    // 2. Extract pod name
    json_object* metadata = NULL;
    if (!json_object_object_get_ex(request_obj, "metadata", &metadata)) {
        *response_code = 400;
        sprintf(response_buffer, "{\"error\": \"No metadata\"}");
        json_object_put(request_obj);
        return;
    }
    
    json_object* name_obj = NULL;
    if (!json_object_object_get_ex(metadata, "name", &name_obj)) {
        *response_code = 400;
        sprintf(response_buffer, "{\"error\": \"No pod name\"}");
        json_object_put(request_obj);
        return;
    }
    
    const char* pod_name = json_object_get_string(name_obj);
    
    // 3. Validate pod spec (existing validation)
    validation_errors_t validation_errors;
    if (validate_pod_spec(request_obj, &validation_errors) != 0) {
        *response_code = 400;
        // ... return validation errors
        json_object_put(request_obj);
        return;
    }
    
    // 4. [NEW] Assign resourceVersion to new pod
    uint64_t initial_version = resource_version_next();
    resource_version_add_to_json(request_obj, initial_version);
    
    // 5. Store pod (existing code)
    store_put_pod(&pod_store, request_obj);  // or your storage mechanism
    
    // 6. [NEW] Notify all watchers of pod creation
    event_t* event = event_new(EVENT_TYPE_ADDED, "pods", namespace, pod_name, request_obj);
    if (event) {
        event_dispatch(event);
        event_free(event);
    }
    
    // 7. Return created pod
    const char* pod_json = json_object_to_json_string_ext(request_obj, JSON_C_TO_STRING_PRETTY);
    strcpy(response_buffer, pod_json);
    *response_code = 201;
    
    json_object_put(request_obj);
}

// ============================================================================
// EXAMPLE 2: PATCH Update Pod Handler (with CAS)
// ============================================================================

void endpoint_patch_pod_with_phase2(const char* namespace, const char* pod_name,
                                    const char* body, char* response_buffer, int* response_code) {
    // 1. Parse request body
    json_object* patch_obj = json_tokener_parse(body);
    if (!patch_obj) {
        *response_code = 400;
        sprintf(response_buffer, "{\"error\": \"Invalid JSON\"}");
        return;
    }
    
    // 2. [NEW] Extract expected resourceVersion from patch
    uint64_t expected_version = 0;
    json_object* metadata = NULL;
    if (json_object_object_get_ex(patch_obj, "metadata", &metadata)) {
        json_object* rv_obj = NULL;
        if (json_object_object_get_ex(metadata, "resourceVersion", &rv_obj)) {
            const char* rv_str = json_object_get_string(rv_obj);
            if (rv_str) {
                expected_version = strtoull(rv_str, NULL, 10);
            }
        }
    }
    
    // 3. [NEW] Check for CAS conflict
    if (expected_version > 0) {
        cas_result_t cas_result = cas_check_version("pods", namespace, pod_name, expected_version);
        if (cas_result == CAS_RESULT_CONFLICT) {
            // Version mismatch - return 409 Conflict
            *response_code = 409;
            json_object* current_pod = store_get_pod(&pod_store, namespace, pod_name);
            if (current_pod) {
                char* conflict_response = cas_generate_conflict_response(
                    "pods", namespace, pod_name, current_pod);
                strcpy(response_buffer, conflict_response);
                free(conflict_response);
                json_object_put(current_pod);
            } else {
                sprintf(response_buffer, "{\"error\": \"Resource not found\"}");
                *response_code = 404;
            }
            json_object_put(patch_obj);
            return;
        }
    }
    
    // 4. Get current pod
    json_object* current_pod = store_get_pod(&pod_store, namespace, pod_name);
    if (!current_pod) {
        *response_code = 404;
        sprintf(response_buffer, "{\"error\": \"Pod not found\"}");
        json_object_put(patch_obj);
        return;
    }
    
    // 5. Merge patch into current pod (strategic merge)
    json_object* updated_pod = merge_patch(current_pod, patch_obj);
    
    // 6. Validate updated pod
    validation_errors_t validation_errors;
    if (validate_pod_spec(updated_pod, &validation_errors) != 0) {
        *response_code = 400;
        // ... return validation errors
        json_object_put(patch_obj);
        json_object_put(updated_pod);
        return;
    }
    
    // 7. [NEW] Update resourceVersion (increment it)
    resource_version_update_object(updated_pod);
    
    // 8. Store updated pod
    store_put_pod(&pod_store, updated_pod);
    
    // 9. [NEW] Notify watchers of pod modification
    event_t* event = event_new(EVENT_TYPE_MODIFIED, "pods", namespace, pod_name, updated_pod);
    if (event) {
        event_dispatch(event);
        event_free(event);
    }
    
    // 10. Return updated pod
    const char* pod_json = json_object_to_json_string_ext(updated_pod, JSON_C_TO_STRING_PRETTY);
    strcpy(response_buffer, pod_json);
    *response_code = 200;
    
    json_object_put(patch_obj);
    json_object_put(updated_pod);
    json_object_put(current_pod);
}

// ============================================================================
// EXAMPLE 3: DELETE Pod Handler
// ============================================================================

void endpoint_delete_pod_with_phase2(const char* namespace, const char* pod_name,
                                     char* response_buffer, int* response_code) {
    // 1. Get pod before deleting (for event)
    json_object* pod = store_get_pod(&pod_store, namespace, pod_name);
    if (!pod) {
        *response_code = 404;
        sprintf(response_buffer, "{\"error\": \"Pod not found\"}");
        return;
    }
    
    // 2. Delete pod
    store_delete_pod(&pod_store, namespace, pod_name);
    
    // 3. [NEW] Notify watchers of pod deletion
    event_t* event = event_new(EVENT_TYPE_DELETED, "pods", namespace, pod_name, pod);
    if (event) {
        event_dispatch(event);
        event_free(event);
    }
    
    // 4. Return success
    *response_code = 204;
    response_buffer[0] = '\0';  // No content
    
    json_object_put(pod);
}

// ============================================================================
// EXAMPLE 4: Watch Pods Handler (NEW)
// ============================================================================

void endpoint_watch_pods_with_phase2(const char* namespace, const char* query_string,
                                     char* response_buffer, int* response_code) {
    // 1. Parse query parameters
    const char* label_sel = get_query_param(query_string, "labelSelector");
    const char* field_sel = get_query_param(query_string, "fieldSelector");
    const char* res_ver_str = get_query_param(query_string, "resourceVersion");
    unsigned long res_ver = res_ver_str ? strtoul(res_ver_str, NULL, 10) : 0;
    
    // 2. Create watch subscription
    int watch_id = watch_create("pods", namespace, label_sel, field_sel, res_ver);
    if (watch_id < 0) {
        *response_code = 400;
        sprintf(response_buffer, "{\"error\": \"Failed to create watch\"}");
        return;
    }
    
    // 3. Setup HTTP chunked response
    strcpy(response_buffer, "");  // Start fresh
    strcat(response_buffer, "HTTP/1.1 200 OK\r\n");
    strcat(response_buffer, "Content-Type: application/json\r\n");
    strcat(response_buffer, "Transfer-Encoding: chunked\r\n");
    strcat(response_buffer, "Cache-Control: no-cache\r\n");
    strcat(response_buffer, "Connection: Keep-Alive\r\n");
    strcat(response_buffer, "\r\n");
    
    *response_code = 200;
    
    // 4. Send initial list of existing pods (if resourceVersion=0)
    if (res_ver == 0) {
        json_object* pods_list = store_list_pods(&pod_store, namespace);
        json_array_foreach(pods_list, idx, pod) {
            // Apply filters
            if (label_sel && !watch_match_label_selector(pod, label_sel)) continue;
            if (field_sel && !watch_match_field_selector(pod, field_sel)) continue;
            
            char* ndjson = event_to_ndjson(
                event_new(EVENT_TYPE_ADDED, "pods", namespace, 
                         json_object_get_string(json_object_object_get(pod, "metadata.name")), 
                         pod));
            
            // Send chunk
            int chunk_size = strlen(ndjson);
            char chunk_header[32];
            sprintf(chunk_header, "%x\r\n", chunk_size);
            strcat(response_buffer, chunk_header);
            strcat(response_buffer, ndjson);
            strcat(response_buffer, "\r\n");
            free(ndjson);
        }
    }
    
    // 5. [NOTE] In real implementation, this would be async/streaming
    // For now, just close the watch
    // In production, you'd:
    // - Keep connection open
    // - Send events as they arrive (via event_dispatch)
    // - Periodically send BOOKMARK events
    // - Handle client disconnect
    
    // Send closing chunk
    strcat(response_buffer, "0\r\n\r\n");
    
    watch_close(watch_id);
}

// ============================================================================
// EXAMPLE 5: Initialization in main()
// ============================================================================

int main(int argc, char* argv[]) {
    // ... existing initialization ...
    
    // NEW: Initialize Phase 2 systems
    resource_version_init();
    event_system_init();
    watch_manager_init();
    cas_handler_init();
    
    // ... rest of main ...
}

// ============================================================================
// Key Integration Points Summary
// ============================================================================

/*
 * PATTERN 1: On CREATE (POST)
 * ├─ Parse request
 * ├─ Validate spec
 * ├─ [NEW] Assign resourceVersion: resource_version_next()
 * ├─ Store object
 * ├─ [NEW] Notify watchers: event_dispatch(EVENT_TYPE_ADDED)
 * └─ Return 201 with resourceVersion
 *
 * PATTERN 2: On UPDATE (PATCH/PUT)
 * ├─ Extract expected resourceVersion from request
 * ├─ [NEW] Check CAS: cas_check_version()
 * │  └─ If conflict: return 409 with current object
 * ├─ Merge/validate changes
 * ├─ [NEW] Update resourceVersion: resource_version_update_object()
 * ├─ Store updated object
 * ├─ [NEW] Notify watchers: event_dispatch(EVENT_TYPE_MODIFIED)
 * └─ Return 200 with new resourceVersion
 *
 * PATTERN 3: On DELETE (DELETE)
 * ├─ Get object
 * ├─ Delete from storage
 * ├─ [NEW] Notify watchers: event_dispatch(EVENT_TYPE_DELETED)
 * └─ Return 204
 *
 * PATTERN 4: On WATCH (GET ?watch=true)
 * ├─ Create subscription: watch_create()
 * ├─ Setup chunked response
 * ├─ Send initial objects (if resourceVersion=0)
 * ├─ Loop: wait for events and send NDJSON
 * └─ Clean up: watch_close()
 */

// ============================================================================
// Helper Functions Needed
// ============================================================================

// Parse query parameter
static const char* get_query_param(const char* query_string, const char* param_name) {
    if (!query_string || !param_name) return NULL;
    
    char search[256];
    snprintf(search, sizeof(search), "%s=", param_name);
    
    const char* pos = strstr(query_string, search);
    if (!pos) return NULL;
    
    static char value[256];
    const char* start = pos + strlen(search);
    const char* end = strchr(start, '&');
    
    int len = end ? (end - start) : strlen(start);
    if (len >= sizeof(value)) len = sizeof(value) - 1;
    
    strncpy(value, start, len);
    value[len] = '\0';
    
    return value;
}

// Merge patch (strategic merge patch per Kubernetes spec)
static json_object* merge_patch(json_object* base, json_object* patch) {
    // For simplicity, this example does a shallow merge
    // Real implementation should do deep strategic merge
    json_object* result = json_object_new_object();
    
    // Copy all from base
    json_object_iterator iterator = json_object_iter_begin(base);
    while (!json_object_iter_end(&iterator)) {
        const char* key = json_object_iter_peek_name(&iterator);
        json_object* value = json_object_iter_peek_value(&iterator);
        json_object_object_add(result, key, value);
        json_object_iter_next(&iterator);
    }
    
    // Override with patch values
    iterator = json_object_iter_begin(patch);
    while (!json_object_iter_end(&iterator)) {
        const char* key = json_object_iter_peek_name(&iterator);
        json_object* value = json_object_iter_peek_value(&iterator);
        json_object_object_add(result, key, value);
        json_object_iter_next(&iterator);
    }
    
    return result;
}
