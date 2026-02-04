#include "storage_class.h"
#include "persistent_volume.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

// Global StorageClass manager singleton
static sc_manager_t sc_manager = {
    .initialized = 0
};

// Initialize StorageClass manager
int sc_manager_init(void) {
    if (sc_manager.initialized) {
        return 0;
    }
    
    pthread_mutex_init(&sc_manager.lock, NULL);
    sc_manager.class_count = 0;
    sc_manager.initialized = 1;
    
    // Create default storage class if not exists
    storage_class_t default_sc = {
        .name = "default",
        .provisioner = {
            .provisioner = "sirah.io/hostpath",
            .allow_volume_expansion = 1
        },
        .reclaim_policy = RECLAIM_POLICY_DELETE,
        .binding_mode = BINDING_MODE_IMMEDIATE,
        .default_capacity_bytes = 10LL * 1024 * 1024 * 1024,  // 10 Gi
        .allowed_access_modes = ACCESS_MODE_READ_WRITE_ONCE,
        .allow_volume_expansion = 1,
        .is_default = 1
    };
    strncpy(default_sc.created_by, "system", sizeof(default_sc.created_by) - 1);
    default_sc.created_at = time(NULL);
    
    if (sc_create(&default_sc) < 0) {
        // Might already exist, that's OK
    }
    
    return 0;
}

// Shutdown StorageClass manager
int sc_manager_shutdown(void) {
    if (!sc_manager.initialized) {
        return 0;
    }
    
    pthread_mutex_lock(&sc_manager.lock);
    
    sc_manager.class_count = 0;
    sc_manager.initialized = 0;
    
    pthread_mutex_unlock(&sc_manager.lock);
    pthread_mutex_destroy(&sc_manager.lock);
    
    return 0;
}

