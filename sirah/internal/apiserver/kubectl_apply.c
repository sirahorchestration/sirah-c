// internal/apiserver/kubectl_apply.c
// Implementation of kubectl apply declarative configuration management
// Implements 3-way merge for classic client-side apply mode
// Based on Kubernetes documented apply behavior and KEP-555 design principles

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include "kubectl_apply.h"

/**
 * Determine the apply mode based on current state and desired state
 */
kubectl_apply_mode_t kubectl_apply_determine_mode(json_object* current_obj,
                                                   json_object* desired_obj) {
    if (!desired_obj) return APPLY_MODE_UNKNOWN;
    
    // If current doesn't exist, this is a create operation
    if (!current_obj) {
        return APPLY_MODE_CREATE;
    }
    
    // Object exists - will be a merge unless we detect conflicts
    // Check if there's a last-applied config to compare against
    json_object* last_applied = kubectl_apply_get_last_applied(current_obj);
    
    if (!last_applied) {
        // No last-applied means this object was created outside kubectl
        // We could treat this as conflict or allow merge - for MVP, allow merge
        return APPLY_MODE_MERGE;
    }
    
    return APPLY_MODE_MERGE;
}

/**
 * Extract last-applied configuration from object annotations
 */
json_object* kubectl_apply_get_last_applied(json_object* obj) {
    if (!obj) return NULL;
    
    json_object* metadata = json_object_object_get(obj, "metadata");
    if (!metadata) return NULL;
    
    json_object* annotations = json_object_object_get(metadata, "annotations");
    if (!annotations) return NULL;
    
    json_object* last_applied_str = json_object_object_get(
        annotations, KUBECTL_LAST_APPLIED_ANNOTATION);
    if (!last_applied_str) return NULL;
    
    const char* last_applied_json = json_object_get_string(last_applied_str);
    if (!last_applied_json) return NULL;
    
    // Parse the stored JSON string
    json_object* last_applied = json_tokener_parse(last_applied_json);
    return last_applied;
}

/**
 * Store last-applied configuration in object annotations
 */
void kubectl_apply_set_last_applied(json_object* obj, const char* desired_json) {
    if (!obj || !desired_json) return;
    
    json_object* metadata = json_object_object_get(obj, "metadata");
    if (!metadata) {
        metadata = json_object_new_object();
        json_object_object_add(obj, "metadata", metadata);
    }
    
    json_object* annotations = json_object_object_get(metadata, "annotations");
    if (!annotations) {
        annotations = json_object_new_object();
        json_object_object_add(metadata, "annotations", annotations);
    }
    
    // Store the desired config as a string in the annotation
    json_object_object_add(annotations, 
        KUBECTL_LAST_APPLIED_ANNOTATION,
        json_object_new_string(desired_json));
}

/**
 * Simple recursive merge for JSON objects
 * Merges patch into base, returning new object with merged values
 * 
 * For kubectl apply 3-way merge:
 * - Takes (last_applied, current, desired)
 * - Returns merged result that respects user changes outside kubectl
 */
static json_object* json_merge_recursive(json_object* last_applied,
                                         json_object* current,
                                         json_object* desired) {
    if (!desired) return NULL;
    
    // Start with a copy of desired
    json_object* result = NULL;
    json_object_deep_copy(desired, &result, NULL);
    if (!result) result = json_object_new_object();
    
    // Process each field in desired
    struct json_object_iterator iter = json_object_iter_begin(desired);
    struct json_object_iterator iter_end = json_object_iter_end(desired);
    
    while (!json_object_iter_equal(&iter, &iter_end)) {
        const char* key = json_object_iter_peek_name(&iter);
        json_object* desired_val = json_object_iter_peek_value(&iter);
        
        json_object* current_val = current ? json_object_object_get(current, key) : NULL;
        json_object* last_val = last_applied ? json_object_object_get(last_applied, key) : NULL;
        
        // Three-way merge logic:
        if (desired_val && json_object_is_type(desired_val, json_type_object)) {
            // Recurse into nested objects
            json_object* merged = json_merge_recursive(last_val, current_val, desired_val);
            if (merged) {
                json_object_object_add(result, key, merged);
            }
        } else {
            // For scalar values, keep desired value (kubectl's intent wins)
            json_object_object_add(result, key, 
                json_object_get(desired_val));  // Increase refcount
        }
        
        json_object_iter_next(&iter);
    }
    
    // Handle fields that exist in current but not in desired
    if (current) {
        struct json_object_iterator iter = json_object_iter_begin(current);
        struct json_object_iterator iter_end = json_object_iter_end(current);
        
        while (!json_object_iter_equal(&iter, &iter_end)) {
            const char* key = json_object_iter_peek_name(&iter);
            json_object* current_val = json_object_iter_peek_value(&iter);
            
            // Skip if already processed
            if (!json_object_object_get(desired, key)) {
                json_object* last_val = last_applied ? 
                    json_object_object_get(last_applied, key) : NULL;
                
                if (!last_val) {
                    // Field only in current - was added after kubectl apply
                    // Keep it (user/controller added it)
                    json_object_object_add(result, key,
                        json_object_get(current_val));
                }
                // If field was in last_applied but not in desired, kubectl removed it
                // so we don't add it to result (it's removed)
            }
            
            json_object_iter_next(&iter);
        }
    }
    
    return result;
}

