/**
 * ResourceQuota Manager Implementation
 * Namespace-scoped resource quota enforcement for Kubernetes v1.28
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include <pthread.h>
#include <time.h>
#include "resource_quota.h"

/* ============================================================================
   Global State
   ============================================================================ */

typedef struct {
    pthread_mutex_t lock;
    resource_quota_t quotas[MAX_RESOURCE_QUOTAS];
    int quota_count;
    int initialized;
} resource_quota_manager_t;

static resource_quota_manager_t g_quota_manager = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .quota_count = 0,
    .initialized = 0
};

/* ============================================================================
   Initialization & Shutdown
   ============================================================================ */

int resource_quota_manager_init(void) {
    pthread_mutex_lock(&g_quota_manager.lock);
    
    if (g_quota_manager.initialized) {
        pthread_mutex_unlock(&g_quota_manager.lock);
        return 0;
    }
    
    /* Initialize quota storage */
    memset(g_quota_manager.quotas, 0, sizeof(g_quota_manager.quotas));
    g_quota_manager.quota_count = 0;
    g_quota_manager.initialized = 1;
    
    pthread_mutex_unlock(&g_quota_manager.lock);
    return 0;
}

int resource_quota_manager_shutdown(void) {
    pthread_mutex_lock(&g_quota_manager.lock);
    
    /* Clear all quotas */
    memset(g_quota_manager.quotas, 0, sizeof(g_quota_manager.quotas));
    g_quota_manager.quota_count = 0;
    g_quota_manager.initialized = 0;
    
    pthread_mutex_unlock(&g_quota_manager.lock);
    return 0;
}

/* ============================================================================
   Helper Functions
   ============================================================================ */

/**
 * Find quota by namespace and name
 * Returns index in array or -1 if not found
 */
