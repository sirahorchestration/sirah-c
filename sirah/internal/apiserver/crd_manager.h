#ifndef K8S_CRD_MANAGER_H
#define K8S_CRD_MANAGER_H

#include <types/crd.h>

#define K8S_MAX_REGISTERED_CRDS 100
#define K8S_MAX_CUSTOM_RESOURCES 10000

// CRD manager - maintains registry of all CRDs
typedef struct {
    k8s_crd_t* crds[K8S_MAX_REGISTERED_CRDS];
    int num_crds;
    
    // Separate storage for custom resource instances
    // In a real implementation, this would use etcd
    void* custom_resources_store;  // Opaque handle to storage
} k8s_crd_manager_t;

// ============ Initialization ============

/**
 * Create a new CRD manager
 */
k8s_crd_manager_t* k8s_crd_manager_new();

/**
 * Free CRD manager and all stored CRDs
 */
void k8s_crd_manager_free(k8s_crd_manager_t* manager);

// ============ CRD Registration ============

/**
 * Register a CRD (makes it available for custom resources)
 * Sets status to "Established" if validation passes
 */
int k8s_crd_manager_register(k8s_crd_manager_t* manager, k8s_crd_t* crd);

/**
 * Unregister a CRD
 * Also removes all custom resource instances of that CRD
 */
int k8s_crd_manager_unregister(k8s_crd_manager_t* manager, const char* crd_name);

/**
 * Get registered CRD by name
 */
k8s_crd_t* k8s_crd_manager_get(k8s_crd_manager_t* manager, const char* crd_name);

/**
 * List all registered CRDs
 */
int k8s_crd_manager_list(k8s_crd_manager_t* manager, k8s_crd_t** crds, int* count);

/**
 * Update a registered CRD
 */
int k8s_crd_manager_update(k8s_crd_manager_t* manager, k8s_crd_t* crd);

// ============ Custom Resource CRUD ============

/**
 * Create a custom resource instance
 */
int k8s_custom_resource_create(k8s_crd_manager_t* manager, const char* crd_name, k8s_custom_resource_t* resource);

/**
 * Get a custom resource instance
 */
k8s_custom_resource_t* k8s_custom_resource_get(k8s_crd_manager_t* manager, const char* crd_name, const char* name, const char* namespace);

/**
 * List custom resource instances (optionally filtered by namespace)
 */
int k8s_custom_resource_list(k8s_crd_manager_t* manager, const char* crd_name, const char* namespace, k8s_custom_resource_t** resources, int* count);

/**
 * Update a custom resource instance
 */
int k8s_custom_resource_update(k8s_crd_manager_t* manager, const char* crd_name, k8s_custom_resource_t* resource);

/**
 * Delete a custom resource instance
 */
int k8s_custom_resource_delete(k8s_crd_manager_t* manager, const char* crd_name, const char* name, const char* namespace);

/**
 * Delete all custom resource instances of a CRD
 */
int k8s_custom_resource_delete_all(k8s_crd_manager_t* manager, const char* crd_name, const char* namespace);

// ============ Validation ============

/**
 * Validate a custom resource against CRD schema
 */
int k8s_custom_resource_validate(k8s_crd_manager_t* manager, const char* crd_name, const char* spec_json);

/**
 * Check if a CRD with given name/group already exists
 */
int k8s_crd_manager_exists(k8s_crd_manager_t* manager, const char* group, const char* plural_name);

// ============ Global Manager ============

/**
 * Get global CRD manager instance
 */
k8s_crd_manager_t* k8s_crd_manager_global();

#endif // K8S_CRD_MANAGER_H
