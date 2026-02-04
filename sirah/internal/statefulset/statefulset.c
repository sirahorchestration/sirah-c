/*
 * StatefulSet Manager Implementation - Kubernetes v1.28
 *
 * Complete implementation of StatefulSet resource management with ordered
 * pod creation/deletion, persistent volume claim templates, and pod identity.
 *
 * Thread-safe singleton manager with mutex protection.
 */

#include "statefulset.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/* Global StatefulSet manager */
static statefulset_manager_t ss_manager = {0};

/* ============================================================================
 * Helper Functions
 * ========================================================================== */

/**
 * Find StatefulSet index by namespace and name
 * @return Index on success, -1 if not found
 */
static int find_statefulset_index(const char *namespace, const char *name) {
    if (!namespace || !name) return -1;
    
    for (int i = 0; i < ss_manager.count; i++) {
        if (strcmp(ss_manager.statefulsets[i].spec.namespace, namespace) == 0 &&
            strcmp(ss_manager.statefulsets[i].spec.name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* ============================================================================
 * Lifecycle Management
 * ========================================================================== */

int statefulset_manager_init(void) {
    if (ss_manager.initialized) return 0;
    
    pthread_mutex_init(&ss_manager.lock, NULL);
    ss_manager.count = 0;
    memset(ss_manager.statefulsets, 0, sizeof(ss_manager.statefulsets));
    ss_manager.initialized = 1;
    
    return 0;
}

int statefulset_manager_shutdown(void) {
    if (!ss_manager.initialized) return 0;
    
    pthread_mutex_lock(&ss_manager.lock);
    ss_manager.count = 0;
    memset(ss_manager.statefulsets, 0, sizeof(ss_manager.statefulsets));
    ss_manager.initialized = 0;
    pthread_mutex_unlock(&ss_manager.lock);
    
    pthread_mutex_destroy(&ss_manager.lock);
    return 0;
}

/* ============================================================================
 * CRUD Operations
 * ========================================================================== */

int statefulset_create(const char *namespace, const char *name, const statefulset_spec_t *spec) {
    if (!namespace || !name || !spec) return -1;
    if (ss_manager.count >= 500) return -1;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    /* Check for duplicate */
    if (find_statefulset_index(namespace, name) >= 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -1;
    }
    
    /* Validate spec */
    char reason[256] = {0};
    if (!statefulset_validate_spec(spec, reason)) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -2;
    }
    
    /* Create new StatefulSet */
    int idx = ss_manager.count;
    statefulset_t *ss = &ss_manager.statefulsets[idx];
    
    memcpy(&ss->spec, spec, sizeof(statefulset_spec_t));
    strncpy(ss->spec.namespace, namespace, 127);
    strncpy(ss->spec.name, name, 255);
    
    ss->spec.created_at = time(NULL);
    ss->spec.updated_at = time(NULL);
    ss->spec.generation = 1;
    
    /* Initialize status */
    ss->status.replicas = 0;
    ss->status.ready_replicas = 0;
    ss->status.current_replicas = 0;
    ss->status.updated_replicas = 0;
    ss->status.current_revision = 1;
    ss->status.update_revision = 1;
    ss->status.max_ordinal = -1;
    ss->status.ready_ordinal = -1;
    
    ss_manager.count++;
    
    pthread_mutex_unlock(&ss_manager.lock);
    return 0;
}

int statefulset_get(const char *namespace, const char *name, statefulset_t *out_statefulset) {
    if (!namespace || !name || !out_statefulset) return -1;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int idx = find_statefulset_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -1;
    }
    
    memcpy(out_statefulset, &ss_manager.statefulsets[idx], sizeof(statefulset_t));
    
    pthread_mutex_unlock(&ss_manager.lock);
    return 0;
}

int statefulset_update(const char *namespace, const char *name, const statefulset_spec_t *spec) {
    if (!namespace || !name || !spec) return -1;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int idx = find_statefulset_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -1;
    }
    
    statefulset_t *ss = &ss_manager.statefulsets[idx];
    
    /* Validate new spec */
    char reason[256] = {0};
    if (!statefulset_validate_spec(spec, reason)) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -2;
    }
    
    /* Update spec, preserving identity fields */
    time_t created_at = ss->spec.created_at;
    memcpy(&ss->spec, spec, sizeof(statefulset_spec_t));
    strncpy(ss->spec.namespace, namespace, 127);
    strncpy(ss->spec.name, name, 255);
    ss->spec.created_at = created_at;
    ss->spec.updated_at = time(NULL);
    ss->spec.generation++;
    
    pthread_mutex_unlock(&ss_manager.lock);
    return 0;
}

