/*
 * LimitRange Manager Implementation - Kubernetes v1.28
 *
 * Complete implementation of LimitRange resource management with
 * per-container, pod-level, and PVC limit enforcement via admission control.
 *
 * Thread-safe singleton manager with mutex protection.
 */

#include "limitrange.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/* Global LimitRange manager */
static limitrange_manager_t limitrange_manager = {0};

/* ============================================================================
 * Helper Functions
 * ========================================================================== */

static int find_limitrange_index(const char *namespace, const char *name) {
    if (!namespace || !name) return -1;
    
    for (int i = 0; i < limitrange_manager.count; i++) {
        if (strcmp(limitrange_manager.limitranges[i].spec.namespace, namespace) == 0 &&
            strcmp(limitrange_manager.limitranges[i].spec.name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* ============================================================================
 * Lifecycle Management
 * ========================================================================== */

int limitrange_manager_init(void) {
    if (limitrange_manager.initialized) return 0;
    
    pthread_mutex_init(&limitrange_manager.lock, NULL);
    limitrange_manager.count = 0;
    memset(limitrange_manager.limitranges, 0, sizeof(limitrange_manager.limitranges));
    limitrange_manager.initialized = 1;
    
    return 0;
}

int limitrange_manager_shutdown(void) {
    if (!limitrange_manager.initialized) return 0;
    
    pthread_mutex_lock(&limitrange_manager.lock);
    limitrange_manager.count = 0;
    memset(limitrange_manager.limitranges, 0, sizeof(limitrange_manager.limitranges));
    limitrange_manager.initialized = 0;
    pthread_mutex_unlock(&limitrange_manager.lock);
    
    pthread_mutex_destroy(&limitrange_manager.lock);
    return 0;
}

/* ============================================================================
 * CRUD Operations
 * ========================================================================== */

int limitrange_create(const char *namespace, const char *name, const limitrange_spec_t *spec) {
    if (!namespace || !name || !spec) return -1;
    if (limitrange_manager.count >= 100) return -1;
    
    pthread_mutex_lock(&limitrange_manager.lock);
    
    if (find_limitrange_index(namespace, name) >= 0) {
        pthread_mutex_unlock(&limitrange_manager.lock);
        return -1;
    }
    
    char reason[256] = {0};
    if (!limitrange_validate_spec(spec, reason)) {
        pthread_mutex_unlock(&limitrange_manager.lock);
        return -2;
    }
    
    int idx = limitrange_manager.count;
    limitrange_t *lr = &limitrange_manager.limitranges[idx];
    
    memcpy(&lr->spec, spec, sizeof(limitrange_spec_t));
    strncpy(lr->spec.namespace, namespace, 127);
    strncpy(lr->spec.name, name, 255);
    
    lr->spec.created_at = time(NULL);
    lr->spec.updated_at = time(NULL);
    lr->spec.generation = 1;
    
    lr->status.enforcing = 1;
    strncpy(lr->status.message, "LimitRange enforcing", 255);
    
    limitrange_manager.count++;
    
    pthread_mutex_unlock(&limitrange_manager.lock);
    return 0;
}

int limitrange_get(const char *namespace, const char *name, limitrange_t *out_limitrange) {
    if (!namespace || !name || !out_limitrange) return -1;
    
    pthread_mutex_lock(&limitrange_manager.lock);
    
    int idx = find_limitrange_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&limitrange_manager.lock);
        return -1;
    }
    
    memcpy(out_limitrange, &limitrange_manager.limitranges[idx], sizeof(limitrange_t));
    
    pthread_mutex_unlock(&limitrange_manager.lock);
    return 0;
}

int limitrange_update(const char *namespace, const char *name, const limitrange_spec_t *spec) {
    if (!namespace || !name || !spec) return -1;
    
    pthread_mutex_lock(&limitrange_manager.lock);
    
    int idx = find_limitrange_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&limitrange_manager.lock);
        return -1;
    }
    
    limitrange_t *lr = &limitrange_manager.limitranges[idx];
    
    char reason[256] = {0};
    if (!limitrange_validate_spec(spec, reason)) {
        pthread_mutex_unlock(&limitrange_manager.lock);
        return -2;
    }
    
    time_t created_at = lr->spec.created_at;
    memcpy(&lr->spec, spec, sizeof(limitrange_spec_t));
    strncpy(lr->spec.namespace, namespace, 127);
    strncpy(lr->spec.name, name, 255);
    lr->spec.created_at = created_at;
    lr->spec.updated_at = time(NULL);
    lr->spec.generation++;
    
    pthread_mutex_unlock(&limitrange_manager.lock);
    return 0;
}

