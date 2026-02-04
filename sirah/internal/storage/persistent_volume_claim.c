#include "persistent_volume_claim.h"
#include "persistent_volume.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

// Global PVC manager singleton
static pvc_manager_t pvc_manager = {
    .initialized = 0
};

// Initialize PVC manager
int pvc_manager_init(void) {
    if (pvc_manager.initialized) {
        return 0;
    }
    
    pthread_mutex_init(&pvc_manager.lock, NULL);
    pvc_manager.claim_count = 0;
    pvc_manager.initialized = 1;
    
    return 0;
}

// Shutdown PVC manager
int pvc_manager_shutdown(void) {
    if (!pvc_manager.initialized) {
        return 0;
    }
    
    pthread_mutex_lock(&pvc_manager.lock);
    
    pvc_manager.claim_count = 0;
    pvc_manager.initialized = 0;
    
    pthread_mutex_unlock(&pvc_manager.lock);
    pthread_mutex_destroy(&pvc_manager.lock);
    
    return 0;
}

// Helper to find PVC by namespace and name
static int find_pvc_index(const char* namespace, const char* name) {
    for (int i = 0; i < pvc_manager.claim_count; i++) {
        if (strcmp(pvc_manager.claims[i].namespace, namespace) == 0 &&
            strcmp(pvc_manager.claims[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// Create a new PersistentVolumeClaim
int pvc_create(const persistent_volume_claim_t* pvc) {
    if (!pvc || !pvc->namespace || !pvc->name) {
        return -1;  // Invalid input
    }
    
    pthread_mutex_lock(&pvc_manager.lock);
    
    // Check if PVC already exists
    if (find_pvc_index(pvc->namespace, pvc->name) >= 0) {
        pthread_mutex_unlock(&pvc_manager.lock);
        return -2;  // Already exists
    }
    
    // Check capacity
    if (pvc_manager.claim_count >= 500) {
        pthread_mutex_unlock(&pvc_manager.lock);
        return -3;  // Capacity exceeded
    }
    
    // Add new PVC
    persistent_volume_claim_t* new_pvc = &pvc_manager.claims[pvc_manager.claim_count];
    *new_pvc = *pvc;
    new_pvc->created_at = time(NULL);
    new_pvc->phase = PVC_PHASE_PENDING;
    new_pvc->bound_pv_name[0] = '\0';
    new_pvc->bound_at = 0;
    new_pvc->binding_attempt_count = 0;
    new_pvc->last_binding_attempt = 0;
    
    pvc_manager.claim_count++;
    
    pthread_mutex_unlock(&pvc_manager.lock);
    return 0;  // Success
}

// Get a PersistentVolumeClaim
int pvc_get(const char* namespace, const char* name, persistent_volume_claim_t* out) {
    if (!namespace || !name || !out) {
        return -1;
    }
    
    pthread_mutex_lock(&pvc_manager.lock);
    
    int idx = find_pvc_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&pvc_manager.lock);
        return -2;  // Not found
    }
    
    *out = pvc_manager.claims[idx];
    
    pthread_mutex_unlock(&pvc_manager.lock);
    return 0;  // Success
}

// Update a PersistentVolumeClaim
int pvc_update(const char* namespace, const char* name, const persistent_volume_claim_t* updated) {
    if (!namespace || !name || !updated) {
        return -1;
    }
    
    pthread_mutex_lock(&pvc_manager.lock);
    
    int idx = find_pvc_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&pvc_manager.lock);
        return -2;  // Not found
    }
    
    // Preserve immutable fields
    persistent_volume_claim_t current = pvc_manager.claims[idx];
    
    pvc_manager.claims[idx] = *updated;
    pvc_manager.claims[idx].created_at = current.created_at;
    pvc_manager.claims[idx].bound_at = current.bound_at;
    
    pthread_mutex_unlock(&pvc_manager.lock);
    return 0;  // Success
}

// Delete a PersistentVolumeClaim
int pvc_delete(const char* namespace, const char* name) {
    if (!namespace || !name) {
        return -1;
    }
    
    pthread_mutex_lock(&pvc_manager.lock);
    
    int idx = find_pvc_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&pvc_manager.lock);
        return -2;  // Not found
    }
    
    // When PVC is deleted, unbind from PV
    persistent_volume_claim_t* pvc = &pvc_manager.claims[idx];
    if (pvc->phase == PVC_PHASE_BOUND && pvc->bound_pv_name[0] != '\0') {
        // In real implementation, would call pv_unbind_from_pvc here
        // For now, just mark as unbound
    }
    
    // Shift remaining claims
    for (int i = idx; i < pvc_manager.claim_count - 1; i++) {
        pvc_manager.claims[i] = pvc_manager.claims[i + 1];
    }
    pvc_manager.claim_count--;
    
    pthread_mutex_unlock(&pvc_manager.lock);
    return 0;  // Success
}

