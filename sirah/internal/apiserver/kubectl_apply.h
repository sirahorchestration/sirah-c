// internal/apiserver/kubectl_apply.h
// Support for kubectl apply declarative configuration management
// Implements classic client-side apply with 3-way merge (last-applied + current + desired)
// Reference: https://kubernetes.io/docs/tasks/manage-kubernetes-objects/declarative-config/
// KEP-555: Server-side apply (future enhancement for --server-side support)

#ifndef SIRAH_KUBECTL_APPLY_H
#define SIRAH_KUBECTL_APPLY_H

#include <json-c/json.h>

/**
 * kubectl apply annotation constants
 */
#define KUBECTL_LAST_APPLIED_ANNOTATION "kubectl.kubernetes.io/last-applied-configuration"

/**
 * Apply mode enumeration
 */
typedef enum {
    APPLY_MODE_UNKNOWN = 0,
    APPLY_MODE_CREATE = 1,        // Object doesn't exist yet, create it
    APPLY_MODE_MERGE = 2,         // Object exists, merge new config
    APPLY_MODE_CONFLICT = 3,      // Object has been modified outside of kubectl
} kubectl_apply_mode_t;

/**
 * Result of applying a configuration
 */
typedef struct {
    int success;                   // 1 if successful, 0 if conflict/error
    int response_code;             // HTTP status code to return (200, 201, 409, 422, etc)
    json_object* result_obj;       // The resulting object after apply
    char error_message[512];       // Error description if failed
} kubectl_apply_result_t;

/**
 * Determine the apply mode based on last-applied config and current state
 * 
 * @param current_obj - Current object from etcd (or NULL if new)
 * @param desired_obj - Desired object from kubectl apply request
 * 
 * @return Apply mode (CREATE, MERGE, or CONFLICT)
 */
kubectl_apply_mode_t kubectl_apply_determine_mode(json_object* current_obj, 
                                                   json_object* desired_obj);

/**
 * Perform kubectl apply: 3-way merge of last-applied + current + desired
 * 
 * Implements the algorithm described in:
 * https://kubernetes.io/docs/tasks/manage-kubernetes-objects/declarative-config/
 *
 * Three-way merge algorithm:
 * 1. If field exists only in desired → use desired value
 * 2. If field exists only in last-applied → delete from current (kubectl intentionally removed it)
 * 3. If field exists in both current and desired, but different from last-applied → conflict (user modified field)
 * 4. If field exists in both current and desired with same value → keep it
 * 5. If field was removed from desired but exists in current → delete it (kubectl removed it)
 *
 * @param current_obj - Current object from etcd (NULL if new)
 * @param desired_obj - Desired object from kubectl apply
 * @param namespace - Kubernetes namespace
 * @param resource_name - Resource name (pod, service, etc.)
 * 
 * @return Apply result with merged object or conflict details
 */
kubectl_apply_result_t* kubectl_apply_three_way_merge(json_object* current_obj,
                                                       json_object* desired_obj,
                                                       const char* namespace,
                                                       const char* resource_name);

/**
 * Extract last-applied configuration from object metadata
 * 
 * @param obj - Kubernetes object
 * @return JSON object containing last-applied config, or NULL if not found
 */
json_object* kubectl_apply_get_last_applied(json_object* obj);

/**
 * Store last-applied configuration in object metadata
 * This annotation is used by kubectl apply to do 3-way merges on subsequent applies
 * 
 * @param obj - Kubernetes object to update
 * @param desired_json - The applied configuration as JSON string
 */
void kubectl_apply_set_last_applied(json_object* obj, const char* desired_json);

/**
 * Check if object has been modified outside of kubectl apply
 * 
 * Detects when a user/controller has modified object fields that kubectl doesn't own.
 * This is used to warn about or prevent destructive updates.
 *
 * @param current_obj - Current object from etcd
 * @param desired_obj - Desired object from kubectl apply
 * @return 1 if conflict detected, 0 if no conflict
 */
int kubectl_apply_detect_field_conflict(json_object* current_obj, json_object* desired_obj);

/**
 * Free kubectl apply result
 */
void kubectl_apply_result_free(kubectl_apply_result_t* result);

#endif // SIRAH_KUBECTL_APPLY_H