int limitrange_delete(const char *namespace, const char *name) {
    if (!namespace || !name) return -1;
    
    pthread_mutex_lock(&limitrange_manager.lock);
    
    int idx = find_limitrange_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&limitrange_manager.lock);
        return -1;
    }
    
    for (int i = idx; i < limitrange_manager.count - 1; i++) {
        memcpy(&limitrange_manager.limitranges[i], &limitrange_manager.limitranges[i + 1], 
               sizeof(limitrange_t));
    }
    
    memset(&limitrange_manager.limitranges[limitrange_manager.count - 1], 0, sizeof(limitrange_t));
    limitrange_manager.count--;
    
    pthread_mutex_unlock(&limitrange_manager.lock);
    return 0;
}

int limitrange_list(const char *namespace, limitrange_t *out_limitranges) {
    if (!namespace || !out_limitranges) return -1;
    
    pthread_mutex_lock(&limitrange_manager.lock);
    
    int count = 0;
    for (int i = 0; i < limitrange_manager.count; i++) {
        if (strcmp(limitrange_manager.limitranges[i].spec.namespace, namespace) == 0) {
            memcpy(&out_limitranges[count], &limitrange_manager.limitranges[i], sizeof(limitrange_t));
            count++;
        }
    }
    
    pthread_mutex_unlock(&limitrange_manager.lock);
    return count;
}

int limitrange_list_count(const char *namespace) {
    if (!namespace) return -1;
    
    pthread_mutex_lock(&limitrange_manager.lock);
    
    int count = 0;
    for (int i = 0; i < limitrange_manager.count; i++) {
        if (strcmp(limitrange_manager.limitranges[i].spec.namespace, namespace) == 0) {
            count++;
        }
    }
    
    pthread_mutex_unlock(&limitrange_manager.lock);
    return count;
}

/* ============================================================================
 * Container Validation
 * ========================================================================== */

int limitrange_validate_container(const char *namespace,
                                   uint64_t cpu_request, uint64_t cpu_limit,
                                   uint64_t memory_request, uint64_t memory_limit,
                                   char *out_reason) {
    if (!namespace || !out_reason) return 0;
    
    pthread_mutex_lock(&limitrange_manager.lock);
    
    limitrange_item_t container_item = {0};
    int found = 0;
    
    /* Find container limit item */
    for (int i = 0; i < limitrange_manager.count; i++) {
        if (strcmp(limitrange_manager.limitranges[i].spec.namespace, namespace) == 0) {
            for (int j = 0; j < limitrange_manager.limitranges[i].spec.item_count; j++) {
                if (limitrange_manager.limitranges[i].spec.items[j].type == LIMITRANGE_TYPE_CONTAINER) {
                    memcpy(&container_item, &limitrange_manager.limitranges[i].spec.items[j],
                           sizeof(limitrange_item_t));
                    found = 1;
                    break;
                }
            }
            break;
        }
    }
    
    pthread_mutex_unlock(&limitrange_manager.lock);
    
    if (!found) return 1;  /* No limits defined, allow */
    
    /* Validate CPU */
    if (cpu_request > 0 && container_item.cpu.min_bytes > 0) {
        if (cpu_request < container_item.cpu.min_bytes) {
            snprintf(out_reason, 256, "CPU request %.0fm is less than minimum %.0fm",
                     (double)cpu_request / 1000, (double)container_item.cpu.min_bytes / 1000);
            return 0;
        }
    }
    
    if (cpu_limit > 0 && container_item.cpu.max_bytes > 0) {
        if (cpu_limit > container_item.cpu.max_bytes) {
            snprintf(out_reason, 256, "CPU limit %.0fm exceeds maximum %.0fm",
                     (double)cpu_limit / 1000, (double)container_item.cpu.max_bytes / 1000);
            return 0;
        }
    }
    
    /* Validate memory */
    if (memory_request > 0 && container_item.memory.min_bytes > 0) {
        if (memory_request < container_item.memory.min_bytes) {
            snprintf(out_reason, 256, "Memory request %luMi is less than minimum %luMi",
                     memory_request / (1024 * 1024), 
                     container_item.memory.min_bytes / (1024 * 1024));
            return 0;
        }
    }
    
    if (memory_limit > 0 && container_item.memory.max_bytes > 0) {
        if (memory_limit > container_item.memory.max_bytes) {
            snprintf(out_reason, 256, "Memory limit %luMi exceeds maximum %luMi",
                     memory_limit / (1024 * 1024),
                     container_item.memory.max_bytes / (1024 * 1024));
            return 0;
        }
    }
    
    return 1;
}

