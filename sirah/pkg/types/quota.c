#include "quota.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdbool.h>

// ============ Resource Quantity ============

k8s_resource_quantity_t* k8s_resource_quantity_new() {
    k8s_resource_quantity_t* qty = (k8s_resource_quantity_t*)malloc(sizeof(k8s_resource_quantity_t));
    if (!qty) return NULL;
    
    qty->cpu_millicores = 0;
    qty->memory_bytes = 0;
    qty->storage_bytes = 0;
    qty->pods_count = 0;
    qty->services_count = 0;
    qty->deployments_count = 0;
    
    return qty;
}

void k8s_resource_quantity_free(k8s_resource_quantity_t* qty) {
    free(qty);
}

int k8s_resource_quantity_add(k8s_resource_quantity_t* total, k8s_resource_quantity_t* qty) {
    if (!total || !qty) return -1;
    
    total->cpu_millicores += qty->cpu_millicores;
    total->memory_bytes += qty->memory_bytes;
    total->storage_bytes += qty->storage_bytes;
    total->pods_count += qty->pods_count;
    total->services_count += qty->services_count;
    total->deployments_count += qty->deployments_count;
    
    return 0;
}

int k8s_resource_quantity_subtract(k8s_resource_quantity_t* total, k8s_resource_quantity_t* qty) {
    if (!total || !qty) return -1;
    
    total->cpu_millicores -= qty->cpu_millicores;
    total->memory_bytes -= qty->memory_bytes;
    total->storage_bytes -= qty->storage_bytes;
    total->pods_count -= qty->pods_count;
    total->services_count -= qty->services_count;
    total->deployments_count -= qty->deployments_count;
    
    // Clamp to zero
    if (total->cpu_millicores < 0) total->cpu_millicores = 0;
    if (total->memory_bytes < 0) total->memory_bytes = 0;
    if (total->storage_bytes < 0) total->storage_bytes = 0;
    if (total->pods_count < 0) total->pods_count = 0;
    if (total->services_count < 0) total->services_count = 0;
    if (total->deployments_count < 0) total->deployments_count = 0;
    
    return 0;
}

bool k8s_resource_quantity_exceeds(k8s_resource_quantity_t* used, k8s_resource_quantity_t* limit) {
    if (!used || !limit) return false;
    
    if (used->cpu_millicores > limit->cpu_millicores) return true;
    if (used->memory_bytes > limit->memory_bytes) return true;
    if (used->storage_bytes > limit->storage_bytes) return true;
    if (used->pods_count > limit->pods_count) return true;
    if (used->services_count > limit->services_count) return true;
    if (used->deployments_count > limit->deployments_count) return true;
    
    return false;
}

// ============ ResourceQuota Specification ============

k8s_quota_spec_t* k8s_quota_spec_new() {
    k8s_quota_spec_t* spec = (k8s_quota_spec_t*)malloc(sizeof(k8s_quota_spec_t));
    if (!spec) return NULL;
    
    spec->hard_limits.cpu_millicores = 0;
    spec->hard_limits.memory_bytes = 0;
    spec->hard_limits.storage_bytes = 0;
    spec->hard_limits.pods_count = 0;
    spec->hard_limits.services_count = 0;
    spec->hard_limits.deployments_count = 0;
    
    spec->soft_limits = spec->hard_limits;
    spec->scope_names = NULL;
    spec->num_scopes = 0;
    
    return spec;
}

void k8s_quota_spec_free(k8s_quota_spec_t* spec) {
    if (!spec) return;
    
    if (spec->scope_names) {
        for (int i = 0; i < spec->num_scopes; i++) {
            free(spec->scope_names[i]);
        }
        free(spec->scope_names);
    }
    
    free(spec);
}

// ============ ResourceQuota Status ============

k8s_quota_status_t* k8s_quota_status_new() {
    k8s_quota_status_t* status = (k8s_quota_status_t*)malloc(sizeof(k8s_quota_status_t));
    if (!status) return NULL;
    
    status->used.cpu_millicores = 0;
    status->used.memory_bytes = 0;
    status->used.storage_bytes = 0;
    status->used.pods_count = 0;
    status->used.services_count = 0;
    status->used.deployments_count = 0;
    
    status->hard = status->used;
    status->soft = status->used;
    status->last_update_time = time(NULL);
    
    return status;
}