int statefulset_delete(const char *namespace, const char *name, int propagation_policy) {
    if (!namespace || !name) return -1;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int idx = find_statefulset_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -1;
    }
    
    /* Shift array elements */
    for (int i = idx; i < ss_manager.count - 1; i++) {
        memcpy(&ss_manager.statefulsets[i], &ss_manager.statefulsets[i + 1], sizeof(statefulset_t));
    }
    
    memset(&ss_manager.statefulsets[ss_manager.count - 1], 0, sizeof(statefulset_t));
    ss_manager.count--;
    
    pthread_mutex_unlock(&ss_manager.lock);
    return 0;
}

int statefulset_list(const char *namespace, statefulset_t *out_sets) {
    if (!out_sets) return -1;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int count = 0;
    for (int i = 0; i < ss_manager.count; i++) {
        if (!namespace || strcmp(ss_manager.statefulsets[i].spec.namespace, namespace) == 0) {
            memcpy(&out_sets[count], &ss_manager.statefulsets[i], sizeof(statefulset_t));
            count++;
        }
    }
    
    pthread_mutex_unlock(&ss_manager.lock);
    return count;
}

int statefulset_list_count(const char *namespace) {
    pthread_mutex_lock(&ss_manager.lock);
    
    int count = 0;
    for (int i = 0; i < ss_manager.count; i++) {
        if (!namespace || strcmp(ss_manager.statefulsets[i].spec.namespace, namespace) == 0) {
            count++;
        }
    }
    
    pthread_mutex_unlock(&ss_manager.lock);
    return count;
}

/* ============================================================================
 * Pod Ordinal Management
 * ========================================================================== */

int statefulset_get_pod_name(const char *statefulset_name, int32_t ordinal, char *out_name) {
    if (!statefulset_name || !out_name || ordinal < 0 || ordinal >= 1000) return -1;
    
    snprintf(out_name, 256, "%s-%d", statefulset_name, ordinal);
    return 0;
}

int statefulset_extract_ordinal(const char *pod_name, int32_t *out_ordinal) {
    if (!pod_name || !out_ordinal) return -1;
    
    /* Format: statefulset-name-{ordinal}
     * Extract the ordinal from the end */
    const char *last_dash = strrchr(pod_name, '-');
    if (!last_dash) return -1;
    
    int result = sscanf(last_dash + 1, "%d", out_ordinal);
    return (result == 1) ? 0 : -1;
}

int statefulset_get_ordinal_state(const char *namespace, const char *statefulset_name,
                                   int32_t ordinal, pod_ordinal_t *out_state) {
    if (!namespace || !statefulset_name || !out_state || ordinal < 0 || ordinal >= 1000) return -1;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int idx = find_statefulset_index(namespace, statefulset_name);
    if (idx < 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -1;
    }
    
    statefulset_t *ss = &ss_manager.statefulsets[idx];
    if (ordinal > ss->status.max_ordinal) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -1;
    }
    
    memcpy(out_state, &ss->status.pod_ordinals[ordinal], sizeof(pod_ordinal_t));
    
    pthread_mutex_unlock(&ss_manager.lock);
    return 0;
}

