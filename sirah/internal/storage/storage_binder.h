#ifndef STORAGE_BINDER_H
#define STORAGE_BINDER_H

#include "types/storage.h"

typedef struct {
    k8s_persistent_volume_t** volumes;
    int num_volumes;
} storage_binding_context_t;

// Create a new binding context
storage_binding_context_t* storage_binding_context_new(void);

// Add a persistent volume to the context
void storage_binding_context_add_pv(storage_binding_context_t* ctx, k8s_persistent_volume_t* pv);

// Attempt to bind a PVC to a matching PV
// Returns 0 on success, -1 if no suitable PV found
int storage_bind_pvc(storage_binding_context_t* ctx, k8s_persistent_volume_claim_t* pvc);

// Unbind a PVC from its PV
int storage_unbind_pvc(storage_binding_context_t* ctx, k8s_persistent_volume_claim_t* pvc);

// Release a PV after claim deletion
int storage_release_pv(storage_binding_context_t* ctx, const char* pv_name);

// Check if access modes are compatible
int access_modes_compatible(k8s_access_mode_t* pv_modes, int pv_count,
                           k8s_access_mode_t* pvc_modes, int pvc_count);

// Check if storage class matches (empty string matches any)
int storage_class_matches(const char* pv_class, const char* pvc_class);

// Free the binding context
void storage_binding_context_free(storage_binding_context_t* ctx);

#endif // STORAGE_BINDER_H