// Helper to find StorageClass by name
static int find_sc_index(const char* name) {
    for (int i = 0; i < sc_manager.class_count; i++) {
        if (strcmp(sc_manager.classes[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// Create a new StorageClass
int sc_create(const storage_class_t* sc) {
    if (!sc || !sc->name || sc->name[0] == '\0') {
        return -1;  // Invalid input
    }
    
    pthread_mutex_lock(&sc_manager.lock);
    
    // Check if StorageClass already exists
    if (find_sc_index(sc->name) >= 0) {
        pthread_mutex_unlock(&sc_manager.lock);
        return -2;  // Already exists
    }
    
    // Check capacity
    if (sc_manager.class_count >= 100) {
        pthread_mutex_unlock(&sc_manager.lock);
        return -3;  // Capacity exceeded
    }
    
    // Add new StorageClass
    storage_class_t* new_sc = &sc_manager.classes[sc_manager.class_count];
    *new_sc = *sc;
    new_sc->created_at = time(NULL);
    
    // If this is marked as default, unset other defaults
    if (new_sc->is_default) {
        for (int i = 0; i < sc_manager.class_count; i++) {
            sc_manager.classes[i].is_default = 0;
        }
    }
    
    sc_manager.class_count++;
    
    pthread_mutex_unlock(&sc_manager.lock);
    return 0;  // Success
}

// Get a StorageClass
int sc_get(const char* name, storage_class_t* out) {
    if (!name || !out) {
        return -1;
    }
    
    pthread_mutex_lock(&sc_manager.lock);
    
    int idx = find_sc_index(name);
    if (idx < 0) {
        pthread_mutex_unlock(&sc_manager.lock);
        return -2;  // Not found
    }
    
    *out = sc_manager.classes[idx];
    
    pthread_mutex_unlock(&sc_manager.lock);
    return 0;  // Success
}

// Update a StorageClass
int sc_update(const char* name, const storage_class_t* updated) {
    if (!name || !updated) {
        return -1;
    }
    
    pthread_mutex_lock(&sc_manager.lock);
    
    int idx = find_sc_index(name);
    if (idx < 0) {
        pthread_mutex_unlock(&sc_manager.lock);
        return -2;  // Not found
    }
    
    // Preserve immutable fields
    storage_class_t current = sc_manager.classes[idx];
    
    sc_manager.classes[idx] = *updated;
    sc_manager.classes[idx].created_at = current.created_at;
    
    pthread_mutex_unlock(&sc_manager.lock);
    return 0;  // Success
}

// Delete a StorageClass
int sc_delete(const char* name) {
    if (!name) {
        return -1;
    }
    
    pthread_mutex_lock(&sc_manager.lock);
    
    int idx = find_sc_index(name);
    if (idx < 0) {
        pthread_mutex_unlock(&sc_manager.lock);
        return -2;  // Not found
    }
    
    // Cannot delete default storage class
    if (sc_manager.classes[idx].is_default) {
        pthread_mutex_unlock(&sc_manager.lock);
        return -3;  // Cannot delete default
    }
    
    // Shift remaining classes
    for (int i = idx; i < sc_manager.class_count - 1; i++) {
        sc_manager.classes[i] = sc_manager.classes[i + 1];
    }
    sc_manager.class_count--;
    
    pthread_mutex_unlock(&sc_manager.lock);
    return 0;  // Success
}

// List all StorageClasses
int sc_list(storage_class_t* out_array, int max_count) {
    if (!out_array) {
        return -1;
    }
    
    pthread_mutex_lock(&sc_manager.lock);
    
    int count = sc_manager.class_count < max_count ? sc_manager.class_count : max_count;
    for (int i = 0; i < count; i++) {
        out_array[i] = sc_manager.classes[i];
    }
    
    pthread_mutex_unlock(&sc_manager.lock);
    return count;
}

// Get StorageClass count
int sc_list_count(void) {
    pthread_mutex_lock(&sc_manager.lock);
    int count = sc_manager.class_count;
    pthread_mutex_unlock(&sc_manager.lock);
    return count;
}

// Set default StorageClass
int sc_set_default(const char* name) {
    if (!name) {
        return -1;
    }
    
    pthread_mutex_lock(&sc_manager.lock);
    
    int idx = find_sc_index(name);
    if (idx < 0) {
        pthread_mutex_unlock(&sc_manager.lock);
        return -2;  // Not found
    }
    
    // Unset all other defaults
    for (int i = 0; i < sc_manager.class_count; i++) {
        sc_manager.classes[i].is_default = 0;
    }
    
    // Set this as default
    sc_manager.classes[idx].is_default = 1;
    
    pthread_mutex_unlock(&sc_manager.lock);
    return 0;
}

// Get default StorageClass name
int sc_get_default(char* out_name) {
    if (!out_name) {
        return -1;
    }
    
    pthread_mutex_lock(&sc_manager.lock);
    
    for (int i = 0; i < sc_manager.class_count; i++) {
        if (sc_manager.classes[i].is_default) {
            strncpy(out_name, sc_manager.classes[i].name, 255);
            out_name[255] = '\0';
            pthread_mutex_unlock(&sc_manager.lock);
            return 0;
        }
    }
    
    // Fallback to "default" if available
    for (int i = 0; i < sc_manager.class_count; i++) {
        if (strcmp(sc_manager.classes[i].name, "default") == 0) {
            strncpy(out_name, "default", 255);
            pthread_mutex_unlock(&sc_manager.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&sc_manager.lock);
    return -2;  // No default found
}

// Check if StorageClass is default
int sc_is_default(const char* name) {
    if (!name) {
        return 0;
    }
    
    pthread_mutex_lock(&sc_manager.lock);
    
    int idx = find_sc_index(name);
    if (idx < 0) {
        pthread_mutex_unlock(&sc_manager.lock);
        return 0;
    }
    
    int is_default = sc_manager.classes[idx].is_default;
    pthread_mutex_unlock(&sc_manager.lock);
    
    return is_default;
}

// Generate PV name from SC, namespace, and PVC name
int sc_generate_pv_name(const char* sc_name, const char* pvc_namespace,
                        const char* pvc_name, char* out_pv_name, int buffer_size) {
    if (!sc_name || !pvc_namespace || !pvc_name || !out_pv_name || buffer_size < 100) {
        return -1;
    }
    
    // Format: pv-<namespace>-<pvcname>-<timestamp>
    time_t now = time(NULL);
    snprintf(out_pv_name, buffer_size, "pv-%s-%s-%ld",
             pvc_namespace, pvc_name, now % 1000000);
    
    return 0;
}

// Provision hostPath PV
int sc_provision_hostpath_pv(const char* sc_name, long long capacity_bytes,
                             pv_access_mode_t access_modes, const char* pvc_namespace,
                             const char* pvc_name, char* out_pv_name) {
    if (!sc_name || !pvc_namespace || !pvc_name || !out_pv_name) {
        return -1;
    }
    
    // Generate PV name
    char pv_name[256];
    if (sc_generate_pv_name(sc_name, pvc_namespace, pvc_name, pv_name, sizeof(pv_name)) < 0) {
        return -2;
    }
    
    // Create PV object
    persistent_volume_t pv = {
        .capacity_bytes = capacity_bytes,
        .access_modes = access_modes,
        .reclaim_policy = RECLAIM_POLICY_DELETE,  // From StorageClass
        .phase = PV_PHASE_AVAILABLE,
        .backend = {
            .type = STORAGE_BACKEND_HOST_PATH,
            .config = {
                .hostpath = {
                    .type = "DirectoryOrCreate"
                }
            }
        }
    };
    
    strncpy(pv.name, pv_name, sizeof(pv.name) - 1);
    strncpy(pv.created_by, sc_name, sizeof(pv.created_by) - 1);
    
    // Create host directory
    char host_path[512];
    snprintf(host_path, sizeof(host_path), "/var/sirah/volumes/%s", pv_name);
    strncpy(pv.backend.config.hostpath.path, host_path, sizeof(pv.backend.config.hostpath.path) - 1);
    
    if (mkdir(host_path, 0755) < 0 && errno != 17) {  // 17 = EEXIST
        return -3;  // Failed to create directory
    }
    
    // Create PV in manager
    if (pv_create(&pv) < 0) {
        return -4;  // Failed to create PV
    }
    
    // Return PV name
    strncpy(out_pv_name, pv_name, 255);
    out_pv_name[255] = '\0';
    
    return 0;  // Success
}

// Provision PV (dispatcher)
int sc_provision_pv(const char* sc_name, long long capacity_bytes,
                    pv_access_mode_t access_modes, const char* pvc_namespace,
                    const char* pvc_name, provisioning_result_t* out_result) {
    if (!sc_name || !pvc_namespace || !pvc_name || !out_result) {
        return -1;
    }
    
    storage_class_t sc;
    if (sc_get(sc_name, &sc) < 0) {
        out_result->success = 0;
        strncpy(out_result->error_message, "StorageClass not found", sizeof(out_result->error_message) - 1);
        return 0;
    }
    
    // Currently only support hostPath provisioner
    if (sc.provisioner.provisioner[0] == '\0' ||
        strcmp(sc.provisioner.provisioner, "sirah.io/hostpath") == 0 ||
        strcmp(sc.provisioner.provisioner, "manual") == 0) {
        
        // Use hostPath provisioning
        char pv_name[256];
        if (sc_provision_hostpath_pv(sc_name, capacity_bytes, access_modes,
                                     pvc_namespace, pvc_name, pv_name) == 0) {
            out_result->success = 1;
            strncpy(out_result->pv_name, pv_name, sizeof(out_result->pv_name) - 1);
            out_result->error_message[0] = '\0';
            return 0;
        } else {
            out_result->success = 0;
            strncpy(out_result->error_message, "Failed to provision hostPath PV", 
                    sizeof(out_result->error_message) - 1);
            return 0;
        }
    }
    
    // Unsupported provisioner
    out_result->success = 0;
    snprintf(out_result->error_message, sizeof(out_result->error_message),
             "Provisioner %s not supported", sc.provisioner.provisioner);
    return 0;
}

// Reclaim PV
int sc_reclaim_pv(const char* sc_name, const char* pv_name) {
    if (!sc_name || !pv_name) {
        return -1;
    }
    
    // Get the StorageClass to determine reclaim policy
    storage_class_t sc;
    if (sc_get(sc_name, &sc) < 0) {
        return -2;  // StorageClass not found
    }
    
    // Apply reclaim policy
    if (sc.reclaim_policy == RECLAIM_POLICY_DELETE) {
        // Delete the PV
        if (pv_delete(pv_name) < 0) {
            return -3;  // Failed to delete
        }
    } else if (sc.reclaim_policy == RECLAIM_POLICY_RETAIN) {
        // Keep the PV, just change phase
        if (pv_set_phase(pv_name, PV_PHASE_RELEASED) < 0) {
            return -4;
        }
    } else if (sc.reclaim_policy == RECLAIM_POLICY_RECYCLE) {
        // Reset the PV to available (deprecated, but supported)
        if (pv_set_phase(pv_name, PV_PHASE_AVAILABLE) < 0) {
            return -5;
        }
    }
    
    return 0;  // Success
}

// Get provisioner
int sc_get_provisioner(const char* name, char* out_provisioner) {
    if (!name || !out_provisioner) {
        return -1;
    }
    
    storage_class_t sc;
    if (sc_get(name, &sc) < 0) {
        return -2;  // Not found
    }
    
    strncpy(out_provisioner, sc.provisioner.provisioner, 255);
    out_provisioner[255] = '\0';
    
    return 0;
}

// Get reclaim policy
int sc_get_reclaim_policy(const char* name) {
    if (!name) {
        return -1;
    }
    
    storage_class_t sc;
    if (sc_get(name, &sc) < 0) {
        return -2;  // Not found
    }
    
    return sc.reclaim_policy;
}

// Get binding mode
int sc_get_binding_mode(const char* name) {
    if (!name) {
        return -1;
    }
    
    storage_class_t sc;
    if (sc_get(name, &sc) < 0) {
        return -2;  // Not found
    }
    
    return sc.binding_mode;
}

// Get default capacity
long long sc_get_default_capacity(const char* name) {
    if (!name) {
        return -1;
    }
    
    storage_class_t sc;
    if (sc_get(name, &sc) < 0) {
        return -1;  // Not found
    }
    
    // If no default capacity set, use 10 Gi
    if (sc.default_capacity_bytes == 0) {
        return 10LL * 1024 * 1024 * 1024;
    }
    
    return sc.default_capacity_bytes;
}

// Check access mode support
int sc_supports_access_mode(const char* name, pv_access_mode_t mode) {
    if (!name) {
        return 0;
    }
    
    storage_class_t sc;
    if (sc_get(name, &sc) < 0) {
        return 0;
    }
    
    return (sc.allowed_access_modes & mode) ? 1 : 0;
}

// Check if expansion allowed
int sc_allows_expansion(const char* name) {
    if (!name) {
        return 0;
    }
    
    storage_class_t sc;
    if (sc_get(name, &sc) < 0) {
        return 0;
    }
    
    return sc.allow_volume_expansion;
}

// Get status as JSON
int sc_get_status(const char* name, char* out_json_buffer, int buffer_size) {
    if (!name || !out_json_buffer || buffer_size < 200) {
        return -1;
    }
    
    storage_class_t sc;
    if (sc_get(name, &sc) < 0) {
        return -2;
    }
    
    const char* reclaim_str = "";
    switch (sc.reclaim_policy) {
        case RECLAIM_POLICY_DELETE: reclaim_str = "Delete"; break;
        case RECLAIM_POLICY_RETAIN: reclaim_str = "Retain"; break;
        case RECLAIM_POLICY_RECYCLE: reclaim_str = "Recycle"; break;
    }
    
    const char* binding_str = "";
    switch (sc.binding_mode) {
        case BINDING_MODE_IMMEDIATE: binding_str = "Immediate"; break;
        case BINDING_MODE_WAIT_FIRST_CONSUMER: binding_str = "WaitForFirstConsumer"; break;
    }
    
    snprintf(out_json_buffer, buffer_size,
        "{\"name\":\"%s\",\"provisioner\":\"%s\",\"reclaim_policy\":\"%s\","
        "\"binding_mode\":\"%s\",\"is_default\":%d,\"created_at\":%ld}",
        sc.name, sc.provisioner.provisioner, reclaim_str, binding_str,
        sc.is_default, sc.created_at);
    
    return 0;
}

// Format list as JSON
int sc_format_list_json(char* out_json_buffer, int buffer_size) {
    if (!out_json_buffer || buffer_size < 100) {
        return -1;
    }
    
    storage_class_t sc_array[100];
    int count = sc_list(sc_array, 100);
    
    int offset = 0;
    offset += snprintf(out_json_buffer + offset, buffer_size - offset, "[");
    
    for (int i = 0; i < count && offset < buffer_size - 50; i++) {
        if (i > 0) {
            offset += snprintf(out_json_buffer + offset, buffer_size - offset, ",");
        }
        
        storage_class_t* sc = &sc_array[i];
        offset += snprintf(out_json_buffer + offset, buffer_size - offset,
            "{\"name\":\"%s\",\"provisioner\":\"%s\",\"is_default\":%d}",
            sc->name, sc->provisioner.provisioner, sc->is_default);
    }
    
    offset += snprintf(out_json_buffer + offset, buffer_size - offset, "]");
    
    return 0;
}