int statefulset_set_ordinal_state(const char *namespace, const char *statefulset_name,
                                   int32_t ordinal, pod_ordinal_state_t state) {
    if (!namespace || !statefulset_name || ordinal < 0 || ordinal >= 1000) return -1;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int idx = find_statefulset_index(namespace, statefulset_name);
    if (idx < 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -1;
    }
    
    statefulset_t *ss = &ss_manager.statefulsets[idx];
    
    /* Expand ordinals array if needed */
    if (ordinal > ss->status.max_ordinal) {
        /* Initialize new ordinals */
        for (int i = ss->status.max_ordinal + 1; i <= ordinal; i++) {
            memset(&ss->status.pod_ordinals[i], 0, sizeof(pod_ordinal_t));
            ss->status.pod_ordinals[i].ordinal = i;
            ss->status.pod_ordinals[i].state = ORDINAL_STATE_PENDING;
            statefulset_get_pod_name(statefulset_name, i, ss->status.pod_ordinals[i].pod_name);
        }
        ss->status.max_ordinal = ordinal;
    }
    
    ss->status.pod_ordinals[ordinal].state = state;
    
    pthread_mutex_unlock(&ss_manager.lock);
    return 0;
}

int statefulset_get_ready_ordinal(const char *namespace, const char *statefulset_name,
                                   int32_t *out_ordinal) {
    if (!namespace || !statefulset_name || !out_ordinal) return -1;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int idx = find_statefulset_index(namespace, statefulset_name);
    if (idx < 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -1;
    }
    
    statefulset_t *ss = &ss_manager.statefulsets[idx];
    *out_ordinal = ss->status.ready_ordinal;
    
    pthread_mutex_unlock(&ss_manager.lock);
    return 0;
}

/* ============================================================================
 * Volume Claim Template Management
 * ========================================================================== */

int statefulset_generate_pvc_name(const char *statefulset_name, const char *template_name,
                                   int32_t ordinal, char *out_pvc_name) {
    if (!statefulset_name || !template_name || !out_pvc_name || ordinal < 0) return -1;
    
    snprintf(out_pvc_name, 256, "%s-%s-%s-%d", template_name, statefulset_name, 
             statefulset_name, ordinal);
    return 0;
}

int statefulset_get_volume_templates(const char *namespace, const char *statefulset_name,
                                      volume_claim_template_t *out_templates, int max_templates) {
    if (!namespace || !statefulset_name || !out_templates) return -1;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int idx = find_statefulset_index(namespace, statefulset_name);
    if (idx < 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -1;
    }
    
    statefulset_t *ss = &ss_manager.statefulsets[idx];
    int count = (ss->spec.volume_template_count < max_templates) ? 
                ss->spec.volume_template_count : max_templates;
    
    memcpy(out_templates, ss->spec.volume_templates, count * sizeof(volume_claim_template_t));
    
    pthread_mutex_unlock(&ss_manager.lock);
    return count;
}

int statefulset_create_pvcs_for_ordinal(const char *namespace, const char *statefulset_name,
                                         int32_t ordinal) {
    if (!namespace || !statefulset_name || ordinal < 0) return -1;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int idx = find_statefulset_index(namespace, statefulset_name);
    if (idx < 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -1;
    }
    
    statefulset_t *ss = &ss_manager.statefulsets[idx];
    
    /* Record PVC names in ordinal */
    pod_ordinal_t *ordinal_info = &ss->status.pod_ordinals[ordinal];
    ordinal_info->pvc_count = ss->spec.volume_template_count;
    
    for (int i = 0; i < ss->spec.volume_template_count; i++) {
        statefulset_generate_pvc_name(statefulset_name, ss->spec.volume_templates[i].name,
                                       ordinal, ordinal_info->pvc_names[i]);
    }
    
    pthread_mutex_unlock(&ss_manager.lock);
    return ss->spec.volume_template_count;
}

int statefulset_delete_pvcs_for_ordinal(const char *namespace, const char *statefulset_name,
                                         int32_t ordinal) {
    if (!namespace || !statefulset_name || ordinal < 0) return -1;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int idx = find_statefulset_index(namespace, statefulset_name);
    if (idx < 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -1;
    }
    
    statefulset_t *ss = &ss_manager.statefulsets[idx];
    pod_ordinal_t *ordinal_info = &ss->status.pod_ordinals[ordinal];
    
    int count = ordinal_info->pvc_count;
    ordinal_info->pvc_count = 0;
    memset(ordinal_info->pvc_names, 0, sizeof(ordinal_info->pvc_names));
    
    pthread_mutex_unlock(&ss_manager.lock);
    return count;
}