void k8s_quota_status_free(k8s_quota_status_t* status) {
    free(status);
}

// ============ ResourceQuota Lifecycle ============

k8s_resource_quota_t* k8s_resource_quota_new(const char* name, const char* namespace) {
    if (!name || !namespace) return NULL;
    
    k8s_resource_quota_t* quota = (k8s_resource_quota_t*)malloc(sizeof(k8s_resource_quota_t));
    if (!quota) return NULL;
    
    quota->metadata.name = (char*)malloc(strlen(name) + 1);
    if (!quota->metadata.name) { free(quota); return NULL; }
    strcpy(quota->metadata.name, name);
    
    quota->metadata.namespace = (char*)malloc(strlen(namespace) + 1);
    if (!quota->metadata.namespace) { free(quota->metadata.name); free(quota); return NULL; }
    strcpy(quota->metadata.namespace, namespace);
    
    quota->metadata.uid = (char*)malloc(37);
    if (!quota->metadata.uid) { free(quota->metadata.namespace); free(quota->metadata.name); free(quota); return NULL; }
    sprintf(quota->metadata.uid, "quota-%ld", time(NULL));
    
    quota->metadata.labels = NULL;
    quota->metadata.num_labels = 0;
    quota->metadata.creation_timestamp = time(NULL);
    
    quota->spec = k8s_quota_spec_new();
    if (!quota->spec) { free(quota->metadata.uid); free(quota->metadata.namespace); free(quota->metadata.name); free(quota); return NULL; }
    
    quota->status = k8s_quota_status_new();
    if (!quota->status) { k8s_quota_spec_free(quota->spec); free(quota->metadata.uid); free(quota->metadata.namespace); free(quota->metadata.name); free(quota); return NULL; }
    
    return quota;
}

void k8s_resource_quota_free(k8s_resource_quota_t* quota) {
    if (!quota) return;
    
    free(quota->metadata.name);
    free(quota->metadata.namespace);
    free(quota->metadata.uid);
    
    if (quota->metadata.labels) {
        for (int i = 0; i < quota->metadata.num_labels; i++) {
            free(quota->metadata.labels[i]);
        }
        free(quota->metadata.labels);
    }
    
    k8s_quota_spec_free(quota->spec);
    k8s_quota_status_free(quota->status);
    
    free(quota);
}

// ============ ResourceQuota Configuration ============

int k8s_resource_quota_set_hard_limit(k8s_resource_quota_t* quota, const char* resource, long long value) {
    if (!quota || !resource || value < 0) return -1;
    if (!quota->spec) return -1;
    
    if (strcmp(resource, "cpu") == 0) {
        quota->spec->hard_limits.cpu_millicores = value;
    } else if (strcmp(resource, "memory") == 0) {
        quota->spec->hard_limits.memory_bytes = value;
    } else if (strcmp(resource, "storage") == 0) {
        quota->spec->hard_limits.storage_bytes = value;
    } else if (strcmp(resource, "pods") == 0) {
        quota->spec->hard_limits.pods_count = (int)value;
    } else if (strcmp(resource, "services") == 0) {
        quota->spec->hard_limits.services_count = (int)value;
    } else if (strcmp(resource, "deployments") == 0) {
        quota->spec->hard_limits.deployments_count = (int)value;
    } else {
        return -1;
    }
    
    return 0;
}

int k8s_resource_quota_set_soft_limit(k8s_resource_quota_t* quota, const char* resource, long long value) {
    if (!quota || !resource || value < 0) return -1;
    if (!quota->spec) return -1;
    
    if (strcmp(resource, "cpu") == 0) {
        quota->spec->soft_limits.cpu_millicores = value;
    } else if (strcmp(resource, "memory") == 0) {
        quota->spec->soft_limits.memory_bytes = value;
    } else if (strcmp(resource, "storage") == 0) {
        quota->spec->soft_limits.storage_bytes = value;
    } else if (strcmp(resource, "pods") == 0) {
        quota->spec->soft_limits.pods_count = (int)value;
    } else if (strcmp(resource, "services") == 0) {
        quota->spec->soft_limits.services_count = (int)value;
    } else if (strcmp(resource, "deployments") == 0) {
        quota->spec->soft_limits.deployments_count = (int)value;
    } else {
        return -1;
    }
    
    return 0;
}