// List PersistentVolumeClaims in namespace
int pvc_list(const char* namespace, persistent_volume_claim_t* out_array, int max_count) {
    if (!namespace || !out_array) {
        return -1;
    }
    
    pthread_mutex_lock(&pvc_manager.lock);
    
    int count = 0;
    for (int i = 0; i < pvc_manager.claim_count && count < max_count; i++) {
        if (strcmp(pvc_manager.claims[i].namespace, namespace) == 0) {
            out_array[count++] = pvc_manager.claims[i];
        }
    }
    
    pthread_mutex_unlock(&pvc_manager.lock);
    return count;
}

// Count PVCs in namespace
int pvc_list_count(const char* namespace) {
    if (!namespace) {
        return -1;
    }
    
    pthread_mutex_lock(&pvc_manager.lock);
    
    int count = 0;
    for (int i = 0; i < pvc_manager.claim_count; i++) {
        if (strcmp(pvc_manager.claims[i].namespace, namespace) == 0) {
            count++;
        }
    }
    
    pthread_mutex_unlock(&pvc_manager.lock);
    return count;
}

// Set PVC phase
int pvc_set_phase(const char* namespace, const char* name, pvc_phase_t phase) {
    if (!namespace || !name) {
        return -1;
    }
    
    pthread_mutex_lock(&pvc_manager.lock);
    
    int idx = find_pvc_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&pvc_manager.lock);
        return -2;  // Not found
    }
    
    pvc_manager.claims[idx].phase = phase;
    
    pthread_mutex_unlock(&pvc_manager.lock);
    return 0;
}

// Get PVC phase
int pvc_get_phase(const char* namespace, const char* name) {
    if (!namespace || !name) {
        return -1;
    }
    
    pthread_mutex_lock(&pvc_manager.lock);
    
    int idx = find_pvc_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&pvc_manager.lock);
        return -2;  // Not found
    }
    
    int phase = pvc_manager.claims[idx].phase;
    pthread_mutex_unlock(&pvc_manager.lock);
    
    return phase;
}

// Check if PVC is bound
int pvc_is_bound(const char* namespace, const char* name) {
    if (!namespace || !name) {
        return 0;
    }
    
    pthread_mutex_lock(&pvc_manager.lock);
    
    int idx = find_pvc_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&pvc_manager.lock);
        return 0;
    }
    
    int bound = (pvc_manager.claims[idx].phase == PVC_PHASE_BOUND) ? 1 : 0;
    pthread_mutex_unlock(&pvc_manager.lock);
    
    return bound;
}

// Bind PVC to PV
int pvc_bind_to_pv(const char* namespace, const char* pvc_name, const char* pv_name) {
    if (!namespace || !pvc_name || !pv_name) {
        return -1;
    }
    
    pthread_mutex_lock(&pvc_manager.lock);
    
    int idx = find_pvc_index(namespace, pvc_name);
    if (idx < 0) {
        pthread_mutex_unlock(&pvc_manager.lock);
        return -2;  // PVC not found
    }
    
    persistent_volume_claim_t* pvc = &pvc_manager.claims[idx];
    
    // Can only bind from PENDING phase
    if (pvc->phase != PVC_PHASE_PENDING) {
        pthread_mutex_unlock(&pvc_manager.lock);
        return -3;  // Not in pending state
    }
    
    // Perform binding
    strncpy(pvc->bound_pv_name, pv_name, sizeof(pvc->bound_pv_name) - 1);
    pvc->phase = PVC_PHASE_BOUND;
    pvc->bound_at = time(NULL);
    
    pthread_mutex_unlock(&pvc_manager.lock);
    return 0;  // Success
}

// Unbind PVC from PV
int pvc_unbind_from_pv(const char* namespace, const char* pvc_name) {
    if (!namespace || !pvc_name) {
        return -1;
    }
    
    pthread_mutex_lock(&pvc_manager.lock);
    
    int idx = find_pvc_index(namespace, pvc_name);
    if (idx < 0) {
        pthread_mutex_unlock(&pvc_manager.lock);
        return -2;  // Not found
    }
    
    persistent_volume_claim_t* pvc = &pvc_manager.claims[idx];
    pvc->bound_pv_name[0] = '\0';
    pvc->phase = PVC_PHASE_LOST;  // Transition to LOST when PV disappears
    pvc->bound_at = 0;
    
    pthread_mutex_unlock(&pvc_manager.lock);
    return 0;
}

