#ifndef K8S_QUOTA_CONTROLLER_H
#define K8S_QUOTA_CONTROLLER_H

#include "../../pkg/types/quota.h"
#include <stdbool.h>

typedef struct {
    int max_quotas;
    int max_limit_ranges;
    
    // Quota storage
    k8s_resource_quota_t** quotas;
    int num_quotas;
    
    // LimitRange storage
    k8s_limit_range_t** limit_ranges;
    int num_limit_ranges;
    
    bool enabled;
} k8s_quota_controller_t;

// Lifecycle
k8s_quota_controller_t* k8s_quota_controller_new(int max_quotas, int max_limit_ranges);
void k8s_quota_controller_free(k8s_quota_controller_t* controller);

// ============ ResourceQuota CRUD ============

int k8s_quota_controller_create_quota(k8s_quota_controller_t* controller, k8s_resource_quota_t* quota);
k8s_resource_quota_t* k8s_quota_controller_get_quota(k8s_quota_controller_t* controller, const char* name, const char* namespace);
int k8s_quota_controller_update_quota(k8s_quota_controller_t* controller, k8s_resource_quota_t* quota);
int k8s_quota_controller_delete_quota(k8s_quota_controller_t* controller, const char* name, const char* namespace);
k8s_resource_quota_t** k8s_quota_controller_list_quotas(k8s_quota_controller_t* controller, const char* namespace, int* count);

// ============ LimitRange CRUD ============

int k8s_quota_controller_create_limit_range(k8s_quota_controller_t* controller, k8s_limit_range_t* limit_range);
k8s_limit_range_t* k8s_quota_controller_get_limit_range(k8s_quota_controller_t* controller, const char* name, const char* namespace);
int k8s_quota_controller_update_limit_range(k8s_quota_controller_t* controller, k8s_limit_range_t* limit_range);
int k8s_quota_controller_delete_limit_range(k8s_quota_controller_t* controller, const char* name, const char* namespace);
k8s_limit_range_t** k8s_quota_controller_list_limit_ranges(k8s_quota_controller_t* controller, const char* namespace, int* count);

// ============ Admission Control ============

// Check if resource can be admitted under quotas
bool k8s_quota_controller_can_admit_resource(
    k8s_quota_controller_t* controller,
    const char* namespace,
    k8s_resource_quantity_t* requested
);

// Track resource usage (call when pod created)
int k8s_quota_controller_record_resource_usage(
    k8s_quota_controller_t* controller,
    const char* namespace,
    k8s_resource_quantity_t* used
);

// Release resource usage (call when pod deleted)
int k8s_quota_controller_release_resource_usage(
    k8s_quota_controller_t* controller,
    const char* namespace,
    k8s_resource_quantity_t* used
);

// ============ LimitRange Defaults ============

// Apply LimitRange defaults to container resources
int k8s_quota_controller_apply_limit_range_defaults(
    k8s_quota_controller_t* controller,
    const char* namespace,
    k8s_resource_quantity_t* container_resources
);

// Validate container against LimitRange
bool k8s_quota_controller_validate_container_limits(
    k8s_quota_controller_t* controller,
    const char* namespace,
    k8s_resource_quantity_t* container_resources
);

// Get quota usage percentage
int k8s_quota_controller_get_usage_percent(
    k8s_quota_controller_t* controller,
    const char* namespace,
    const char* resource,
    int* percent
);

// Control
void k8s_quota_controller_set_enabled(k8s_quota_controller_t* controller, bool enabled);
bool k8s_quota_controller_is_enabled(k8s_quota_controller_t* controller);

// Global instance
k8s_quota_controller_t* k8s_quota_controller_global();

#endif