int k8s_resource_quota_add_scope(k8s_resource_quota_t* quota, const char* scope) {
    if (!quota || !scope) return -1;
    if (!quota->spec || quota->spec->num_scopes >= 10) return -1;
    
    char** new_scopes = (char**)realloc(quota->spec->scope_names, (quota->spec->num_scopes + 1) * sizeof(char*));
    if (!new_scopes) return -1;
    
    quota->spec->scope_names = new_scopes;
    quota->spec->scope_names[quota->spec->num_scopes] = (char*)malloc(strlen(scope) + 1);
    if (!quota->spec->scope_names[quota->spec->num_scopes]) return -1;
    
    strcpy(quota->spec->scope_names[quota->spec->num_scopes], scope);
    quota->spec->num_scopes++;
    
    return 0;
}

// ============ ResourceQuota Status Tracking ============

int k8s_resource_quota_update_used(k8s_resource_quota_t* quota, k8s_resource_quantity_t* used) {
    if (!quota || !used || !quota->status) return -1;
    
    quota->status->used = *used;
    quota->status->hard = quota->spec->hard_limits;
    quota->status->soft = quota->spec->soft_limits;
    quota->status->last_update_time = time(NULL);
    
    return 0;
}

int k8s_resource_quota_add_usage(k8s_resource_quota_t* quota, k8s_resource_quantity_t* qty) {
    if (!quota || !qty || !quota->status) return -1;
    
    return k8s_resource_quantity_add(&quota->status->used, qty);
}

int k8s_resource_quota_subtract_usage(k8s_resource_quota_t* quota, k8s_resource_quantity_t* qty) {
    if (!quota || !qty || !quota->status) return -1;
    
    return k8s_resource_quantity_subtract(&quota->status->used, qty);
}

// ============ ResourceQuota Validation ============

bool k8s_resource_quota_can_admit(k8s_resource_quota_t* quota, k8s_resource_quantity_t* requested) {
    if (!quota || !requested || !quota->spec || !quota->status) return false;
    
    // Check if adding requested would exceed hard limits
    k8s_resource_quantity_t projected = quota->status->used;
    k8s_resource_quantity_add(&projected, requested);
    
    return !k8s_resource_quantity_exceeds(&projected, &quota->spec->hard_limits);
}

// ============ ResourceQuota Serialization ============

char* k8s_resource_quota_to_json(k8s_resource_quota_t* quota) {
    if (!quota) return NULL;
    
    char* json = (char*)malloc(2048);
    if (!json) return NULL;
    
    sprintf(json,
        "{\"kind\":\"ResourceQuota\",\"metadata\":{\"name\":\"%s\",\"namespace\":\"%s\"},\"spec\":{\"hard\":{\"cpu\":\"%lld\",\"memory\":\"%lld\",\"pods\":%d}}}",
        quota->metadata.name, quota->metadata.namespace,
        quota->spec->hard_limits.cpu_millicores,
        quota->spec->hard_limits.memory_bytes,
        quota->spec->hard_limits.pods_count
    );
    
    return json;
}

// ============ LimitRange Specification ============

k8s_limit_range_spec_t* k8s_limit_range_spec_new() {
    k8s_limit_range_spec_t* spec = (k8s_limit_range_spec_t*)malloc(sizeof(k8s_limit_range_spec_t));
    if (!spec) return NULL;
    
    spec->container_min.cpu_millicores = 100;      // 100m default
    spec->container_min.memory_bytes = 128 * 1024; // 128Mi default
    
    spec->container_max.cpu_millicores = 4000;              // 4 CPU default
    spec->container_max.memory_bytes = 4LL * 1024 * 1024 * 1024;  // 4Gi default
    
    spec->container_default_request.cpu_millicores = 500;
    spec->container_default_request.memory_bytes = 512 * 1024;  // 512Mi
    
    spec->container_default_limit.cpu_millicores = 1000;
    spec->container_default_limit.memory_bytes = 1024 * 1024 * 1024;  // 1Gi
    
    spec->pod_min = spec->container_min;
    spec->pod_max = spec->container_max;
    
    return spec;
}

void k8s_limit_range_spec_free(k8s_limit_range_spec_t* spec) {
    free(spec);
}

// ============ LimitRange Lifecycle ============