// Get bound PV name
int pvc_get_bound_pv(const char* namespace, const char* name, char* out_pv_name) {
    if (!namespace || !name || !out_pv_name) {
        return -1;
    }
    
    pthread_mutex_lock(&pvc_manager.lock);
    
    int idx = find_pvc_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&pvc_manager.lock);
        return -2;  // Not found
    }
    
    persistent_volume_claim_t* pvc = &pvc_manager.claims[idx];
    
    if (pvc->phase != PVC_PHASE_BOUND) {
        pthread_mutex_unlock(&pvc_manager.lock);
        return -3;  // Not bound
    }
    
    strncpy(out_pv_name, pvc->bound_pv_name, 255);
    out_pv_name[255] = '\0';
    
    pthread_mutex_unlock(&pvc_manager.lock);
    return 0;
}

// Get requested capacity
long long pvc_get_requested_capacity(const char* namespace, const char* name) {
    if (!namespace || !name) {
        return -1;
    }
    
    pthread_mutex_lock(&pvc_manager.lock);
    
    int idx = find_pvc_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&pvc_manager.lock);
        return -1;  // Not found
    }
    
    long long capacity = pvc_manager.claims[idx].requested_capacity_bytes;
    pthread_mutex_unlock(&pvc_manager.lock);
    
    return capacity;
}

// Check access mode support
int pvc_supports_access_mode(const char* namespace, const char* name, pv_access_mode_t mode) {
    if (!namespace || !name) {
        return 0;
    }
    
    pthread_mutex_lock(&pvc_manager.lock);
    
    int idx = find_pvc_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&pvc_manager.lock);
        return 0;
    }
    
    int supports = (pvc_manager.claims[idx].access_modes & mode) ? 1 : 0;
    pthread_mutex_unlock(&pvc_manager.lock);
    
    return supports;
}

// Find matching PV for this PVC
int pvc_find_matching_pv(const char* namespace, const char* pvc_name,
                         pvc_binding_result_t* out_result) {
    if (!namespace || !pvc_name || !out_result) {
        return -1;
    }
    
    persistent_volume_claim_t pvc;
    if (pvc_get(namespace, pvc_name, &pvc) < 0) {
        out_result->found = 0;
        strncpy(out_result->reason, "PVC not found", sizeof(out_result->reason) - 1);
        return -2;
    }
    
    // Find an available PV that matches PVC requirements
    persistent_volume_t pv;
    int pv_count = pv_list_count();
    
    for (int i = 0; i < pv_count; i++) {
        // In real implementation, would use pv_list with iteration
        // For now, match by checking availability and capacity
        
        // Check if PV is available
        if (!pv_is_available(pv.name)) {
            continue;
        }
        
        // Check capacity
        long long pv_capacity = pv_get_capacity(pv.name);
        if (pv_capacity < pvc.requested_capacity_bytes) {
            continue;
        }
        
        // Check access modes compatibility
        if ((pv.access_modes & pvc.access_modes) != pvc.access_modes) {
            continue;
        }
        
        // Found a match!
        out_result->found = 1;
        strncpy(out_result->matched_pv_name, pv.name, sizeof(out_result->matched_pv_name) - 1);
        out_result->reason[0] = '\0';
        
        return 0;  // Success
    }
    
    // No matching PV found
    out_result->found = 0;
    snprintf(out_result->reason, sizeof(out_result->reason),
             "No available PV with capacity %lld and required access modes",
             pvc.requested_capacity_bytes);
    
    return 0;  // Not an error, just no match
}

// Increment binding attempts
int pvc_increment_binding_attempts(const char* namespace, const char* name) {
    if (!namespace || !name) {
        return -1;
    }
    
    pthread_mutex_lock(&pvc_manager.lock);
    
    int idx = find_pvc_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&pvc_manager.lock);
        return -2;  // Not found
    }
    
    pvc_manager.claims[idx].binding_attempt_count++;
    pvc_manager.claims[idx].last_binding_attempt = time(NULL);
    
    pthread_mutex_unlock(&pvc_manager.lock);
    return 0;
}

// Get binding attempts
int pvc_get_binding_attempts(const char* namespace, const char* name) {
    if (!namespace || !name) {
        return -1;
    }
    
    pthread_mutex_lock(&pvc_manager.lock);
    
    int idx = find_pvc_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&pvc_manager.lock);
        return -1;  // Not found
    }
    
    int attempts = pvc_manager.claims[idx].binding_attempt_count;
    pthread_mutex_unlock(&pvc_manager.lock);
    
    return attempts;
}

