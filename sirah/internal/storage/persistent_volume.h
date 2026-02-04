#ifndef SIRAH_PERSISTENT_VOLUME_H
#define SIRAH_PERSISTENT_VOLUME_H

#include <time.h>
#include <pthread.h>

// PersistentVolume represents a piece of storage in the cluster
// PersistentVolumes are cluster-level resources (not namespaced)

// Access mode for PersistentVolume
typedef enum {
    ACCESS_MODE_READ_WRITE_ONCE = 1,  // RWO - Single node read/write
    ACCESS_MODE_READ_ONLY_MANY = 2,   // ROX - Multiple nodes read-only
    ACCESS_MODE_READ_WRITE_MANY = 4   // RWX - Multiple nodes read/write
} pv_access_mode_t;

// Reclaim policy - what happens to PV when PVC is deleted
typedef enum {
    RECLAIM_POLICY_RETAIN = 1,  // Manual cleanup required
    RECLAIM_POLICY_DELETE = 2,  // Automatically delete storage
    RECLAIM_POLICY_RECYCLE = 3  // Clean volume and recycle (deprecated in v1.23+)
} pv_reclaim_policy_t;

// PersistentVolume phase
typedef enum {
    PV_PHASE_AVAILABLE = 1,       // Available for binding
    PV_PHASE_BOUND = 2,           // Bound to a PVC
    PV_PHASE_RELEASED = 3,        // PVC deleted, waiting for reclaim
    PV_PHASE_FAILED = 4,          // Failed (manual cleanup needed)
    PV_PHASE_PENDING = 5          // Not yet fully initialized
} pv_phase_t;

// Storage backend (currently supporting hostPath)
typedef enum {
    STORAGE_BACKEND_HOST_PATH = 1,
    STORAGE_BACKEND_NFS = 2,      // Future
    STORAGE_BACKEND_ISCSI = 3,    // Future
    STORAGE_BACKEND_LOCAL = 4     // Future
} storage_backend_type_t;

// hostPath configuration
typedef struct {
    char path[512];  // Path on host node (e.g., /var/sirah/volumes/pv-001)
    char type[64];   // "Directory" or "DirectoryOrCreate" or "File" or "FileOrCreate"
} hostpath_config_t;

// Backend-specific configuration
typedef struct {
    storage_backend_type_t type;
    union {
        hostpath_config_t hostpath;
        // Future: nfs_config_t nfs, etc.
    } config;
} storage_backend_config_t;

// PersistentVolume object (cluster-scoped)
typedef struct {
    char name[256];                          // PV name (cluster-wide unique)
    char bound_pvc_namespace[64];            // Namespace of bound PVC (empty if not bound)
    char bound_pvc_name[256];                // Name of bound PVC (empty if not bound)
    
    long long capacity_bytes;                // Storage capacity in bytes
    pv_access_mode_t access_modes;          // Bitmask of allowed access modes
    pv_reclaim_policy_t reclaim_policy;     // What to do when released
    
    pv_phase_t phase;                       // Current phase (available/bound/released/failed)
    
    storage_backend_config_t backend;       // Storage backend configuration
    
    time_t created_at;                      // When PV was created
    time_t bound_at;                        // When PVC was bound (0 if not bound)
    char created_by[256];                   // User who created this PV
    
    char node_affinity[512];                // Node affinity requirement (optional)
    int claim_ref_generation;               // For tracking claim binding
} persistent_volume_t;

// PersistentVolume Manager
typedef struct {
    pthread_mutex_t lock;
    persistent_volume_t volumes[500];       // Max 500 PVs per cluster
    int volume_count;
    int initialized;
} pv_manager_t;

// ===== PersistentVolume API Functions =====

// Initialize and shutdown
int pv_manager_init(void);
int pv_manager_shutdown(void);

// CRUD Operations
int pv_create(const persistent_volume_t* pv);
int pv_get(const char* name, persistent_volume_t* out);
int pv_update(const char* name, const persistent_volume_t* updated);
int pv_delete(const char* name);
int pv_list(persistent_volume_t* out_array, int max_count);
int pv_list_count(void);

// Phase management and status
int pv_set_phase(const char* name, pv_phase_t phase);
int pv_get_phase(const char* name);
int pv_is_available(const char* name);

// Binding operations
int pv_bind_to_pvc(const char* pv_name, const char* pvc_namespace, const char* pvc_name);
int pv_unbind_from_pvc(const char* pv_name);
int pv_get_bound_pvc(const char* pv_name, char* out_namespace, char* out_pvc_name);

// Capacity and access mode checks
long long pv_get_capacity(const char* name);
int pv_supports_access_mode(const char* name, pv_access_mode_t mode);
int pv_supports_read_write_once(const char* name);
int pv_supports_read_only_many(const char* name);
int pv_supports_read_write_many(const char* name);

// Backend operations
int pv_create_hostpath_directory(const char* pv_name, const char* path);
int pv_delete_hostpath_directory(const char* pv_name);
int pv_verify_storage_available(const char* pv_name);

// Reclaim operations
int pv_reclaim(const char* pv_name);
int pv_get_reclaim_policy(const char* name);

// Status and debugging
int pv_get_status(const char* name, char* out_json_buffer, int buffer_size);
int pv_format_list_json(char* out_json_buffer, int buffer_size);
int pv_format_phase_string(pv_phase_t phase, char* out_string);

#endif // SIRAH_PERSISTENT_VOLUME_H