k8s_limit_range_t* k8s_limit_range_new(const char* name, const char* namespace) {
    if (!name || !namespace) return NULL;
    
    k8s_limit_range_t* lr = (k8s_limit_range_t*)malloc(sizeof(k8s_limit_range_t));
    if (!lr) return NULL;
    
    lr->metadata.name = (char*)malloc(strlen(name) + 1);
    if (!lr->metadata.name) { free(lr); return NULL; }
    strcpy(lr->metadata.name, name);
    
    lr->metadata.namespace = (char*)malloc(strlen(namespace) + 1);
    if (!lr->metadata.namespace) { free(lr->metadata.name); free(lr); return NULL; }
    strcpy(lr->metadata.namespace, namespace);
    
    lr->metadata.uid = (char*)malloc(37);
    if (!lr->metadata.uid) { free(lr->metadata.namespace); free(lr->metadata.name); free(lr); return NULL; }
    sprintf(lr->metadata.uid, "lr-%ld", time(NULL));
    
    lr->metadata.creation_timestamp = time(NULL);
    
    lr->spec = k8s_limit_range_spec_new();
    if (!lr->spec) { free(lr->metadata.uid); free(lr->metadata.namespace); free(lr->metadata.name); free(lr); return NULL; }
    
    return lr;
}

void k8s_limit_range_free(k8s_limit_range_t* limit_range) {
    if (!limit_range) return;
    
    free(limit_range->metadata.name);
    free(limit_range->metadata.namespace);
    free(limit_range->metadata.uid);
    
    k8s_limit_range_spec_free(limit_range->spec);
    free(limit_range);
}

// ============ LimitRange Configuration ============

int k8s_limit_range_set_container_limits(k8s_limit_range_t* lr, k8s_resource_quantity_t* min, k8s_resource_quantity_t* max) {
    if (!lr || !min || !max || !lr->spec) return -1;
    
    lr->spec->container_min = *min;
    lr->spec->container_max = *max;
    
    return 0;
}

int k8s_limit_range_set_container_defaults(k8s_limit_range_t* lr, k8s_resource_quantity_t* request, k8s_resource_quantity_t* limit) {
    if (!lr || !request || !limit || !lr->spec) return -1;
    
    lr->spec->container_default_request = *request;
    lr->spec->container_default_limit = *limit;
    
    return 0;
}

int k8s_limit_range_set_pod_limits(k8s_limit_range_t* lr, k8s_resource_quantity_t* min, k8s_resource_quantity_t* max) {
    if (!lr || !min || !max || !lr->spec) return -1;
    
    lr->spec->pod_min = *min;
    lr->spec->pod_max = *max;
    
    return 0;
}

// ============ LimitRange Validation ============

bool k8s_limit_range_validate_container(k8s_limit_range_t* lr, k8s_resource_quantity_t* resources) {
    if (!lr || !resources || !lr->spec) return false;
    
    // Check if resources are within limits
    if (resources->cpu_millicores < lr->spec->container_min.cpu_millicores) return false;
    if (resources->cpu_millicores > lr->spec->container_max.cpu_millicores) return false;
    
    if (resources->memory_bytes < lr->spec->container_min.memory_bytes) return false;
    if (resources->memory_bytes > lr->spec->container_max.memory_bytes) return false;
    
    return true;
}

bool k8s_limit_range_validate_pod(k8s_limit_range_t* lr, k8s_resource_quantity_t* resources) {
    if (!lr || !resources || !lr->spec) return false;
    
    if (resources->cpu_millicores < lr->spec->pod_min.cpu_millicores) return false;
    if (resources->cpu_millicores > lr->spec->pod_max.cpu_millicores) return false;
    
    if (resources->memory_bytes < lr->spec->pod_min.memory_bytes) return false;
    if (resources->memory_bytes > lr->spec->pod_max.memory_bytes) return false;
    
    return true;
}

// ============ LimitRange Apply Defaults ============

int k8s_limit_range_apply_defaults(k8s_limit_range_t* lr, k8s_resource_quantity_t* container_resources) {
    if (!lr || !container_resources || !lr->spec) return -1;
    
    // Apply defaults if not specified
    if (container_resources->cpu_millicores == 0) {
        container_resources->cpu_millicores = lr->spec->container_default_request.cpu_millicores;
    }
    if (container_resources->memory_bytes == 0) {
        container_resources->memory_bytes = lr->spec->container_default_request.memory_bytes;
    }
    
    // Validate against limits
    if (!k8s_limit_range_validate_container(lr, container_resources)) {
        return -1;
    }
    
    return 0;
}
