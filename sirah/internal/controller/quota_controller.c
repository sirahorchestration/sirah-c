#include "quota_controller.h"
#include <stdlib.h>
#include <string.h>

static k8s_quota_controller_t* g_quota_controller = NULL;

// ============ Lifecycle ============

k8s_quota_controller_t* k8s_quota_controller_new(int max_quotas, int max_limit_ranges) {
    if (max_quotas <= 0 || max_limit_ranges <= 0) return NULL;
    
    k8s_quota_controller_t* controller = (k8s_quota_controller_t*)malloc(sizeof(k8s_quota_controller_t));
    if (!controller) return NULL;
    
    controller->quotas = (k8s_resource_quota_t**)malloc(max_quotas * sizeof(k8s_resource_quota_t*));
    if (!controller->quotas) { free(controller); return NULL; }
    
    controller->limit_ranges = (k8s_limit_range_t**)malloc(max_limit_ranges * sizeof(k8s_limit_range_t*));
    if (!controller->limit_ranges) { free(controller->quotas); free(controller); return NULL; }
    
    controller->max_quotas = max_quotas;
    controller->max_limit_ranges = max_limit_ranges;
    controller->num_quotas = 0;
    controller->num_limit_ranges = 0;
    controller->enabled = true;
    
    return controller;
}

void k8s_quota_controller_free(k8s_quota_controller_t* controller) {
    if (!controller) return;
    
    if (controller->quotas) {
        for (int i = 0; i < controller->num_quotas; i++) {
            k8s_resource_quota_free(controller->quotas[i]);
        }
        free(controller->quotas);
    }
    
    if (controller->limit_ranges) {
        for (int i = 0; i < controller->num_limit_ranges; i++) {
            k8s_limit_range_free(controller->limit_ranges[i]);
        }
        free(controller->limit_ranges);
    }
    
    free(controller);
}

// ============ ResourceQuota CRUD ============

int k8s_quota_controller_create_quota(k8s_quota_controller_t* controller, k8s_resource_quota_t* quota) {
    if (!controller || !quota || controller->num_quotas >= controller->max_quotas) return -1;
    
    // Check for duplicates
    for (int i = 0; i < controller->num_quotas; i++) {
        if (strcmp(controller->quotas[i]->metadata.name, quota->metadata.name) == 0 &&
            strcmp(controller->quotas[i]->metadata.namespace, quota->metadata.namespace) == 0) {
            return -1;
        }
    }
    
    controller->quotas[controller->num_quotas] = quota;
    controller->num_quotas++;
    
    return 0;
}

k8s_resource_quota_t* k8s_quota_controller_get_quota(k8s_quota_controller_t* controller, const char* name, const char* namespace) {
    if (!controller || !name || !namespace) return NULL;
    
    for (int i = 0; i < controller->num_quotas; i++) {
        if (strcmp(controller->quotas[i]->metadata.name, name) == 0 &&
            strcmp(controller->quotas[i]->metadata.namespace, namespace) == 0) {
            return controller->quotas[i];
        }
    }
    
    return NULL;
}

int k8s_quota_controller_update_quota(k8s_quota_controller_t* controller, k8s_resource_quota_t* quota) {
    if (!controller || !quota) return -1;
    
    for (int i = 0; i < controller->num_quotas; i++) {
        if (strcmp(controller->quotas[i]->metadata.name, quota->metadata.name) == 0 &&
            strcmp(controller->quotas[i]->metadata.namespace, quota->metadata.namespace) == 0) {
            k8s_resource_quota_free(controller->quotas[i]);
            controller->quotas[i] = quota;
            return 0;
        }
    }
    
    return -1;
}

int k8s_quota_controller_delete_quota(k8s_quota_controller_t* controller, const char* name, const char* namespace) {
    if (!controller || !name || !namespace) return -1;
    
    for (int i = 0; i < controller->num_quotas; i++) {
        if (strcmp(controller->quotas[i]->metadata.name, name) == 0 &&
            strcmp(controller->quotas[i]->metadata.namespace, namespace) == 0) {
            
            k8s_resource_quota_free(controller->quotas[i]);
            
            for (int j = i; j < controller->num_quotas - 1; j++) {
                controller->quotas[j] = controller->quotas[j + 1];
            }
            controller->num_quotas--;
            return 0;
        }
    }
    
    return -1;
}

