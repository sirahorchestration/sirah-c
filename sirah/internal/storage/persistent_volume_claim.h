#ifndef SIRAH_PERSISTENT_VOLUME_CLAIM_H
#define SIRAH_PERSISTENT_VOLUME_CLAIM_H

#include <time.h>
#include <pthread.h>
#include "persistent_volume.h"

// PersistentVolumeClaim represents a request for storage
// PVCs are namespace-scoped resources

// PVC Phase - progression of binding to storage
typedef enum {
    PVC_PHASE_PENDING = 1,  // Waiting for matching PV to be available
    PVC_PHASE_BOUND = 2,    // Successfully bound to a PV
    PVC_PHASE_LOST = 3,     // Underlying PV was deleted
    PVC_PHASE_FAILED = 4    // Binding failed (incompatible access modes, etc.)
} pvc_phase_t;

// PVC Storage Class reference
typedef struct {
    char name[256];  // Reference to StorageClass
} storage_class_ref_t;

// PersistentVolumeClaim object (namespace-scoped)
typedef struct {
    char namespace[64];                      // Namespace of PVC
    char name[256];                          // Name within namespace
    
    char bound_pv_name[256];                 // Name of bound PV (empty if not bound)
    
    long long requested_capacity_bytes;      // Storage size requested (e.g., 10Gi)
    pv_access_mode_t access_modes;           // Required access modes (bitmask)
    
    storage_class_ref_t storage_class;       // Reference to StorageClass (optional)
    
    pvc_phase_t phase;                       // Current phase (pending/bound/lost/failed)
    
    time_t created_at;                       // When PVC was created
    time_t bound_at;                         // When PV was bound (0 if not bound)
    char created_by[256];                    // User who created this PVC
    
    int binding_attempt_count;               // Number of binding attempts
    time_t last_binding_attempt;             // Timestamp of last binding attempt
    
    char selector[512];                      // Label selector for PVs (optional)
} persistent_volume_claim_t;

// PVC Manager
typedef struct {
    pthread_mutex_t lock;
    persistent_volume_claim_t claims[500];   // Max 500 PVCs per namespace
    int claim_count;
    int initialized;
} pvc_manager_t;

// ===== PersistentVolumeClaim API Functions =====

// Initialize and shutdown
int pvc_manager_init(void);
int pvc_manager_shutdown(void);

// CRUD Operations
int pvc_create(const persistent_volume_claim_t* pvc);
int pvc_get(const char* namespace, const char* name, persistent_volume_claim_t* out);
int pvc_update(const char* namespace, const char* name, const persistent_volume_claim_t* updated);
int pvc_delete(const char* namespace, const char* name);
int pvc_list(const char* namespace, persistent_volume_claim_t* out_array, int max_count);
int pvc_list_count(const char* namespace);

// Phase management
int pvc_set_phase(const char* namespace, const char* name, pvc_phase_t phase);
int pvc_get_phase(const char* namespace, const char* name);
int pvc_is_bound(const char* namespace, const char* name);

// Binding operations (managed by binder)
int pvc_bind_to_pv(const char* namespace, const char* pvc_name, const char* pv_name);
int pvc_unbind_from_pv(const char* namespace, const char* pvc_name);
int pvc_get_bound_pv(const char* namespace, const char* name, char* out_pv_name);

// Capacity checks
long long pvc_get_requested_capacity(const char* namespace, const char* name);
int pvc_supports_access_mode(const char* namespace, const char* name, pv_access_mode_t mode);

// Volume binding algorithm (find matching PV)
typedef struct {
    int found;                          // 1 if match found
    char matched_pv_name[256];          // Name of matched PV
    char reason[256];                   // Why binding failed (if not found)
} pvc_binding_result_t;

int pvc_find_matching_pv(const char* namespace, const char* pvc_name, 
                         pvc_binding_result_t* out_result);

// Binding attempt tracking
int pvc_increment_binding_attempts(const char* namespace, const char* name);
int pvc_get_binding_attempts(const char* namespace, const char* name);

// Status and debugging
int pvc_get_status(const char* namespace, const char* name, char* out_json_buffer, int buffer_size);
int pvc_format_list_json(const char* namespace, char* out_json_buffer, int buffer_size);
int pvc_format_phase_string(pvc_phase_t phase, char* out_string);

// Admission check - validate PVC request is compatible
typedef struct {
    int allowed;                 // 1 = allowed, 0 = denied
    char denial_reason[256];    // Why denied
    long long requested_bytes;  // Requested capacity
    long long available_bytes;  // Available from matching PVs
} pvc_admission_result_t;

int pvc_check_can_create(const char* namespace, const persistent_volume_claim_t* pvc,
                         pvc_admission_result_t* out_result);

#endif // SIRAH_PERSISTENT_VOLUME_CLAIM_H