/* ============================================================================
 * Pod Lifecycle & Ordering
 * ========================================================================== */

int statefulset_get_next_ordinal_to_create(const char *namespace, const char *statefulset_name,
                                            int32_t *out_ordinal) {
    if (!namespace || !statefulset_name || !out_ordinal) return -1;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int idx = find_statefulset_index(namespace, statefulset_name);
    if (idx < 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -1;
    }
    
    statefulset_t *ss = &ss_manager.statefulsets[idx];
    
    /* Current replica count (pods actually created) */
    int32_t current_count = ss->status.replicas;
    
    /* Find ordinals that need creation */
    *out_ordinal = -1;
    for (int32_t i = 0; i < ss->spec.replicas; i++) {
        if (i <= ss->status.max_ordinal) {
            pod_ordinal_state_t state = ss->status.pod_ordinals[i].state;
            if (state == ORDINAL_STATE_PENDING || state == ORDINAL_STATE_FAILED) {
                /* For OrderedReady, only create if previous ordinal is ready */
                if (ss->spec.pod_management_policy == STATEFULSET_STRATEGY_ORDERED_READY) {
                    if (i == 0 || ss->status.pod_ordinals[i - 1].ready) {
                        *out_ordinal = i;
                        break;
                    }
                } else {
                    *out_ordinal = i;
                    break;
                }
            }
        } else {
            /* Ordinal hasn't been created yet */
            if (ss->spec.pod_management_policy == STATEFULSET_STRATEGY_ORDERED_READY) {
                if (i == 0 || ss->status.pod_ordinals[i - 1].ready) {
                    *out_ordinal = i;
                    break;
                }
            } else {
                *out_ordinal = i;
                break;
            }
        }
    }
    
    pthread_mutex_unlock(&ss_manager.lock);
    return 0;
}

int statefulset_get_next_ordinal_to_delete(const char *namespace, const char *statefulset_name,
                                            int32_t *out_ordinal) {
    if (!namespace || !statefulset_name || !out_ordinal) return -1;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int idx = find_statefulset_index(namespace, statefulset_name);
    if (idx < 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -1;
    }
    
    statefulset_t *ss = &ss_manager.statefulsets[idx];
    
    /* Delete in reverse order (highest ordinal first) */
    *out_ordinal = -1;
    for (int32_t i = ss->status.max_ordinal; i >= ss->spec.replicas; i--) {
        if (i >= 0 && ss->status.pod_ordinals[i].state != ORDINAL_STATE_TERMINATED) {
            *out_ordinal = i;
            break;
        }
    }
    
    pthread_mutex_unlock(&ss_manager.lock);
    return 0;
}

int statefulset_is_ordinal_ready(const char *namespace, const char *statefulset_name,
                                  int32_t ordinal) {
    if (!namespace || !statefulset_name || ordinal < 0) return 0;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int idx = find_statefulset_index(namespace, statefulset_name);
    if (idx < 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return 0;
    }
    
    statefulset_t *ss = &ss_manager.statefulsets[idx];
    if (ordinal > ss->status.max_ordinal) {
        pthread_mutex_unlock(&ss_manager.lock);
        return 0;
    }
    
    int ready = ss->status.pod_ordinals[ordinal].ready;
    
    pthread_mutex_unlock(&ss_manager.lock);
    return ready;
}