k8s_resource_quota_t** k8s_quota_controller_list_quotas(k8s_quota_controller_t* controller, const char* namespace, int* count) {
    if (!controller || !namespace || !count) return NULL;
    
    int ns_count = 0;
    for (int i = 0; i < controller->num_quotas; i++) {
        if (strcmp(controller->quotas[i]->metadata.namespace, namespace) == 0) {
            ns_count++;
        }
    }
    
    if (ns_count == 0) {
        *count = 0;
        return NULL;
    }
    
    k8s_resource_quota_t** result = (k8s_resource_quota_t**)malloc(ns_count * sizeof(k8s_resource_quota_t*));
    if (!result) return NULL;
    
    int idx = 0;
    for (int i = 0; i < controller->num_quotas; i++) {
        if (strcmp(controller->quotas[i]->metadata.namespace, namespace) == 0) {
            result[idx++] = controller->quotas[i];
        }
    }
    
    *count = ns_count;
    return result;
}

// ============ LimitRange CRUD ============

int k8s_quota_controller_create_limit_range(k8s_quota_controller_t* controller, k8s_limit_range_t* limit_range) {
    if (!controller || !limit_range || controller->num_limit_ranges >= controller->max_limit_ranges) return -1;
    
    // Check for duplicates
    for (int i = 0; i < controller->num_limit_ranges; i++) {
        if (strcmp(controller->limit_ranges[i]->metadata.name, limit_range->metadata.name) == 0 &&
            strcmp(controller->limit_ranges[i]->metadata.namespace, limit_range->metadata.namespace) == 0) {
            return -1;
        }
    }
    
    controller->limit_ranges[controller->num_limit_ranges] = limit_range;
    controller->num_limit_ranges++;
    
    return 0;
}

k8s_limit_range_t* k8s_quota_controller_get_limit_range(k8s_quota_controller_t* controller, const char* name, const char* namespace) {
    if (!controller || !name || !namespace) return NULL;
    
    for (int i = 0; i < controller->num_limit_ranges; i++) {
        if (strcmp(controller->limit_ranges[i]->metadata.name, name) == 0 &&
            strcmp(controller->limit_ranges[i]->metadata.namespace, namespace) == 0) {
            return controller->limit_ranges[i];
        }
    }
    
    return NULL;
}

int k8s_quota_controller_update_limit_range(k8s_quota_controller_t* controller, k8s_limit_range_t* limit_range) {
    if (!controller || !limit_range) return -1;
    
    for (int i = 0; i < controller->num_limit_ranges; i++) {
        if (strcmp(controller->limit_ranges[i]->metadata.name, limit_range->metadata.name) == 0 &&
            strcmp(controller->limit_ranges[i]->metadata.namespace, limit_range->metadata.namespace) == 0) {
            k8s_limit_range_free(controller->limit_ranges[i]);
            controller->limit_ranges[i] = limit_range;
            return 0;
        }
    }
    
    return -1;
}

int k8s_quota_controller_delete_limit_range(k8s_quota_controller_t* controller, const char* name, const char* namespace) {
    if (!controller || !name || !namespace) return -1;
    
    for (int i = 0; i < controller->num_limit_ranges; i++) {
        if (strcmp(controller->limit_ranges[i]->metadata.name, name) == 0 &&
            strcmp(controller->limit_ranges[i]->metadata.namespace, namespace) == 0) {
            
            k8s_limit_range_free(controller->limit_ranges[i]);
            
            for (int j = i; j < controller->num_limit_ranges - 1; j++) {
                controller->limit_ranges[j] = controller->limit_ranges[j + 1];
            }
            controller->num_limit_ranges--;
            return 0;
        }
    }
    
    return -1;
}

k8s_limit_range_t** k8s_quota_controller_list_limit_ranges(k8s_quota_controller_t* controller, const char* namespace, int* count) {
    if (!controller || !namespace || !count) return NULL;
    
    int ns_count = 0;
    for (int i = 0; i < controller->num_limit_ranges; i++) {
        if (strcmp(controller->limit_ranges[i]->metadata.namespace, namespace) == 0) {
            ns_count++;
        }
    }
    
    if (ns_count == 0) {
        *count = 0;
        return NULL;
    }
    
    k8s_limit_range_t** result = (k8s_limit_range_t**)malloc(ns_count * sizeof(k8s_limit_range_t*));
    if (!result) return NULL;
    
    int idx = 0;
    for (int i = 0; i < controller->num_limit_ranges; i++) {
        if (strcmp(controller->limit_ranges[i]->metadata.namespace, namespace) == 0) {
            result[idx++] = controller->limit_ranges[i];
        }
    }
    
    *count = ns_count;
    return result;
}

// ============ Admission Control ============

bool k8s_quota_controller_can_admit_resource(
    k8s_quota_controller_t* controller,
    const char* namespace,
    k8s_resource_quantity_t* requested) {
    
    if (!controller || !namespace || !requested || !controller->enabled) return true;  // Allow if disabled
    
    int count = 0;
    k8s_resource_quota_t** quotas = k8s_quota_controller_list_quotas(controller, namespace, &count);
    if (!quotas || count == 0) return true;  // No quotas = allow
    
    bool can_admit = true;
    for (int i = 0; i < count; i++) {
        if (!k8s_resource_quota_can_admit(quotas[i], requested)) {
            can_admit = false;
            break;
        }
    }
    
    free(quotas);
    return can_admit;
}