// Check if PVC can be created (admission control)
int pvc_check_can_create(const char* namespace, const persistent_volume_claim_t* pvc,
                         pvc_admission_result_t* out_result) {
    if (!namespace || !pvc || !out_result) {
        return -1;
    }
    
    // Validate required fields
    if (!pvc->name || pvc->name[0] == '\0') {
        out_result->allowed = 0;
        strncpy(out_result->denial_reason, "PVC name is required", sizeof(out_result->denial_reason) - 1);
        return 0;  // Admission check complete
    }
    
    // Check capacity is reasonable
    if (pvc->requested_capacity_bytes <= 0) {
        out_result->allowed = 0;
        strncpy(out_result->denial_reason, "Capacity must be > 0", sizeof(out_result->denial_reason) - 1);
        return 0;
    }
    
    if (pvc->requested_capacity_bytes > 1LL * 1024 * 1024 * 1024 * 1024) {  // > 1 TB
        out_result->allowed = 0;
        snprintf(out_result->denial_reason, sizeof(out_result->denial_reason),
                 "Capacity exceeds max allowed (1TB)");
        return 0;
    }
    
    // Check access modes are valid
    if (pvc->access_modes == 0) {
        out_result->allowed = 0;
        strncpy(out_result->denial_reason, "At least one access mode is required", 
                sizeof(out_result->denial_reason) - 1);
        return 0;
    }
    
    // All checks passed
    out_result->allowed = 1;
    out_result->denial_reason[0] = '\0';
    out_result->requested_bytes = pvc->requested_capacity_bytes;
    out_result->available_bytes = 0;  // Would check actual available PVs
    
    return 0;
}

// Get status as JSON
int pvc_get_status(const char* namespace, const char* name, char* out_json_buffer, int buffer_size) {
    if (!namespace || !name || !out_json_buffer || buffer_size < 200) {
        return -1;
    }
    
    persistent_volume_claim_t pvc;
    if (pvc_get(namespace, name, &pvc) < 0) {
        return -2;
    }
    
    const char* phase_str = "";
    switch (pvc.phase) {
        case PVC_PHASE_PENDING:
            phase_str = "Pending";
            break;
        case PVC_PHASE_BOUND:
            phase_str = "Bound";
            break;
        case PVC_PHASE_LOST:
            phase_str = "Lost";
            break;
        case PVC_PHASE_FAILED:
            phase_str = "Failed";
            break;
    }
    
    snprintf(out_json_buffer, buffer_size,
        "{\"namespace\":\"%s\",\"name\":\"%s\",\"capacity\":%lld,\"phase\":\"%s\","
        "\"bound_pv\":\"%s\",\"created_at\":%ld}",
        pvc.namespace, pvc.name, pvc.requested_capacity_bytes, phase_str,
        pvc.bound_pv_name, pvc.created_at);
    
    return 0;
}

// Format list as JSON
int pvc_format_list_json(const char* namespace, char* out_json_buffer, int buffer_size) {
    if (!namespace || !out_json_buffer || buffer_size < 100) {
        return -1;
    }
    
    persistent_volume_claim_t pvc_array[500];
    int count = pvc_list(namespace, pvc_array, 500);
    
    int offset = 0;
    offset += snprintf(out_json_buffer + offset, buffer_size - offset, "[");
    
    for (int i = 0; i < count && offset < buffer_size - 50; i++) {
        if (i > 0) {
            offset += snprintf(out_json_buffer + offset, buffer_size - offset, ",");
        }
        
        persistent_volume_claim_t* pvc = &pvc_array[i];
        const char* phase_str = "";
        switch (pvc->phase) {
            case PVC_PHASE_PENDING: phase_str = "Pending"; break;
            case PVC_PHASE_BOUND: phase_str = "Bound"; break;
            case PVC_PHASE_LOST: phase_str = "Lost"; break;
            case PVC_PHASE_FAILED: phase_str = "Failed"; break;
        }
        
        offset += snprintf(out_json_buffer + offset, buffer_size - offset,
            "{\"name\":\"%s\",\"capacity\":%lld,\"phase\":\"%s\",\"bound_pv\":\"%s\"}",
            pvc->name, pvc->requested_capacity_bytes, phase_str, pvc->bound_pv_name);
    }
    
    offset += snprintf(out_json_buffer + offset, buffer_size - offset, "]");
    
    return 0;
}

// Format phase as string
int pvc_format_phase_string(pvc_phase_t phase, char* out_string) {
    if (!out_string) {
        return -1;
    }
    
    const char* phase_str = "";
    switch (phase) {
        case PVC_PHASE_PENDING:
            phase_str = "Pending";
            break;
        case PVC_PHASE_BOUND:
            phase_str = "Bound";
            break;
        case PVC_PHASE_LOST:
            phase_str = "Lost";
            break;
        case PVC_PHASE_FAILED:
            phase_str = "Failed";
            break;
        default:
            phase_str = "Unknown";
    }
    
    strncpy(out_string, phase_str, 63);
    out_string[63] = '\0';
    
    return 0;
}