int limitrange_apply_container_defaults(const char *namespace,
                                         uint64_t in_cpu_request, uint64_t *out_cpu_request,
                                         uint64_t in_memory_request, uint64_t *out_memory_request) {
    if (!namespace || !out_cpu_request || !out_memory_request) return -1;
    
    pthread_mutex_lock(&limitrange_manager.lock);
    
    limitrange_item_t container_item = {0};
    int found = 0;
    
    for (int i = 0; i < limitrange_manager.count; i++) {
        if (strcmp(limitrange_manager.limitranges[i].spec.namespace, namespace) == 0) {
            for (int j = 0; j < limitrange_manager.limitranges[i].spec.item_count; j++) {
                if (limitrange_manager.limitranges[i].spec.items[j].type == LIMITRANGE_TYPE_CONTAINER) {
                    memcpy(&container_item, &limitrange_manager.limitranges[i].spec.items[j],
                           sizeof(limitrange_item_t));
                    found = 1;
                    break;
                }
            }
            break;
        }
    }
    
    pthread_mutex_unlock(&limitrange_manager.lock);
    
    if (!found) return -1;
    
    /* Apply CPU default request */
    *out_cpu_request = (in_cpu_request > 0) ? in_cpu_request : container_item.cpu.default_request_bytes;
    
    /* Apply memory default request */
    *out_memory_request = (in_memory_request > 0) ? in_memory_request : 
                          container_item.memory.default_request_bytes;
    
    return 0;
}

int limitrange_apply_container_limit_defaults(const char *namespace,
                                               uint64_t in_cpu_limit, uint64_t *out_cpu_limit,
                                               uint64_t in_memory_limit, uint64_t *out_memory_limit) {
    if (!namespace || !out_cpu_limit || !out_memory_limit) return -1;
    
    pthread_mutex_lock(&limitrange_manager.lock);
    
    limitrange_item_t container_item = {0};
    int found = 0;
    
    for (int i = 0; i < limitrange_manager.count; i++) {
        if (strcmp(limitrange_manager.limitranges[i].spec.namespace, namespace) == 0) {
            for (int j = 0; j < limitrange_manager.limitranges[i].spec.item_count; j++) {
                if (limitrange_manager.limitranges[i].spec.items[j].type == LIMITRANGE_TYPE_CONTAINER) {
                    memcpy(&container_item, &limitrange_manager.limitranges[i].spec.items[j],
                           sizeof(limitrange_item_t));
                    found = 1;
                    break;
                }
            }
            break;
        }
    }
    
    pthread_mutex_unlock(&limitrange_manager.lock);
    
    if (!found) return -1;
    
    *out_cpu_limit = (in_cpu_limit > 0) ? in_cpu_limit : container_item.cpu.default_bytes;
    *out_memory_limit = (in_memory_limit > 0) ? in_memory_limit : container_item.memory.default_bytes;
    
    return 0;
}

/* ============================================================================
 * Pod-Level Validation
 * ========================================================================== */

int limitrange_validate_pod(const char *namespace,
                             uint64_t total_cpu_request, uint64_t total_cpu_limit,
                             uint64_t total_memory_request, uint64_t total_memory_limit,
                             int container_count,
                             char *out_reason) {
    if (!namespace || !out_reason) return 0;
    
    pthread_mutex_lock(&limitrange_manager.lock);
    
    limitrange_item_t pod_item = {0};
    int found = 0;
    
    for (int i = 0; i < limitrange_manager.count; i++) {
        if (strcmp(limitrange_manager.limitranges[i].spec.namespace, namespace) == 0) {
            for (int j = 0; j < limitrange_manager.limitranges[i].spec.item_count; j++) {
                if (limitrange_manager.limitranges[i].spec.items[j].type == LIMITRANGE_TYPE_POD) {
                    memcpy(&pod_item, &limitrange_manager.limitranges[i].spec.items[j],
                           sizeof(limitrange_item_t));
                    found = 1;
                    break;
                }
            }
            break;
        }
    }
    
    pthread_mutex_unlock(&limitrange_manager.lock);
    
    if (!found) return 1;
    
    /* Validate pod-level aggregate limits */
    if (total_cpu_request > 0 && pod_item.cpu.max_bytes > 0) {
        if (total_cpu_request > pod_item.cpu.max_bytes) {
            snprintf(out_reason, 256, "Pod total CPU exceeds limit");
            return 0;
        }
    }
    
    if (total_memory_request > 0 && pod_item.memory.max_bytes > 0) {
        if (total_memory_request > pod_item.memory.max_bytes) {
            snprintf(out_reason, 256, "Pod total memory exceeds limit");
            return 0;
        }
    }
    
    return 1;
}