static int find_quota_index(const char* namespace, const char* name) {
    for (int i = 0; i < g_quota_manager.quota_count; i++) {
        if (strcmp(g_quota_manager.quotas[i].namespace, namespace) == 0 &&
            strcmp(g_quota_manager.quotas[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * Convert resource quantity string to long long
 * Handles units: m (milli), Mi (mebibytes), Gi (gibibytes)
 */
static long long parse_resource_quantity(const char* quantity_str) {
    if (!quantity_str) return 0;
    
    long long value = 0;
    char unit[32] = {0};
    
    /* Parse value and unit */
    if (sscanf(quantity_str, "%lld%31s", &value, unit) < 1) {
        return 0;
    }
    
    /* Convert to base units */
    if (strcmp(unit, "m") == 0) {
        return value;  /* Already in millicores */
    } else if (strcmp(unit, "Mi") == 0) {
        return value * 1048576;  /* Mebibytes to bytes */
    } else if (strcmp(unit, "Gi") == 0) {
        return value * 1073741824;  /* Gibibytes to bytes */
    } else if (strcmp(unit, "Ki") == 0) {
        return value * 1024;  /* Kibibytes to bytes */
    } else if (strcmp(unit, "") == 0 || strcmp(unit, "0") == 0) {
        return value;  /* No unit = base units */
    }
    
    return value;
}

/* ============================================================================
   CRUD Operations
   ============================================================================ */

int resource_quota_create(const resource_quota_t* quota) {
    if (!quota || !quota->namespace || !quota->name) {
        return -1;
    }
    
    pthread_mutex_lock(&g_quota_manager.lock);
    
    /* Check if quota already exists */
    if (find_quota_index(quota->namespace, quota->name) >= 0) {
        pthread_mutex_unlock(&g_quota_manager.lock);
        return -1;
    }
    
    /* Check storage limit */
    if (g_quota_manager.quota_count >= MAX_RESOURCE_QUOTAS) {
        pthread_mutex_unlock(&g_quota_manager.lock);
        return -1;
    }
    
    /* Add new quota */
    int idx = g_quota_manager.quota_count++;
    g_quota_manager.quotas[idx] = *quota;
    g_quota_manager.quotas[idx].created_at = time(NULL);
    g_quota_manager.quotas[idx].last_updated = time(NULL);
    
    /* Initialize usage to zero */
    memset(&g_quota_manager.quotas[idx].used, 0, sizeof(quota_usage_t));
    
    pthread_mutex_unlock(&g_quota_manager.lock);
    return 0;
}

int resource_quota_get(const char* namespace, const char* name,
                      resource_quota_t* quota_out) {
    if (!namespace || !name || !quota_out) {
        return -1;
    }
    
    pthread_mutex_lock(&g_quota_manager.lock);
    
    int idx = find_quota_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&g_quota_manager.lock);
        return -1;
    }
    
    *quota_out = g_quota_manager.quotas[idx];
    pthread_mutex_unlock(&g_quota_manager.lock);
    return 0;
}

int resource_quota_update(const resource_quota_t* quota) {
    if (!quota || !quota->namespace || !quota->name) {
        return -1;
    }
    
    pthread_mutex_lock(&g_quota_manager.lock);
    
    int idx = find_quota_index(quota->namespace, quota->name);
    if (idx < 0) {
        pthread_mutex_unlock(&g_quota_manager.lock);
        return -1;
    }
    
    /* Update hard limits only */
    g_quota_manager.quotas[idx].hard = quota->hard;
    g_quota_manager.quotas[idx].last_updated = time(NULL);
    
    pthread_mutex_unlock(&g_quota_manager.lock);
    return 0;
}

int resource_quota_delete(const char* namespace, const char* name) {
    if (!namespace || !name) {
        return -1;
    }
    
    pthread_mutex_lock(&g_quota_manager.lock);
    
    int idx = find_quota_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&g_quota_manager.lock);
        return -1;
    }
    
    /* Shift remaining quotas */
    for (int i = idx; i < g_quota_manager.quota_count - 1; i++) {
        g_quota_manager.quotas[i] = g_quota_manager.quotas[i + 1];
    }
    g_quota_manager.quota_count--;
    
    pthread_mutex_unlock(&g_quota_manager.lock);
    return 0;
}

int resource_quota_list(const char* namespace,
                       resource_quota_t* quotas_out,
                       int* count_out) {
    if (!namespace || !quotas_out || !count_out) {
        return -1;
    }
    
    pthread_mutex_lock(&g_quota_manager.lock);
    
    int count = 0;
    for (int i = 0; i < g_quota_manager.quota_count && count < 100; i++) {
        if (strcmp(g_quota_manager.quotas[i].namespace, namespace) == 0) {
            quotas_out[count++] = g_quota_manager.quotas[i];
        }
    }
    
    *count_out = count;
    pthread_mutex_unlock(&g_quota_manager.lock);
    return 0;
}

/* ============================================================================
   Usage Tracking
   ============================================================================ */

/**
 * Find first quota for namespace and update it
 */
static int update_quota_usage(const char* namespace,
                             int (*update_func)(quota_usage_t*)) {
    pthread_mutex_lock(&g_quota_manager.lock);
    
    int found = 0;
    for (int i = 0; i < g_quota_manager.quota_count; i++) {
        if (strcmp(g_quota_manager.quotas[i].namespace, namespace) == 0) {
            if (update_func(&g_quota_manager.quotas[i].used) == 0) {
                g_quota_manager.quotas[i].last_updated = time(NULL);
                found = 1;
                break;
            }
        }
    }
    
    pthread_mutex_unlock(&g_quota_manager.lock);
    return found ? 0 : 1;  /* Return 1 if no quota found (not an error) */
}

/**
 * Update functions for each resource type
 */
static int add_pod_update(quota_usage_t* usage) {
    return 0;  /* Updated by caller */
}

int resource_quota_add_pod(const char* namespace,
                          long long cpu_millicores,
                          long long memory_bytes) {
    if (!namespace) {
        return -1;
    }
    
    pthread_mutex_lock(&g_quota_manager.lock);
    
    for (int i = 0; i < g_quota_manager.quota_count; i++) {
        if (strcmp(g_quota_manager.quotas[i].namespace, namespace) == 0) {
            g_quota_manager.quotas[i].used.cpu_used_millicores += cpu_millicores;
            g_quota_manager.quotas[i].used.memory_used_bytes += memory_bytes;
            g_quota_manager.quotas[i].used.pods_used++;
            g_quota_manager.quotas[i].last_updated = time(NULL);
            pthread_mutex_unlock(&g_quota_manager.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_quota_manager.lock);
    return 1;  /* No quota found (not an error) */
}

int resource_quota_remove_pod(const char* namespace,
                             long long cpu_millicores,
                             long long memory_bytes) {
    if (!namespace) {
        return -1;
    }
    
    pthread_mutex_lock(&g_quota_manager.lock);
    
    for (int i = 0; i < g_quota_manager.quota_count; i++) {
        if (strcmp(g_quota_manager.quotas[i].namespace, namespace) == 0) {
            g_quota_manager.quotas[i].used.cpu_used_millicores -= cpu_millicores;
            g_quota_manager.quotas[i].used.memory_used_bytes -= memory_bytes;
            g_quota_manager.quotas[i].used.pods_used--;
            
            /* Ensure counters don't go negative */
            if (g_quota_manager.quotas[i].used.cpu_used_millicores < 0)
                g_quota_manager.quotas[i].used.cpu_used_millicores = 0;
            if (g_quota_manager.quotas[i].used.memory_used_bytes < 0)
                g_quota_manager.quotas[i].used.memory_used_bytes = 0;
            if (g_quota_manager.quotas[i].used.pods_used < 0)
                g_quota_manager.quotas[i].used.pods_used = 0;
            
            g_quota_manager.quotas[i].last_updated = time(NULL);
            pthread_mutex_unlock(&g_quota_manager.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_quota_manager.lock);
    return 1;
}

int resource_quota_add_service(const char* namespace) {
    if (!namespace) return -1;
    
    pthread_mutex_lock(&g_quota_manager.lock);
    for (int i = 0; i < g_quota_manager.quota_count; i++) {
        if (strcmp(g_quota_manager.quotas[i].namespace, namespace) == 0) {
            g_quota_manager.quotas[i].used.services_used++;
            g_quota_manager.quotas[i].last_updated = time(NULL);
            pthread_mutex_unlock(&g_quota_manager.lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_quota_manager.lock);
    return 1;
}

int resource_quota_remove_service(const char* namespace) {
    if (!namespace) return -1;
    
    pthread_mutex_lock(&g_quota_manager.lock);
    for (int i = 0; i < g_quota_manager.quota_count; i++) {
        if (strcmp(g_quota_manager.quotas[i].namespace, namespace) == 0) {
            if (g_quota_manager.quotas[i].used.services_used > 0)
                g_quota_manager.quotas[i].used.services_used--;
            g_quota_manager.quotas[i].last_updated = time(NULL);
            pthread_mutex_unlock(&g_quota_manager.lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_quota_manager.lock);
    return 1;
}

int resource_quota_add_configmap(const char* namespace) {
    if (!namespace) return -1;
    
    pthread_mutex_lock(&g_quota_manager.lock);
    for (int i = 0; i < g_quota_manager.quota_count; i++) {
        if (strcmp(g_quota_manager.quotas[i].namespace, namespace) == 0) {
            g_quota_manager.quotas[i].used.configmaps_used++;
            g_quota_manager.quotas[i].last_updated = time(NULL);
            pthread_mutex_unlock(&g_quota_manager.lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_quota_manager.lock);
    return 1;
}

int resource_quota_remove_configmap(const char* namespace) {
    if (!namespace) return -1;
    
    pthread_mutex_lock(&g_quota_manager.lock);
    for (int i = 0; i < g_quota_manager.quota_count; i++) {
        if (strcmp(g_quota_manager.quotas[i].namespace, namespace) == 0) {
            if (g_quota_manager.quotas[i].used.configmaps_used > 0)
                g_quota_manager.quotas[i].used.configmaps_used--;
            g_quota_manager.quotas[i].last_updated = time(NULL);
            pthread_mutex_unlock(&g_quota_manager.lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_quota_manager.lock);
    return 1;
}

int resource_quota_add_secret(const char* namespace) {
    if (!namespace) return -1;
    
    pthread_mutex_lock(&g_quota_manager.lock);
    for (int i = 0; i < g_quota_manager.quota_count; i++) {
        if (strcmp(g_quota_manager.quotas[i].namespace, namespace) == 0) {
            g_quota_manager.quotas[i].used.secrets_used++;
            g_quota_manager.quotas[i].last_updated = time(NULL);
            pthread_mutex_unlock(&g_quota_manager.lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_quota_manager.lock);
    return 1;
}

int resource_quota_remove_secret(const char* namespace) {
    if (!namespace) return -1;
    
    pthread_mutex_lock(&g_quota_manager.lock);
    for (int i = 0; i < g_quota_manager.quota_count; i++) {
        if (strcmp(g_quota_manager.quotas[i].namespace, namespace) == 0) {
            if (g_quota_manager.quotas[i].used.secrets_used > 0)
                g_quota_manager.quotas[i].used.secrets_used--;
            g_quota_manager.quotas[i].last_updated = time(NULL);
            pthread_mutex_unlock(&g_quota_manager.lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_quota_manager.lock);
    return 1;
}

/* ============================================================================
   Admission Control
   ============================================================================ */

int resource_quota_can_create_pod(const char* namespace,
                                 long long cpu_millicores,
                                 long long memory_bytes,
                                 quota_admission_result_t* result_out) {
    if (!namespace || !result_out) {
        return -1;
    }
    
    pthread_mutex_lock(&g_quota_manager.lock);
    
    /* Find quota for namespace */
    int quota_idx = -1;
    for (int i = 0; i < g_quota_manager.quota_count; i++) {
        if (strcmp(g_quota_manager.quotas[i].namespace, namespace) == 0) {
            quota_idx = i;
            break;
        }
    }
    
    /* No quota defined */
    if (quota_idx < 0) {
        pthread_mutex_unlock(&g_quota_manager.lock);
        return 1;  /* QUOTA_NOT_FOUND */
    }
    
    resource_quota_t* quota = &g_quota_manager.quotas[quota_idx];
    
    /* Check CPU quota */
    if (quota->hard.cpu_millicores > 0) {
        if (quota->used.cpu_used_millicores + cpu_millicores > quota->hard.cpu_millicores) {
            snprintf(result_out->reason, sizeof(result_out->reason),
                    "CPU quota exceeded: %lld + %lld > %lld",
                    quota->used.cpu_used_millicores, cpu_millicores,
                    quota->hard.cpu_millicores);
            strcpy(result_out->exceeded_resource, "cpu");
            result_out->requested = cpu_millicores;
            result_out->limit = quota->hard.cpu_millicores;
            result_out->current = quota->used.cpu_used_millicores;
            result_out->decision = QUOTA_EXCEEDED;
            pthread_mutex_unlock(&g_quota_manager.lock);
            return -1;  /* QUOTA_EXCEEDED */
        }
    }
    
    /* Check memory quota */
    if (quota->hard.memory_bytes > 0) {
        if (quota->used.memory_used_bytes + memory_bytes > quota->hard.memory_bytes) {
            snprintf(result_out->reason, sizeof(result_out->reason),
                    "Memory quota exceeded: %lld + %lld > %lld",
                    quota->used.memory_used_bytes, memory_bytes,
                    quota->hard.memory_bytes);
            strcpy(result_out->exceeded_resource, "memory");
            result_out->requested = memory_bytes;
            result_out->limit = quota->hard.memory_bytes;
            result_out->current = quota->used.memory_used_bytes;
            result_out->decision = QUOTA_EXCEEDED;
            pthread_mutex_unlock(&g_quota_manager.lock);
            return -1;
        }
    }
    
    /* Check pod count quota */
    if (quota->hard.pods > 0) {
        if (quota->used.pods_used >= quota->hard.pods) {
            snprintf(result_out->reason, sizeof(result_out->reason),
                    "Pod quota exceeded: %d >= %d",
                    quota->used.pods_used, quota->hard.pods);
            strcpy(result_out->exceeded_resource, "pods");
            result_out->requested = 1;
            result_out->limit = quota->hard.pods;
            result_out->current = quota->used.pods_used;
            result_out->decision = QUOTA_EXCEEDED;
            pthread_mutex_unlock(&g_quota_manager.lock);
            return -1;
        }
    }
    
    /* All checks passed */
    result_out->decision = QUOTA_ALLOWED;
    strcpy(result_out->reason, "Pod creation allowed");
    pthread_mutex_unlock(&g_quota_manager.lock);
    return 0;  /* QUOTA_ALLOWED */
}

int resource_quota_can_create_service(const char* namespace,
                                     quota_admission_result_t* result_out) {
    if (!namespace || !result_out) return -1;
    
    pthread_mutex_lock(&g_quota_manager.lock);
    
    int quota_idx = -1;
    for (int i = 0; i < g_quota_manager.quota_count; i++) {
        if (strcmp(g_quota_manager.quotas[i].namespace, namespace) == 0) {
            quota_idx = i;
            break;
        }
    }
    
    if (quota_idx < 0) {
        pthread_mutex_unlock(&g_quota_manager.lock);
        return 1;
    }
    
    resource_quota_t* quota = &g_quota_manager.quotas[quota_idx];
    
    if (quota->hard.services > 0 && quota->used.services_used >= quota->hard.services) {
        snprintf(result_out->reason, sizeof(result_out->reason),
                "Service quota exceeded: %d >= %d",
                quota->used.services_used, quota->hard.services);
        strcpy(result_out->exceeded_resource, "services");
        result_out->decision = QUOTA_EXCEEDED;
        pthread_mutex_unlock(&g_quota_manager.lock);
        return -1;
    }
    
    result_out->decision = QUOTA_ALLOWED;
    pthread_mutex_unlock(&g_quota_manager.lock);
    return 0;
}

int resource_quota_can_create_configmap(const char* namespace,
                                       quota_admission_result_t* result_out) {
    if (!namespace || !result_out) return -1;
    
    pthread_mutex_lock(&g_quota_manager.lock);
    
    int quota_idx = -1;
    for (int i = 0; i < g_quota_manager.quota_count; i++) {
        if (strcmp(g_quota_manager.quotas[i].namespace, namespace) == 0) {
            quota_idx = i;
            break;
        }
    }
    
    if (quota_idx < 0) {
        pthread_mutex_unlock(&g_quota_manager.lock);
        return 1;
    }
    
    resource_quota_t* quota = &g_quota_manager.quotas[quota_idx];
    
    if (quota->hard.configmaps > 0 && quota->used.configmaps_used >= quota->hard.configmaps) {
        snprintf(result_out->reason, sizeof(result_out->reason),
                "ConfigMap quota exceeded: %d >= %d",
                quota->used.configmaps_used, quota->hard.configmaps);
        strcpy(result_out->exceeded_resource, "configmaps");
        result_out->decision = QUOTA_EXCEEDED;
        pthread_mutex_unlock(&g_quota_manager.lock);
        return -1;
    }
    
    result_out->decision = QUOTA_ALLOWED;
    pthread_mutex_unlock(&g_quota_manager.lock);
    return 0;
}

int resource_quota_can_create_secret(const char* namespace,
                                    quota_admission_result_t* result_out) {
    if (!namespace || !result_out) return -1;
    
    pthread_mutex_lock(&g_quota_manager.lock);
    
    int quota_idx = -1;
    for (int i = 0; i < g_quota_manager.quota_count; i++) {
        if (strcmp(g_quota_manager.quotas[i].namespace, namespace) == 0) {
            quota_idx = i;
            break;
        }
    }
    
    if (quota_idx < 0) {
        pthread_mutex_unlock(&g_quota_manager.lock);
        return 1;
    }
    
    resource_quota_t* quota = &g_quota_manager.quotas[quota_idx];
    
    if (quota->hard.secrets > 0 && quota->used.secrets_used >= quota->hard.secrets) {
        snprintf(result_out->reason, sizeof(result_out->reason),
                "Secret quota exceeded: %d >= %d",
                quota->used.secrets_used, quota->hard.secrets);
        strcpy(result_out->exceeded_resource, "secrets");
        result_out->decision = QUOTA_EXCEEDED;
        pthread_mutex_unlock(&g_quota_manager.lock);
        return -1;
    }
    
    result_out->decision = QUOTA_ALLOWED;
    pthread_mutex_unlock(&g_quota_manager.lock);
    return 0;
}

/* ============================================================================
   Status & Reporting
   ============================================================================ */

int resource_quota_get_status(const char* namespace,
                             const char* name,
                             resource_quota_status_t* status_out) {
    if (!namespace || !name || !status_out) {
        return -1;
    }
    
    pthread_mutex_lock(&g_quota_manager.lock);
    
    int idx = find_quota_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&g_quota_manager.lock);
        return -1;
    }
    
    status_out->spec = g_quota_manager.quotas[idx];
    status_out->status = g_quota_manager.quotas[idx].used;
    status_out->status_updated_at = g_quota_manager.quotas[idx].last_updated;
    
    pthread_mutex_unlock(&g_quota_manager.lock);
    return 0;
}

int resource_quota_get_usage(const char* namespace,
                            quota_usage_t* usage_out) {
    if (!namespace || !usage_out) {
        return -1;
    }
    
    memset(usage_out, 0, sizeof(quota_usage_t));
    
    pthread_mutex_lock(&g_quota_manager.lock);
    
    for (int i = 0; i < g_quota_manager.quota_count; i++) {
        if (strcmp(g_quota_manager.quotas[i].namespace, namespace) == 0) {
            *usage_out = g_quota_manager.quotas[i].used;
            pthread_mutex_unlock(&g_quota_manager.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_quota_manager.lock);
    return 1;  /* No quota (not an error) */
}

int resource_quota_format_quota_exceeded(const quota_admission_result_t* result,
                                        char* response_buffer,
                                        int* response_code) {
    if (!result || !response_buffer || !response_code) {
        return -1;
    }
    
    json_object* root = json_object_new_object();
    
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("Status"));
    json_object_object_add(root, "status", json_object_new_string("Failure"));
    json_object_object_add(root, "message", 
        json_object_new_string(result->reason));
    json_object_object_add(root, "reason", json_object_new_string("Forbidden"));
    json_object_object_add(root, "code", json_object_new_int(429));
    
    json_object* details = json_object_new_object();
    json_object_object_add(details, "resource", 
        json_object_new_string(result->exceeded_resource));
    json_object_object_add(details, "requested", 
        json_object_new_int64(result->requested));
    json_object_object_add(details, "limit", 
        json_object_new_int64(result->limit));
    json_object_object_add(details, "current", 
        json_object_new_int64(result->current));
    json_object_object_add(root, "details", details);
    
    const char* json_str = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    strncpy(response_buffer, json_str, 16383);
    response_buffer[16383] = '\0';
    
    *response_code = 429;
    
    json_object_put(root);
    return 0;
}
