#include "storage_binder.h"
#include <stdlib.h>
#include <string.h>

storage_binding_context_t* storage_binding_context_new(void) {
    storage_binding_context_t* ctx = (storage_binding_context_t*)malloc(sizeof(storage_binding_context_t));
    ctx->volumes = (k8s_persistent_volume_t**)malloc(sizeof(k8s_persistent_volume_t*) * 1000);
    ctx->num_volumes = 0;
    return ctx;
}

void storage_binding_context_add_pv(storage_binding_context_t* ctx, k8s_persistent_volume_t* pv) {
    if (!ctx || !pv || ctx->num_volumes >= 1000) return;
    ctx->volumes[ctx->num_volumes++] = pv;
}

int access_modes_compatible(k8s_access_mode_t* pv_modes, int pv_count,
                           k8s_access_mode_t* pvc_modes, int pvc_count) {
    if (!pv_modes || !pvc_modes || pv_count <= 0 || pvc_count <= 0) {
        return 0;
    }
    
    // Check if PV supports all PVC's access modes
    for (int i = 0; i < pvc_count; i++) {
        int found = 0;
        for (int j = 0; j < pv_count; j++) {
            if (pv_modes[j] == pvc_modes[i]) {
                found = 1;
                break;
            }
        }
        if (!found) return 0;  // PV doesn't support this access mode
    }
    
    return 1;  // All PVC access modes supported
}

int storage_class_matches(const char* pv_class, const char* pvc_class) {
    if (!pv_class || !pvc_class) return 0;
    
    // Empty storage class matches anything
    if (strlen(pv_class) == 0) return 1;
    if (strlen(pvc_class) == 0) return 1;
    
    // Exact match required
    return strcmp(pv_class, pvc_class) == 0;
}

int storage_bind_pvc(storage_binding_context_t* ctx, k8s_persistent_volume_claim_t* pvc) {
    if (!ctx || !pvc) return -1;
    
    // Find a suitable PV
    for (int i = 0; i < ctx->num_volumes; i++) {
        k8s_persistent_volume_t* pv = ctx->volumes[i];
        
        // Check if PV is available
        if (pv->status != VOLUME_STATUS_AVAILABLE) continue;
        
        // Check storage class matching
        if (!storage_class_matches(pv->spec.storage_class, pvc->spec.storage_class)) continue;
        
        // Check capacity
        if (pv->spec.capacity_bytes < pvc->spec.storage_bytes) continue;
        
        // Check access modes
        if (!access_modes_compatible(pv->spec.access_modes, pv->spec.num_access_modes,
                                     pvc->spec.access_modes, pvc->spec.num_access_modes)) {
            continue;
        }
        
        // Bind them
        pv->status = VOLUME_STATUS_BOUND;
        pv->claim_ref = strdup(pvc->metadata.name);
        
        pvc->status.phase = VOLUME_STATUS_BOUND;
        pvc->status.volume_name = strdup(pv->metadata.name);
        
        return 0;  // Success
    }
    
    return -1;  // No suitable PV found
}

int storage_unbind_pvc(storage_binding_context_t* ctx, k8s_persistent_volume_claim_t* pvc) {
    if (!ctx || !pvc || !pvc->status.volume_name) return -1;
    
    // Find the bound PV
    for (int i = 0; i < ctx->num_volumes; i++) {
        k8s_persistent_volume_t* pv = ctx->volumes[i];
        
        if (strcmp(pv->metadata.name, pvc->status.volume_name) != 0) continue;
        
        // Transition to Released
        pv->status = VOLUME_STATUS_RELEASED;
        
        return 0;
    }
    
    return -1;
}

int storage_release_pv(storage_binding_context_t* ctx, const char* pv_name) {
    if (!ctx || !pv_name) return -1;
    
    for (int i = 0; i < ctx->num_volumes; i++) {
        k8s_persistent_volume_t* pv = ctx->volumes[i];
        
        if (strcmp(pv->metadata.name, pv_name) != 0) continue;
        
        // Fully release the PV
        pv->status = VOLUME_STATUS_AVAILABLE;
        if (pv->claim_ref) {
            free(pv->claim_ref);
            pv->claim_ref = NULL;
        }
        
        return 0;
    }
    
    return -1;
}

void storage_binding_context_free(storage_binding_context_t* ctx) {
    if (!ctx) return;
    
    for (int i = 0; i < ctx->num_volumes; i++) {
        k8s_pv_free(ctx->volumes[i]);
    }
    free(ctx->volumes);
    free(ctx);
}