/**
 * Perform 3-way merge for kubectl apply
 */
kubectl_apply_result_t* kubectl_apply_three_way_merge(json_object* current_obj,
                                                       json_object* desired_obj,
                                                       const char* namespace,
                                                       const char* resource_name) {
    kubectl_apply_result_t* result = malloc(sizeof(*result));
    if (!result) return NULL;
    
    memset(result, 0, sizeof(*result));
    result->success = 0;
    result->response_code = 500;
    
    if (!desired_obj) {
        snprintf(result->error_message, sizeof(result->error_message),
            "Desired configuration is empty");
        return result;
    }
    
    // Determine apply mode
    kubectl_apply_mode_t mode = kubectl_apply_determine_mode(current_obj, desired_obj);
    
    if (mode == APPLY_MODE_CREATE) {
        // Creating new object - just use desired config
        result->result_obj = NULL;
        json_object_deep_copy(desired_obj, &result->result_obj, NULL);
        result->success = 1;
        result->response_code = 201;  // Created
        
        // Store last-applied annotation
        const char* desired_json = json_object_to_json_string(desired_obj);
        kubectl_apply_set_last_applied(result->result_obj, desired_json);
        
        return result;
    }
    
    if (mode == APPLY_MODE_MERGE) {
        // Get last-applied config for 3-way merge
        json_object* last_applied = kubectl_apply_get_last_applied(current_obj);
        
        // Perform 3-way merge
        json_object* merged = json_merge_recursive(last_applied, current_obj, desired_obj);
        if (!merged) {
            snprintf(result->error_message, sizeof(result->error_message),
                "Failed to merge configurations");
            if (last_applied) json_object_put(last_applied);
            return result;
        }
        
        result->result_obj = merged;
        result->success = 1;
        result->response_code = 200;  // OK
        
        // Update last-applied annotation with new desired config
        const char* desired_json = json_object_to_json_string(desired_obj);
        kubectl_apply_set_last_applied(result->result_obj, desired_json);
        
        if (last_applied) json_object_put(last_applied);
        return result;
    }
    
    snprintf(result->error_message, sizeof(result->error_message),
        "Unknown apply mode");
    return result;
}

/**
 * Detect field conflicts between current and desired
 */
int kubectl_apply_detect_field_conflict(json_object* current_obj, json_object* desired_obj) {
    if (!current_obj || !desired_obj) return 0;
    
    json_object* last_applied = kubectl_apply_get_last_applied(current_obj);
    if (!last_applied) {
        // No last-applied - object created outside kubectl, no conflict detection
        return 0;
    }
    
    // Check if any field was modified in current that differs from both last-applied and desired
    struct json_object_iterator iter = json_object_iter_begin(current_obj);
    struct json_object_iterator iter_end = json_object_iter_end(current_obj);
    
    int conflict = 0;
    while (!json_object_iter_equal(&iter, &iter_end)) {
        const char* key = json_object_iter_peek_name(&iter);
        const char* skip_fields[] = {"metadata", "status", NULL};
        
        // Skip metadata and status fields
        int skip = 0;
        for (int i = 0; skip_fields[i]; i++) {
            if (strcmp(key, skip_fields[i]) == 0) {
                skip = 1;
                break;
            }
        }
        
        if (!skip) {
            json_object* current_val = json_object_iter_peek_value(&iter);
            json_object* desired_val = json_object_object_get(desired_obj, key);
            json_object* last_val = json_object_object_get(last_applied, key);
            
            // Conflict if: current != desired && current != last_applied
            if (desired_val && current_val) {
                const char* current_str = json_object_to_json_string(current_val);
                const char* desired_str = json_object_to_json_string(desired_val);
                const char* last_str = last_val ? json_object_to_json_string(last_val) : "";
                
                if (strcmp(current_str, desired_str) != 0 && 
                    strcmp(current_str, last_str) != 0) {
                    conflict = 1;
                    break;
                }
            }
        }
        
        json_object_iter_next(&iter);
    }
    
    if (last_applied) json_object_put(last_applied);
    return conflict;
}

/**
 * Free kubectl apply result
 */
void kubectl_apply_result_free(kubectl_apply_result_t* result) {
    if (!result) return;
    if (result->result_obj) json_object_put(result->result_obj);
    free(result);
}