int limitrange_apply_pod_limit_defaults(const char *namespace,
                                         uint64_t in_cpu_limit, uint64_t *out_cpu_limit) {
    if (!namespace || !out_cpu_limit) return -1;
    
    pthread_mutex_lock(&limitrange_manager.lock);
    
    limitrange_item_t pod_item = {0};
    int found = 0;
    
    for (int i = 0; i < limitrange_manager.count; i++) {
        if (strcmp(limitrange_manager.limitranges[i].spec.namespace, namespace) == 0) {
            for (int j = 0; j < limitrange_manager.limitranges[i].spec.item_count; j++) {
                if (limitrange_manager.limitranges[i].spec.items[j].type == LIMITRANGE_TYPE_POD) {
                    memcpy(&pod_item, &limitrange_manager.limitranges[i].spec.items[j],
                           sizeof(limitrange_item_t));
                    found = 1;
                    break;
                }
            }
            break;
        }
    }
    
    pthread_mutex_unlock(&limitrange_manager.lock);
    
    if (!found) return -1;
    
    *out_cpu_limit = (in_cpu_limit > 0) ? in_cpu_limit : pod_item.cpu.default_bytes;
    
    return 0;
}

/* ============================================================================
 * Storage Limit Validation
 * ========================================================================== */

int limitrange_validate_storage(const char *namespace, uint64_t storage_bytes, char *out_reason) {
    if (!namespace || !out_reason) return 0;
    
    pthread_mutex_lock(&limitrange_manager.lock);
    
    limitrange_item_t pvc_item = {0};
    int found = 0;
    
    for (int i = 0; i < limitrange_manager.count; i++) {
        if (strcmp(limitrange_manager.limitranges[i].spec.namespace, namespace) == 0) {
            for (int j = 0; j < limitrange_manager.limitranges[i].spec.item_count; j++) {
                if (limitrange_manager.limitranges[i].spec.items[j].type == LIMITRANGE_TYPE_PVC) {
                    memcpy(&pvc_item, &limitrange_manager.limitranges[i].spec.items[j],
                           sizeof(limitrange_item_t));
                    found = 1;
                    break;
                }
            }
            break;
        }
    }
    
    pthread_mutex_unlock(&limitrange_manager.lock);
    
    if (!found) return 1;
    
    if (storage_bytes > 0 && pvc_item.storage.max_bytes > 0) {
        if (storage_bytes > pvc_item.storage.max_bytes) {
            snprintf(out_reason, 256, "Storage size exceeds limit");
            return 0;
        }
    }
    
    return 1;
}

/* ============================================================================
 * Item Query Functions
 * ========================================================================== */

int limitrange_get_container_item(const char *namespace, limitrange_item_t *out_item) {
    if (!namespace || !out_item) return -1;
    
    pthread_mutex_lock(&limitrange_manager.lock);
    
    for (int i = 0; i < limitrange_manager.count; i++) {
        if (strcmp(limitrange_manager.limitranges[i].spec.namespace, namespace) == 0) {
            for (int j = 0; j < limitrange_manager.limitranges[i].spec.item_count; j++) {
                if (limitrange_manager.limitranges[i].spec.items[j].type == LIMITRANGE_TYPE_CONTAINER) {
                    memcpy(out_item, &limitrange_manager.limitranges[i].spec.items[j],
                           sizeof(limitrange_item_t));
                    pthread_mutex_unlock(&limitrange_manager.lock);
                    return 0;
                }
            }
        }
    }
    
    pthread_mutex_unlock(&limitrange_manager.lock);
    return -1;
}