int k8s_quota_controller_record_resource_usage(
    k8s_quota_controller_t* controller,
    const char* namespace,
    k8s_resource_quantity_t* used) {
    
    if (!controller || !namespace || !used) return -1;
    
    int count = 0;
    k8s_resource_quota_t** quotas = k8s_quota_controller_list_quotas(controller, namespace, &count);
    if (!quotas || count == 0) return 0;
    
    for (int i = 0; i < count; i++) {
        k8s_resource_quota_add_usage(quotas[i], used);
    }
    
    free(quotas);
    return 0;
}

int k8s_quota_controller_release_resource_usage(
    k8s_quota_controller_t* controller,
    const char* namespace,
    k8s_resource_quantity_t* used) {
    
    if (!controller || !namespace || !used) return -1;
    
    int count = 0;
    k8s_resource_quota_t** quotas = k8s_quota_controller_list_quotas(controller, namespace, &count);
    if (!quotas || count == 0) return 0;
    
    for (int i = 0; i < count; i++) {
        k8s_resource_quota_subtract_usage(quotas[i], used);
    }
    
    free(quotas);
    return 0;
}

// ============ LimitRange Defaults ============

int k8s_quota_controller_apply_limit_range_defaults(
    k8s_quota_controller_t* controller,
    const char* namespace,
    k8s_resource_quantity_t* container_resources) {
    
    if (!controller || !namespace || !container_resources) return -1;
    
    int count = 0;
    k8s_limit_range_t** limit_ranges = k8s_quota_controller_list_limit_ranges(controller, namespace, &count);
    if (!limit_ranges || count == 0) return 0;  // No limit ranges
    
    // Apply defaults from first limit range
    int result = k8s_limit_range_apply_defaults(limit_ranges[0], container_resources);
    
    free(limit_ranges);
    return result;
}

bool k8s_quota_controller_validate_container_limits(
    k8s_quota_controller_t* controller,
    const char* namespace,
    k8s_resource_quantity_t* container_resources) {
    
    if (!controller || !namespace || !container_resources) return false;
    
    int count = 0;
    k8s_limit_range_t** limit_ranges = k8s_quota_controller_list_limit_ranges(controller, namespace, &count);
    if (!limit_ranges || count == 0) return true;  // No limit ranges = valid
    
    bool valid = true;
    for (int i = 0; i < count; i++) {
        if (!k8s_limit_range_validate_container(limit_ranges[i], container_resources)) {
            valid = false;
            break;
        }
    }
    
    free(limit_ranges);
    return valid;
}

int k8s_quota_controller_get_usage_percent(
    k8s_quota_controller_t* controller,
    const char* namespace,
    const char* resource,
    int* percent) {
    
    if (!controller || !namespace || !resource || !percent) return -1;
    
    k8s_resource_quota_t* quota = NULL;
    
    // Find first quota in namespace
    for (int i = 0; i < controller->num_quotas; i++) {
        if (strcmp(controller->quotas[i]->metadata.namespace, namespace) == 0) {
            quota = controller->quotas[i];
            break;
        }
    }
    
    if (!quota) {
        *percent = 0;
        return 0;
    }
    
    // Calculate usage percentage for resource
    long long used = 0, limit = 0;
    
    if (strcmp(resource, "cpu") == 0) {
        used = quota->status->used.cpu_millicores;
        limit = quota->status->hard.cpu_millicores;
    } else if (strcmp(resource, "memory") == 0) {
        used = quota->status->used.memory_bytes;
        limit = quota->status->hard.memory_bytes;
    } else if (strcmp(resource, "pods") == 0) {
        used = quota->status->used.pods_count;
        limit = quota->status->hard.pods_count;
    } else {
        return -1;
    }
    
    if (limit <= 0) {
        *percent = 0;
        return 0;
    }
    
    *percent = (int)((used * 100) / limit);
    return 0;
}

// ============ Control ============

void k8s_quota_controller_set_enabled(k8s_quota_controller_t* controller, bool enabled) {
    if (!controller) return;
    controller->enabled = enabled;
}

bool k8s_quota_controller_is_enabled(k8s_quota_controller_t* controller) {
    if (!controller) return false;
    return controller->enabled;
}

// ============ Global Instance ============

k8s_quota_controller_t* k8s_quota_controller_global() {
    if (!g_quota_controller) {
        g_quota_controller = k8s_quota_controller_new(5000, 5000);  // 5000 quotas and limit ranges
    }
    return g_quota_controller;
}
