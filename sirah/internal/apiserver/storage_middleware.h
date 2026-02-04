#ifndef SIRAH_STORAGE_MIDDLEWARE_H
#define SIRAH_STORAGE_MIDDLEWARE_H

#include "persistent_volume.h"
#include "persistent_volume_claim.h"
#include "storage_class.h"

// Storage middleware for HTTP integration
// Handles PVC creation admission checks, volume binding, reclaim policy enforcement

// PVC creation admission result
typedef struct {
    int allowed;                    // 1 = allowed, 0 = denied
    char denial_reason[256];        // Why denied if applicable
    char pv_bound[256];             // Bound PV name (empty if pending)
} pvc_creation_admission_t;

// ===== Storage Middleware API Functions =====

// Initialize storage subsystem
int storage_middleware_init(void);

// Shutdown storage subsystem
int storage_middleware_shutdown(void);

// Check if PVC can be created (admission control)
int storage_middleware_check_pvc_creation(const char* namespace,
                                         const persistent_volume_claim_t* pvc,
                                         pvc_creation_admission_t* out_result);

// Attempt to bind PVC to a PV
int storage_middleware_bind_pvc(const char* namespace, const char* pvc_name,
                                char* out_pv_name);

// Check if volume mount is valid
int storage_middleware_check_volume_mount(const char* namespace, const char* volume_name,
                                         const char* mount_path);

// Handle PV creation from StorageClass provisioning
int storage_middleware_provision_pv(const char* sc_name, long long capacity_bytes,
                                    pv_access_mode_t access_modes, const char* pvc_namespace,
                                    const char* pvc_name, char* out_pv_name,
                                    char* out_error, int error_buffer_size);

// Handle PVC deletion (triggers PV reclaim)
int storage_middleware_handle_pvc_deletion(const char* namespace, const char* pvc_name);

// Format denied PVC response
int storage_middleware_format_pvc_denied(const pvc_creation_admission_t* result,
                                        char* out_json_buffer, int buffer_size,
                                        int* out_http_code);

// Volume mount support in pods
int storage_middleware_validate_pod_volumes(const char* namespace, const char* pod_json);

// List available storage classes
int storage_middleware_get_storage_classes(char* out_json_buffer, int buffer_size);

// Get default storage class
int storage_middleware_get_default_storage_class(char* out_name);

#endif // SIRAH_STORAGE_MIDDLEWARE_H