int statefulset_set_ordinal_ready(const char *namespace, const char *statefulset_name,
                                   int32_t ordinal, int ready) {
    if (!namespace || !statefulset_name || ordinal < 0) return -1;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int idx = find_statefulset_index(namespace, statefulset_name);
    if (idx < 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -1;
    }
    
    statefulset_t *ss = &ss_manager.statefulsets[idx];
    if (ordinal > ss->status.max_ordinal) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -1;
    }
    
    ss->status.pod_ordinals[ordinal].ready = ready;
    
    /* Update ready_ordinal (highest consecutive ready ordinal) */
    if (ready && ordinal == ss->status.ready_ordinal + 1) {
        ss->status.ready_ordinal = ordinal;
    }
    
    pthread_mutex_unlock(&ss_manager.lock);
    return 0;
}

/* ============================================================================
 * Status Management
 * ========================================================================== */

int statefulset_update_status(const char *namespace, const char *statefulset_name,
                               int32_t replicas, int32_t ready_replicas, int32_t updated_replicas) {
    if (!namespace || !statefulset_name) return -1;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int idx = find_statefulset_index(namespace, statefulset_name);
    if (idx < 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -1;
    }
    
    statefulset_t *ss = &ss_manager.statefulsets[idx];
    ss->status.replicas = replicas;
    ss->status.ready_replicas = ready_replicas;
    ss->status.updated_replicas = updated_replicas;
    ss->status.last_update_time = time(NULL);
    
    pthread_mutex_unlock(&ss_manager.lock);
    return 0;
}

int statefulset_get_status(const char *namespace, const char *statefulset_name,
                            statefulset_status_t *out_status) {
    if (!namespace || !statefulset_name || !out_status) return -1;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int idx = find_statefulset_index(namespace, statefulset_name);
    if (idx < 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -1;
    }
    
    memcpy(out_status, &ss_manager.statefulsets[idx].status, sizeof(statefulset_status_t));
    
    pthread_mutex_unlock(&ss_manager.lock);
    return 0;
}

int statefulset_is_rolled_out(const char *namespace, const char *statefulset_name) {
    if (!namespace || !statefulset_name) return 0;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int idx = find_statefulset_index(namespace, statefulset_name);
    if (idx < 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return 0;
    }
    
    statefulset_t *ss = &ss_manager.statefulsets[idx];
    
    /* All replicas should be ready and updated */
    int rolled_out = (ss->status.replicas == ss->spec.replicas &&
                      ss->status.ready_replicas == ss->spec.replicas &&
                      ss->status.updated_replicas == ss->spec.replicas);
    
    pthread_mutex_unlock(&ss_manager.lock);
    return rolled_out;
}

int statefulset_get_status_json(const char *namespace, const char *statefulset_name, char *out_json) {
    if (!namespace || !statefulset_name || !out_json) return -1;
    
    statefulset_status_t status = {0};
    if (statefulset_get_status(namespace, statefulset_name, &status) < 0) return -1;
    
    snprintf(out_json, 2048,
        "{"
        "  \"replicas\": %d,"
        "  \"readyReplicas\": %d,"
        "  \"currentReplicas\": %d,"
        "  \"updatedReplicas\": %d,"
        "  \"currentRevision\": %u,"
        "  \"updateRevision\": %u,"
        "  \"maxOrdinal\": %d,"
        "  \"readyOrdinal\": %d,"
        "  \"updateInProgress\": %d"
        "}",
        status.replicas, status.ready_replicas, status.current_replicas,
        status.updated_replicas, status.current_revision, status.update_revision,
        status.max_ordinal, status.ready_ordinal, status.update_in_progress);
    
    return 0;
}

/* ============================================================================
 * Validation & Admission Control
 * ========================================================================== */

int statefulset_validate_spec(const statefulset_spec_t *spec, char *out_reason) {
    if (!spec) return 0;
    
    /* Validate name */
    if (strlen(spec->name) == 0 || strlen(spec->name) > 253) {
        if (out_reason) snprintf(out_reason, 256, "Invalid name: must be 1-253 characters");
        return 0;
    }
    
    /* Validate replicas */
    if (spec->replicas < 0 || spec->replicas > 1000) {
        if (out_reason) snprintf(out_reason, 256, "Invalid replicas: must be 0-1000");
        return 0;
    }
    
    /* Validate service name (REQUIRED) */
    if (strlen(spec->service_name) == 0) {
        if (out_reason) snprintf(out_reason, 256, "ServiceName is required");
        return 0;
    }
    
    /* Validate termination grace period */
    if (spec->termination_grace_period_seconds < 0 || spec->termination_grace_period_seconds > 3600) {
        if (out_reason) snprintf(out_reason, 256, "Invalid termination grace period: 0-3600 seconds");
        return 0;
    }
    
    return 1;
}

