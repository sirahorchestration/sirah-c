#include "crd_manager.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// Global CRD manager instance
static k8s_crd_manager_t* g_crd_manager = NULL;

// ============ Initialization ============

k8s_crd_manager_t* k8s_crd_manager_new() {
    k8s_crd_manager_t* manager = calloc(1, sizeof(k8s_crd_manager_t));
    if (!manager) return NULL;
    
    manager->num_crds = 0;
    manager->custom_resources_store = calloc(1, sizeof(void*));
    
    return manager;
}

void k8s_crd_manager_free(k8s_crd_manager_t* manager) {
    if (!manager) return;
    
    for (int i = 0; i < manager->num_crds; i++) {
        if (manager->crds[i]) {
            k8s_crd_free(manager->crds[i]);
        }
    }
    
    if (manager->custom_resources_store) {
        free(manager->custom_resources_store);
    }
    
    free(manager);
}

// ============ CRD Registration ============

int k8s_crd_manager_register(k8s_crd_manager_t* manager, k8s_crd_t* crd) {
    if (!manager || !crd || !crd->metadata) return -1;
    
    if (manager->num_crds >= K8S_MAX_REGISTERED_CRDS) {
        return -1;  // CRD quota exceeded
    }
    
    // Check if CRD already registered
    if (k8s_crd_manager_get(manager, crd->metadata->name)) {
        return -1;  // CRD already exists
    }
    
    // Validate CRD has required fields
    if (!crd->spec || !crd->spec->group || !crd->spec->names) {
        return -1;
    }
    
    // Add default version if not present
    if (crd->spec->num_versions == 0) {
        k8s_crd_add_version(crd, "v1");
        k8s_crd_set_storage_version(crd, "v1");
    }
    
    // Set status to Established
    if (crd->status) {
        crd->status->accepted_names_plural = 1;
        
        // Add Established condition
        if (crd->status->num_conditions < K8S_MAX_CRD_CONDITIONS) {
            crd->status->conditions[crd->status->num_conditions] = calloc(1, sizeof(k8s_crd_condition_t));
            crd->status->conditions[crd->status->num_conditions]->type = strdup("Established");
            crd->status->conditions[crd->status->num_conditions]->status = strdup("True");
            crd->status->conditions[crd->status->num_conditions]->reason = strdup("InitialNamesAccepted");
            crd->status->num_conditions++;
        }
    }
    
    // Store CRD
    manager->crds[manager->num_crds] = crd;
    manager->num_crds++;
    
    return 0;
}

int k8s_crd_manager_unregister(k8s_crd_manager_t* manager, const char* crd_name) {
    if (!manager || !crd_name) return -1;
    
    // Find and remove CRD
    for (int i = 0; i < manager->num_crds; i++) {
        if (manager->crds[i] && manager->crds[i]->metadata && 
            strcmp(manager->crds[i]->metadata->name, crd_name) == 0) {
            
            // Delete all custom resources of this CRD
            // TODO: Implement deletion of associated custom resources
            
            k8s_crd_free(manager->crds[i]);
            
            // Shift remaining CRDs
            for (int j = i; j < manager->num_crds - 1; j++) {
                manager->crds[j] = manager->crds[j + 1];
            }
            manager->num_crds--;
            return 0;
        }
    }
    
    return -1;  // CRD not found
}

k8s_crd_t* k8s_crd_manager_get(k8s_crd_manager_t* manager, const char* crd_name) {
    if (!manager || !crd_name) return NULL;
    
    for (int i = 0; i < manager->num_crds; i++) {
        if (manager->crds[i] && manager->crds[i]->metadata && 
            strcmp(manager->crds[i]->metadata->name, crd_name) == 0) {
            return manager->crds[i];
        }
    }
    
    return NULL;
}

int k8s_crd_manager_list(k8s_crd_manager_t* manager, k8s_crd_t** crds, int* count) {
    if (!manager || !crds || !count) return -1;
    
    if (manager->num_crds > *count) {
        *count = manager->num_crds;
        return -1;  // Buffer too small
    }
    
    for (int i = 0; i < manager->num_crds; i++) {
        crds[i] = manager->crds[i];
    }
    
    *count = manager->num_crds;
    return 0;
}

int k8s_crd_manager_update(k8s_crd_manager_t* manager, k8s_crd_t* crd) {
    if (!manager || !crd || !crd->metadata) return -1;
    
    k8s_crd_t* existing = k8s_crd_manager_get(manager, crd->metadata->name);
    if (!existing) return -1;
    
    // Update metadata (timestamps, etc.)
    if (crd->metadata->uid) {
        if (existing->metadata->uid) free(existing->metadata->uid);
        existing->metadata->uid = strdup(crd->metadata->uid);
    }
    
    // Update spec (versions, scope, etc.)
    if (crd->spec) {
        if (crd->spec->num_versions > 0) {
            for (int i = 0; i < crd->spec->num_versions; i++) {
                k8s_crd_add_version(existing, crd->spec->versions[i]);
            }
        }
    }
    
    return 0;
}