int limitrange_get_pod_item(const char *namespace, limitrange_item_t *out_item) {
    if (!namespace || !out_item) return -1;
    
    pthread_mutex_lock(&limitrange_manager.lock);
    
    for (int i = 0; i < limitrange_manager.count; i++) {
        if (strcmp(limitrange_manager.limitranges[i].spec.namespace, namespace) == 0) {
            for (int j = 0; j < limitrange_manager.limitranges[i].spec.item_count; j++) {
                if (limitrange_manager.limitranges[i].spec.items[j].type == LIMITRANGE_TYPE_POD) {
                    memcpy(out_item, &limitrange_manager.limitranges[i].spec.items[j],
                           sizeof(limitrange_item_t));
                    pthread_mutex_unlock(&limitrange_manager.lock);
                    return 0;
                }
            }
        }
    }
    
    pthread_mutex_unlock(&limitrange_manager.lock);
    return -1;
}

int limitrange_get_pvc_item(const char *namespace, limitrange_item_t *out_item) {
    if (!namespace || !out_item) return -1;
    
    pthread_mutex_lock(&limitrange_manager.lock);
    
    for (int i = 0; i < limitrange_manager.count; i++) {
        if (strcmp(limitrange_manager.limitranges[i].spec.namespace, namespace) == 0) {
            for (int j = 0; j < limitrange_manager.limitranges[i].spec.item_count; j++) {
                if (limitrange_manager.limitranges[i].spec.items[j].type == LIMITRANGE_TYPE_PVC) {
                    memcpy(out_item, &limitrange_manager.limitranges[i].spec.items[j],
                           sizeof(limitrange_item_t));
                    pthread_mutex_unlock(&limitrange_manager.lock);
                    return 0;
                }
            }
        }
    }
    
    pthread_mutex_unlock(&limitrange_manager.lock);
    return -1;
}

/* ============================================================================
 * Status Query Functions
 * ========================================================================== */

int limitrange_get_status_json(const char *namespace, const char *name, char *out_json) {
    if (!namespace || !name || !out_json) return -1;
    
    limitrange_t lr = {0};
    if (limitrange_get(namespace, name, &lr) < 0) return -1;
    
    snprintf(out_json, 1024,
        "{"
        "  \"enforcing\": %d,"
        "  \"message\": \"%s\""
        "}",
        lr.status.enforcing, lr.status.message);
    
    return 0;
}

int limitrange_is_enforcing(const char *namespace, const char *name) {
    if (!namespace || !name) return 0;
    
    limitrange_t lr = {0};
    if (limitrange_get(namespace, name, &lr) < 0) return 0;
    
    return lr.status.enforcing;
}

/* ============================================================================
 * Validation & Admission Control
 * ========================================================================== */

int limitrange_validate_spec(const limitrange_spec_t *spec, char *out_reason) {
    if (!spec) return 0;
    
    if (strlen(spec->name) == 0 || strlen(spec->name) > 253) {
        if (out_reason) snprintf(out_reason, 256, "Invalid name");
        return 0;
    }
    
    if (strlen(spec->namespace) == 0 || strlen(spec->namespace) > 253) {
        if (out_reason) snprintf(out_reason, 256, "Namespace is required");
        return 0;
    }
    
    if (spec->item_count <= 0 || spec->item_count > 10) {
        if (out_reason) snprintf(out_reason, 256, "Invalid item count");
        return 0;
    }
    
    return 1;
}

int limitrange_can_create(const char *namespace, const limitrange_spec_t *spec, char *out_reason) {
    if (!namespace || !spec) return 0;
    return limitrange_validate_spec(spec, out_reason);
}

/* ============================================================================
 * Helper Functions
 * ========================================================================== */

int limitrange_format_resource(uint64_t bytes, char *out_str) {
    if (!out_str) return -1;
    
    if (bytes < 1024) {
        snprintf(out_str, 32, "%lu", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(out_str, 32, "%luKi", bytes / 1024);
    } else if (bytes < 1024 * 1024 * 1024) {
        snprintf(out_str, 32, "%luMi", bytes / (1024 * 1024));
    } else {
        snprintf(out_str, 32, "%luGi", bytes / (1024 * 1024 * 1024));
    }
    
    return 0;
}

int limitrange_parse_resource(const char *resource_str, uint64_t *out_bytes) {
    if (!resource_str || !out_bytes) return -1;
    
    uint64_t value = 0;
    int scanned = sscanf(resource_str, "%lu", &value);
    if (scanned != 1) return -1;
    
    if (strstr(resource_str, "Ki")) {
        *out_bytes = value * 1024;
    } else if (strstr(resource_str, "Mi")) {
        *out_bytes = value * 1024 * 1024;
    } else if (strstr(resource_str, "Gi")) {
        *out_bytes = value * 1024 * 1024 * 1024;
    } else {
        *out_bytes = value;
    }
    
    return 0;
}