int statefulset_can_create(const char *namespace, const statefulset_spec_t *spec, char *out_reason) {
    if (!namespace || !spec) return 0;
    
    /* Check namespace length */
    if (strlen(namespace) == 0 || strlen(namespace) > 253) {
        if (out_reason) snprintf(out_reason, 256, "Invalid namespace");
        return 0;
    }
    
    return statefulset_validate_spec(spec, out_reason);
}

/* ============================================================================
 * Headless Service Integration
 * ========================================================================== */

int statefulset_get_service_name(const char *namespace, const char *statefulset_name,
                                  char *out_service_name) {
    if (!namespace || !statefulset_name || !out_service_name) return -1;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int idx = find_statefulset_index(namespace, statefulset_name);
    if (idx < 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -1;
    }
    
    statefulset_t *ss = &ss_manager.statefulsets[idx];
    strncpy(out_service_name, ss->spec.service_name, 255);
    
    pthread_mutex_unlock(&ss_manager.lock);
    return 0;
}

int statefulset_verify_headless_service(const char *namespace, const char *service_name) {
    /* Placeholder: would check Service object in manager
     * For now, assume service exists if name is valid */
    if (!namespace || !service_name || strlen(service_name) == 0) return 0;
    return 1;
}

/* ============================================================================
 * Rolling Update Management
 * ========================================================================== */

int statefulset_get_current_revision(const char *namespace, const char *statefulset_name,
                                      uint32_t *out_revision) {
    if (!namespace || !statefulset_name || !out_revision) return -1;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int idx = find_statefulset_index(namespace, statefulset_name);
    if (idx < 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -1;
    }
    
    *out_revision = ss_manager.statefulsets[idx].status.current_revision;
    
    pthread_mutex_unlock(&ss_manager.lock);
    return 0;
}

int statefulset_start_rolling_update(const char *namespace, const char *statefulset_name) {
    if (!namespace || !statefulset_name) return -1;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int idx = find_statefulset_index(namespace, statefulset_name);
    if (idx < 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -1;
    }
    
    statefulset_t *ss = &ss_manager.statefulsets[idx];
    ss->status.update_revision++;
    ss->status.update_in_progress = 1;
    ss->status.last_update_time = time(NULL);
    ss->spec.generation++;
    
    pthread_mutex_unlock(&ss_manager.lock);
    return 0;
}

int statefulset_set_update_partition(const char *namespace, const char *statefulset_name,
                                      int32_t partition) {
    if (!namespace || !statefulset_name || partition < 0) return -1;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int idx = find_statefulset_index(namespace, statefulset_name);
    if (idx < 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return -1;
    }
    
    ss_manager.statefulsets[idx].spec.partition = partition;
    
    pthread_mutex_unlock(&ss_manager.lock);
    return 0;
}

int statefulset_ordinal_needs_update(const char *namespace, const char *statefulset_name,
                                      int32_t ordinal) {
    if (!namespace || !statefulset_name || ordinal < 0) return 0;
    
    pthread_mutex_lock(&ss_manager.lock);
    
    int idx = find_statefulset_index(namespace, statefulset_name);
    if (idx < 0) {
        pthread_mutex_unlock(&ss_manager.lock);
        return 0;
    }
    
    statefulset_t *ss = &ss_manager.statefulsets[idx];
    
    /* For partition-based updates, only ordinals >= partition need update */
    int needs_update = (ordinal >= ss->spec.partition &&
                        ss->status.pod_ordinals[ordinal].state == ORDINAL_STATE_RUNNING);
    
    pthread_mutex_unlock(&ss_manager.lock);
    return needs_update;
}