// ============ Custom Resource CRUD ============

int k8s_custom_resource_create(k8s_crd_manager_t* manager, const char* crd_name, k8s_custom_resource_t* resource) {
    if (!manager || !crd_name || !resource) return -1;
    
    // Verify CRD exists
    k8s_crd_t* crd = k8s_crd_manager_get(manager, crd_name);
    if (!crd) return -1;
    
    // Validate resource against CRD schema
    if (resource->spec_json) {
        if (k8s_custom_resource_validate(manager, crd_name, resource->spec_json) != 0) {
            return -1;  // Validation failed
        }
    }
    
    // TODO: Store in etcd via storage layer
    // For now, this is a placeholder that validates the resource
    
    return 0;
}

k8s_custom_resource_t* k8s_custom_resource_get(k8s_crd_manager_t* manager, const char* crd_name, 
                                               const char* name, const char* namespace) {
    if (!manager || !crd_name || !name) return NULL;
    
    // Verify CRD exists
    k8s_crd_t* crd = k8s_crd_manager_get(manager, crd_name);
    if (!crd) return NULL;
    
    // TODO: Retrieve from etcd via storage layer
    return NULL;
}

int k8s_custom_resource_list(k8s_crd_manager_t* manager, const char* crd_name, const char* namespace,
                             k8s_custom_resource_t** resources, int* count) {
    if (!manager || !crd_name) return -1;
    
    // Verify CRD exists
    k8s_crd_t* crd = k8s_crd_manager_get(manager, crd_name);
    if (!crd) return -1;
    
    // TODO: List from etcd via storage layer
    if (count) *count = 0;
    return 0;
}

int k8s_custom_resource_update(k8s_crd_manager_t* manager, const char* crd_name, k8s_custom_resource_t* resource) {
    if (!manager || !crd_name || !resource) return -1;
    
    // Verify CRD exists
    k8s_crd_t* crd = k8s_crd_manager_get(manager, crd_name);
    if (!crd) return -1;
    
    // Validate resource against CRD schema
    if (resource->spec_json) {
        if (k8s_custom_resource_validate(manager, crd_name, resource->spec_json) != 0) {
            return -1;
        }
    }
    
    // TODO: Update in etcd via storage layer
    return 0;
}

int k8s_custom_resource_delete(k8s_crd_manager_t* manager, const char* crd_name, const char* name, const char* namespace) {
    if (!manager || !crd_name || !name) return -1;
    
    // Verify CRD exists
    k8s_crd_t* crd = k8s_crd_manager_get(manager, crd_name);
    if (!crd) return -1;
    
    // TODO: Delete from etcd via storage layer
    return 0;
}

int k8s_custom_resource_delete_all(k8s_crd_manager_t* manager, const char* crd_name, const char* namespace) {
    if (!manager || !crd_name) return -1;
    
    // Verify CRD exists
    k8s_crd_t* crd = k8s_crd_manager_get(manager, crd_name);
    if (!crd) return -1;
    
    // TODO: Delete all instances from etcd via storage layer
    return 0;
}

// ============ Validation ============

int k8s_custom_resource_validate(k8s_crd_manager_t* manager, const char* crd_name, const char* spec_json) {
    if (!manager || !crd_name || !spec_json) return -1;
    
    k8s_crd_t* crd = k8s_crd_manager_get(manager, crd_name);
    if (!crd) return -1;
    
    // If CRD has validation schema, validate against it
    if (crd->spec && crd->spec->validation_schema_json) {
        // TODO: Implement JSON schema validation
        // For now, accept any spec
        return 0;
    }
    
    // No schema defined, accept any spec
    return 0;
}

int k8s_crd_manager_exists(k8s_crd_manager_t* manager, const char* group, const char* plural_name) {
    if (!manager || !group || !plural_name) return 0;
    
    for (int i = 0; i < manager->num_crds; i++) {
        k8s_crd_t* crd = manager->crds[i];
        if (crd && crd->spec && crd->spec->names &&
            strcmp(crd->spec->group, group) == 0 &&
            strcmp(crd->spec->names->plural, plural_name) == 0) {
            return 1;
        }
    }
    
    return 0;
}

// ============ Global Manager ============

k8s_crd_manager_t* k8s_crd_manager_global() {
    if (!g_crd_manager) {
        g_crd_manager = k8s_crd_manager_new();
    }
    return g_crd_manager;
}
