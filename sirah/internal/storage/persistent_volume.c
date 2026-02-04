#include "persistent_volume.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

// Global PV manager singleton
static pv_manager_t pv_manager = {
    .initialized = 0
};

// Initialize PV manager
int pv_manager_init(void) {
    if (pv_manager.initialized) {
        return 0;  // Already initialized
    }
    
    pthread_mutex_init(&pv_manager.lock, NULL);
    pv_manager.volume_count = 0;
    pv_manager.initialized = 1;
    
    return 0;
}

// Shutdown PV manager
int pv_manager_shutdown(void) {
    if (!pv_manager.initialized) {
        return 0;
    }
    
    pthread_mutex_lock(&pv_manager.lock);
    
    // Cleanup
    pv_manager.volume_count = 0;
    pv_manager.initialized = 0;
    
    pthread_mutex_unlock(&pv_manager.lock);
    pthread_mutex_destroy(&pv_manager.lock);
    
    return 0;
}

// Helper to find PV by name
static int find_pv_index(const char* name) {
    for (int i = 0; i < pv_manager.volume_count; i++) {
        if (strcmp(pv_manager.volumes[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// Create a new PersistentVolume
int pv_create(const persistent_volume_t* pv) {
    if (!pv || !pv->name || pv->name[0] == '\0') {
        return -1;  // Invalid input
    }
    
    pthread_mutex_lock(&pv_manager.lock);
    
    // Check if PV already exists
    if (find_pv_index(pv->name) >= 0) {
        pthread_mutex_unlock(&pv_manager.lock);
        return -2;  // Already exists
    }
    
    // Check capacity
    if (pv_manager.volume_count >= 500) {
        pthread_mutex_unlock(&pv_manager.lock);
        return -3;  // Capacity exceeded
    }
    
    // Add new PV
    persistent_volume_t* new_pv = &pv_manager.volumes[pv_manager.volume_count];
    *new_pv = *pv;
    new_pv->created_at = time(NULL);
    new_pv->phase = PV_PHASE_AVAILABLE;
    new_pv->bound_pvc_namespace[0] = '\0';
    new_pv->bound_pvc_name[0] = '\0';
    new_pv->bound_at = 0;
    new_pv->claim_ref_generation = 0;
    
    pv_manager.volume_count++;
    
    pthread_mutex_unlock(&pv_manager.lock);
    return 0;  // Success
}

// Get a PersistentVolume
int pv_get(const char* name, persistent_volume_t* out) {
    if (!name || !out) {
        return -1;
    }
    
    pthread_mutex_lock(&pv_manager.lock);
    
    int idx = find_pv_index(name);
    if (idx < 0) {
        pthread_mutex_unlock(&pv_manager.lock);
        return -2;  // Not found
    }
    
    *out = pv_manager.volumes[idx];
    
    pthread_mutex_unlock(&pv_manager.lock);
    return 0;  // Success
}

// Update a PersistentVolume
int pv_update(const char* name, const persistent_volume_t* updated) {
    if (!name || !updated) {
        return -1;
    }
    
    pthread_mutex_lock(&pv_manager.lock);
    
    int idx = find_pv_index(name);
    if (idx < 0) {
        pthread_mutex_unlock(&pv_manager.lock);
        return -2;  // Not found
    }
    
    // Preserve some immutable fields
    persistent_volume_t current = pv_manager.volumes[idx];
    
    pv_manager.volumes[idx] = *updated;
    pv_manager.volumes[idx].created_at = current.created_at;
    pv_manager.volumes[idx].bound_at = current.bound_at;  // Don't change if already bound
    
    pthread_mutex_unlock(&pv_manager.lock);
    return 0;  // Success
}

// Delete a PersistentVolume
int pv_delete(const char* name) {
    if (!name) {
        return -1;
    }
    
    pthread_mutex_lock(&pv_manager.lock);
    
    int idx = find_pv_index(name);
    if (idx < 0) {
        pthread_mutex_unlock(&pv_manager.lock);
        return -2;  // Not found
    }
    
    // Cannot delete if bound to PVC
    persistent_volume_t* pv = &pv_manager.volumes[idx];
    if (pv->phase == PV_PHASE_BOUND && pv->bound_pvc_name[0] != '\0') {
        pthread_mutex_unlock(&pv_manager.lock);
        return -3;  // Still bound
    }
    
    // Shift remaining volumes
    for (int i = idx; i < pv_manager.volume_count - 1; i++) {
        pv_manager.volumes[i] = pv_manager.volumes[i + 1];
    }
    pv_manager.volume_count--;
    
    pthread_mutex_unlock(&pv_manager.lock);
    return 0;  // Success
}

// List all PersistentVolumes
int pv_list(persistent_volume_t* out_array, int max_count) {
    if (!out_array) {
        return -1;
    }
    
    pthread_mutex_lock(&pv_manager.lock);
    
    int count = pv_manager.volume_count < max_count ? pv_manager.volume_count : max_count;
    for (int i = 0; i < count; i++) {
        out_array[i] = pv_manager.volumes[i];
    }
    
    pthread_mutex_unlock(&pv_manager.lock);
    return count;
}

// Get total PV count
int pv_list_count(void) {
    pthread_mutex_lock(&pv_manager.lock);
    int count = pv_manager.volume_count;
    pthread_mutex_unlock(&pv_manager.lock);
    return count;
}

// Set PV phase
int pv_set_phase(const char* name, pv_phase_t phase) {
    if (!name) {
        return -1;
    }
    
    pthread_mutex_lock(&pv_manager.lock);
    
    int idx = find_pv_index(name);
    if (idx < 0) {
        pthread_mutex_unlock(&pv_manager.lock);
        return -2;  // Not found
    }
    
    pv_manager.volumes[idx].phase = phase;
    
    pthread_mutex_unlock(&pv_manager.lock);
    return 0;
}

// Get PV phase
int pv_get_phase(const char* name) {
    if (!name) {
        return -1;
    }
    
    pthread_mutex_lock(&pv_manager.lock);
    
    int idx = find_pv_index(name);
    if (idx < 0) {
        pthread_mutex_unlock(&pv_manager.lock);
        return -2;  // Not found (encoded as negative)
    }
    
    int phase = pv_manager.volumes[idx].phase;
    pthread_mutex_unlock(&pv_manager.lock);
    
    return phase;
}

// Check if PV is available
int pv_is_available(const char* name) {
    if (!name) {
        return 0;
    }
    
    pthread_mutex_lock(&pv_manager.lock);
    
    int idx = find_pv_index(name);
    if (idx < 0) {
        pthread_mutex_unlock(&pv_manager.lock);
        return 0;  // Not found
    }
    
    int available = (pv_manager.volumes[idx].phase == PV_PHASE_AVAILABLE) ? 1 : 0;
    pthread_mutex_unlock(&pv_manager.lock);
    
    return available;
}

// Bind PV to PVC
int pv_bind_to_pvc(const char* pv_name, const char* pvc_namespace, const char* pvc_name) {
    if (!pv_name || !pvc_namespace || !pvc_name) {
        return -1;
    }
    
    pthread_mutex_lock(&pv_manager.lock);
    
    int idx = find_pv_index(pv_name);
    if (idx < 0) {
        pthread_mutex_unlock(&pv_manager.lock);
        return -2;  // PV not found
    }
    
    persistent_volume_t* pv = &pv_manager.volumes[idx];
    
    // Can only bind from AVAILABLE or RELEASED state
    if (pv->phase != PV_PHASE_AVAILABLE && pv->phase != PV_PHASE_RELEASED) {
        pthread_mutex_unlock(&pv_manager.lock);
        return -3;  // Not available for binding
    }
    
    // Perform binding
    strncpy(pv->bound_pvc_namespace, pvc_namespace, sizeof(pv->bound_pvc_namespace) - 1);
    strncpy(pv->bound_pvc_name, pvc_name, sizeof(pv->bound_pvc_name) - 1);
    pv->phase = PV_PHASE_BOUND;
    pv->bound_at = time(NULL);
    pv->claim_ref_generation++;
    
    pthread_mutex_unlock(&pv_manager.lock);
    return 0;  // Success
}

// Unbind PV from PVC
int pv_unbind_from_pvc(const char* pv_name) {
    if (!pv_name) {
        return -1;
    }
    
    pthread_mutex_lock(&pv_manager.lock);
    
    int idx = find_pv_index(pv_name);
    if (idx < 0) {
        pthread_mutex_unlock(&pv_manager.lock);
        return -2;  // Not found
    }
    
    persistent_volume_t* pv = &pv_manager.volumes[idx];
    
    if (pv->phase == PV_PHASE_BOUND) {
        // Transition to RELEASED based on reclaim policy
        if (pv->reclaim_policy == RECLAIM_POLICY_DELETE) {
            pv->phase = PV_PHASE_RELEASED;
        } else if (pv->reclaim_policy == RECLAIM_POLICY_RETAIN) {
            pv->phase = PV_PHASE_RELEASED;
        } else if (pv->reclaim_policy == RECLAIM_POLICY_RECYCLE) {
            pv->phase = PV_PHASE_RELEASED;
        }
    }
    
    pv->bound_pvc_namespace[0] = '\0';
    pv->bound_pvc_name[0] = '\0';
    pv->bound_at = 0;
    
    pthread_mutex_unlock(&pv_manager.lock);
    return 0;
}

// Get bound PVC name
int pv_get_bound_pvc(const char* pv_name, char* out_namespace, char* out_pvc_name) {
    if (!pv_name || !out_namespace || !out_pvc_name) {
        return -1;
    }
    
    pthread_mutex_lock(&pv_manager.lock);
    
    int idx = find_pv_index(pv_name);
    if (idx < 0) {
        pthread_mutex_unlock(&pv_manager.lock);
        return -2;  // Not found
    }
    
    persistent_volume_t* pv = &pv_manager.volumes[idx];
    
    if (pv->phase != PV_PHASE_BOUND) {
        pthread_mutex_unlock(&pv_manager.lock);
        return -3;  // Not bound
    }
    
    strncpy(out_namespace, pv->bound_pvc_namespace, 63);
    out_namespace[63] = '\0';
    strncpy(out_pvc_name, pv->bound_pvc_name, 255);
    out_pvc_name[255] = '\0';
    
    pthread_mutex_unlock(&pv_manager.lock);
    return 0;
}

// Get capacity
long long pv_get_capacity(const char* name) {
    if (!name) {
        return -1;
    }
    
    pthread_mutex_lock(&pv_manager.lock);
    
    int idx = find_pv_index(name);
    if (idx < 0) {
        pthread_mutex_unlock(&pv_manager.lock);
        return -1;  // Not found
    }
    
    long long capacity = pv_manager.volumes[idx].capacity_bytes;
    pthread_mutex_unlock(&pv_manager.lock);
    
    return capacity;
}

// Check access mode support
int pv_supports_access_mode(const char* name, pv_access_mode_t mode) {
    if (!name) {
        return 0;
    }
    
    pthread_mutex_lock(&pv_manager.lock);
    
    int idx = find_pv_index(name);
    if (idx < 0) {
        pthread_mutex_unlock(&pv_manager.lock);
        return 0;
    }
    
    int supports = (pv_manager.volumes[idx].access_modes & mode) ? 1 : 0;
    pthread_mutex_unlock(&pv_manager.lock);
    
    return supports;
}

// Check RWO support
int pv_supports_read_write_once(const char* name) {
    return pv_supports_access_mode(name, ACCESS_MODE_READ_WRITE_ONCE);
}

// Check ROX support
int pv_supports_read_only_many(const char* name) {
    return pv_supports_access_mode(name, ACCESS_MODE_READ_ONLY_MANY);
}

// Check RWX support
int pv_supports_read_write_many(const char* name) {
    return pv_supports_access_mode(name, ACCESS_MODE_READ_WRITE_MANY);
}

// Create hostPath directory
int pv_create_hostpath_directory(const char* pv_name, const char* path) {
    if (!pv_name || !path) {
        return -1;
    }
    
    // Try to create directory with mode 0755
    if (mkdir(path, 0755) < 0) {
        // EEXIST is OK, means directory already exists
        if (errno == 17) {  // EEXIST
            return 0;
        }
        return -2;  // Failed to create
    }
    
    return 0;  // Success
}

// Delete hostPath directory
int pv_delete_hostpath_directory(const char* pv_name) {
    if (!pv_name) {
        return -1;
    }
    
    pthread_mutex_lock(&pv_manager.lock);
    
    int idx = find_pv_index(pv_name);
    if (idx < 0) {
        pthread_mutex_unlock(&pv_manager.lock);
        return -2;  // Not found
    }
    
    persistent_volume_t* pv = &pv_manager.volumes[idx];
    
    if (pv->backend.type != STORAGE_BACKEND_HOST_PATH) {
        pthread_mutex_unlock(&pv_manager.lock);
        return -3;  // Not hostPath
    }
    
    const char* path = pv->backend.config.hostpath.path;
    pthread_mutex_unlock(&pv_manager.lock);
    
    // Note: actual deletion would require recursive rmdir/unlink
    // For now, just return success (cleanup can be manual)
    return 0;
}

// Verify storage available
int pv_verify_storage_available(const char* pv_name) {
    if (!pv_name) {
        return -1;
    }
    
    pthread_mutex_lock(&pv_manager.lock);
    
    int idx = find_pv_index(pv_name);
    if (idx < 0) {
        pthread_mutex_unlock(&pv_manager.lock);
        return -2;  // Not found
    }
    
    persistent_volume_t* pv = &pv_manager.volumes[idx];
    
    if (pv->backend.type == STORAGE_BACKEND_HOST_PATH) {
        const char* path = pv->backend.config.hostpath.path;
        struct stat st;
        if (stat(path, &st) < 0) {
            pthread_mutex_unlock(&pv_manager.lock);
            return -3;  // Path doesn't exist
        }
        if (!S_ISDIR(st.st_mode)) {
            pthread_mutex_unlock(&pv_manager.lock);
            return -4;  // Not a directory
        }
    }
    
    pthread_mutex_unlock(&pv_manager.lock);
    return 0;  // Available
}

// Reclaim PV
int pv_reclaim(const char* pv_name) {
    if (!pv_name) {
        return -1;
    }
    
    pthread_mutex_lock(&pv_manager.lock);
    
    int idx = find_pv_index(pv_name);
    if (idx < 0) {
        pthread_mutex_unlock(&pv_manager.lock);
        return -2;  // Not found
    }
    
    persistent_volume_t* pv = &pv_manager.volumes[idx];
    
    if (pv->reclaim_policy == RECLAIM_POLICY_DELETE) {
        // Actually delete the volume
        pv->phase = PV_PHASE_RELEASED;
    } else if (pv->reclaim_policy == RECLAIM_POLICY_RETAIN) {
        // Manual intervention required
        pv->phase = PV_PHASE_RELEASED;
    } else if (pv->reclaim_policy == RECLAIM_POLICY_RECYCLE) {
        // Clean and recycle (deprecated)
        pv->phase = PV_PHASE_AVAILABLE;
    }
    
    pv->bound_pvc_namespace[0] = '\0';
    pv->bound_pvc_name[0] = '\0';
    
    pthread_mutex_unlock(&pv_manager.lock);
    return 0;
}

// Get reclaim policy
int pv_get_reclaim_policy(const char* name) {
    if (!name) {
        return -1;
    }
    
    pthread_mutex_lock(&pv_manager.lock);
    
    int idx = find_pv_index(name);
    if (idx < 0) {
        pthread_mutex_unlock(&pv_manager.lock);
        return -2;  // Not found
    }
    
    int policy = pv_manager.volumes[idx].reclaim_policy;
    pthread_mutex_unlock(&pv_manager.lock);
    
    return policy;
}

// Get status as JSON
int pv_get_status(const char* name, char* out_json_buffer, int buffer_size) {
    if (!name || !out_json_buffer || buffer_size < 200) {
        return -1;
    }
    
    persistent_volume_t pv;
    if (pv_get(name, &pv) < 0) {
        return -2;
    }
    
    const char* phase_str = "";
    switch (pv.phase) {
        case PV_PHASE_AVAILABLE:
            phase_str = "Available";
            break;
        case PV_PHASE_BOUND:
            phase_str = "Bound";
            break;
        case PV_PHASE_RELEASED:
            phase_str = "Released";
            break;
        case PV_PHASE_FAILED:
            phase_str = "Failed";
            break;
        case PV_PHASE_PENDING:
            phase_str = "Pending";
            break;
    }
    
    snprintf(out_json_buffer, buffer_size,
        "{\"name\":\"%s\",\"capacity\":%lld,\"phase\":\"%s\",\"bound_pvc\":\"%s/%s\","
        "\"reclaim_policy\":%d,\"created_at\":%ld}",
        pv.name, pv.capacity_bytes, phase_str,
        pv.bound_pvc_namespace, pv.bound_pvc_name,
        pv.reclaim_policy, pv.created_at);
    
    return 0;
}

// Format list as JSON
int pv_format_list_json(char* out_json_buffer, int buffer_size) {
    if (!out_json_buffer || buffer_size < 100) {
        return -1;
    }
    
    pthread_mutex_lock(&pv_manager.lock);
    
    int offset = 0;
    offset += snprintf(out_json_buffer + offset, buffer_size - offset, "[");
    
    for (int i = 0; i < pv_manager.volume_count && offset < buffer_size - 50; i++) {
        if (i > 0) {
            offset += snprintf(out_json_buffer + offset, buffer_size - offset, ",");
        }
        
        persistent_volume_t* pv = &pv_manager.volumes[i];
        const char* phase_str = "";
        switch (pv->phase) {
            case PV_PHASE_AVAILABLE: phase_str = "Available"; break;
            case PV_PHASE_BOUND: phase_str = "Bound"; break;
            case PV_PHASE_RELEASED: phase_str = "Released"; break;
            case PV_PHASE_FAILED: phase_str = "Failed"; break;
            case PV_PHASE_PENDING: phase_str = "Pending"; break;
        }
        
        offset += snprintf(out_json_buffer + offset, buffer_size - offset,
            "{\"name\":\"%s\",\"capacity\":%lld,\"phase\":\"%s\"}",
            pv->name, pv->capacity_bytes, phase_str);
    }
    
    offset += snprintf(out_json_buffer + offset, buffer_size - offset, "]");
    
    pthread_mutex_unlock(&pv_manager.lock);
    return 0;
}

// Format phase as string
int pv_format_phase_string(pv_phase_t phase, char* out_string) {
    if (!out_string) {
        return -1;
    }
    
    const char* phase_str = "";
    switch (phase) {
        case PV_PHASE_AVAILABLE:
            phase_str = "Available";
            break;
        case PV_PHASE_BOUND:
            phase_str = "Bound";
            break;
        case PV_PHASE_RELEASED:
            phase_str = "Released";
            break;
        case PV_PHASE_FAILED:
            phase_str = "Failed";
            break;
        case PV_PHASE_PENDING:
            phase_str = "Pending";
            break;
        default:
            phase_str = "Unknown";
    }
    
    strncpy(out_string, phase_str, 63);
    out_string[63] = '\0';
    
    return 0;
}
