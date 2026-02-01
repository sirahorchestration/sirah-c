#ifndef K8S_QUOTA_H
#define K8S_QUOTA_H

#include <stdbool.h>
#include <time.h>

// Resource quantities
typedef struct {
    long long cpu_millicores;
    long long memory_bytes;
    long long storage_bytes;
    int pods_count;
    int services_count;
    int deployments_count;
} k8s_resource_quantity_t;

// ResourceQuota specification
typedef struct {
    k8s_resource_quantity_t hard_limits;
    k8s_resource_quantity_t soft_limits;
    
    // Scope selectors
    char** scope_names;
    int num_scopes;
} k8s_quota_spec_t;

// ResourceQuota status
typedef struct {
    k8s_resource_quantity_t used;
    k8s_resource_quantity_t hard;
    k8s_resource_quantity_t soft;
    
    time_t last_update_time;
} k8s_quota_status_t;

// Full ResourceQuota object
typedef struct {
    struct {
        char* name;
        char* namespace;
        char* uid;
        char** labels;
        int num_labels;
        time_t creation_timestamp;
    } metadata;
    
    k8s_quota_spec_t* spec;
    k8s_quota_status_t* status;
} k8s_resource_quota_t;

// LimitRange specification
typedef struct {
    // Per-container limits
    k8s_resource_quantity_t container_min;
    k8s_resource_quantity_t container_max;
    k8s_resource_quantity_t container_default_request;
    k8s_resource_quantity_t container_default_limit;
    
    // Per-pod limits
    k8s_resource_quantity_t pod_min;
    k8s_resource_quantity_t pod_max;
} k8s_limit_range_spec_t;

// Full LimitRange object
typedef struct {
    struct {
        char* name;
        char* namespace;
        char* uid;
        time_t creation_timestamp;
    } metadata;
    
    k8s_limit_range_spec_t* spec;
} k8s_limit_range_t;

// ============ Resource Quantity ============

k8s_resource_quantity_t* k8s_resource_quantity_new();
void k8s_resource_quantity_free(k8s_resource_quantity_t* qty);
int k8s_resource_quantity_add(k8s_resource_quantity_t* total, k8s_resource_quantity_t* qty);
int k8s_resource_quantity_subtract(k8s_resource_quantity_t* total, k8s_resource_quantity_t* qty);
bool k8s_resource_quantity_exceeds(k8s_resource_quantity_t* used, k8s_resource_quantity_t* limit);

// ============ ResourceQuota Functions ============

k8s_quota_spec_t* k8s_quota_spec_new();
void k8s_quota_spec_free(k8s_quota_spec_t* spec);

k8s_quota_status_t* k8s_quota_status_new();
void k8s_quota_status_free(k8s_quota_status_t* status);

k8s_resource_quota_t* k8s_resource_quota_new(const char* name, const char* namespace);
void k8s_resource_quota_free(k8s_resource_quota_t* quota);

// Configuration
int k8s_resource_quota_set_hard_limit(k8s_resource_quota_t* quota, const char* resource, long long value);
int k8s_resource_quota_set_soft_limit(k8s_resource_quota_t* quota, const char* resource, long long value);
int k8s_resource_quota_add_scope(k8s_resource_quota_t* quota, const char* scope);

// Status tracking
int k8s_resource_quota_update_used(k8s_resource_quota_t* quota, k8s_resource_quantity_t* used);
int k8s_resource_quota_add_usage(k8s_resource_quota_t* quota, k8s_resource_quantity_t* qty);
int k8s_resource_quota_subtract_usage(k8s_resource_quota_t* quota, k8s_resource_quantity_t* qty);

// Validation
bool k8s_resource_quota_can_admit(k8s_resource_quota_t* quota, k8s_resource_quantity_t* requested);

// Serialization
char* k8s_resource_quota_to_json(k8s_resource_quota_t* quota);

// ============ LimitRange Functions ============

k8s_limit_range_spec_t* k8s_limit_range_spec_new();
void k8s_limit_range_spec_free(k8s_limit_range_spec_t* spec);

k8s_limit_range_t* k8s_limit_range_new(const char* name, const char* namespace);
void k8s_limit_range_free(k8s_limit_range_t* limit_range);

// Configuration
int k8s_limit_range_set_container_limits(k8s_limit_range_t* lr, k8s_resource_quantity_t* min, k8s_resource_quantity_t* max);
int k8s_limit_range_set_container_defaults(k8s_limit_range_t* lr, k8s_resource_quantity_t* request, k8s_resource_quantity_t* limit);
int k8s_limit_range_set_pod_limits(k8s_limit_range_t* lr, k8s_resource_quantity_t* min, k8s_resource_quantity_t* max);

// Validation
bool k8s_limit_range_validate_container(k8s_limit_range_t* lr, k8s_resource_quantity_t* resources);
bool k8s_limit_range_validate_pod(k8s_limit_range_t* lr, k8s_resource_quantity_t* resources);

// Apply defaults
int k8s_limit_range_apply_defaults(k8s_limit_range_t* lr, k8s_resource_quantity_t* container_resources);

#endif
