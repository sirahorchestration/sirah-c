#ifndef SIRAH_STORAGE_CLASS_H
#define SIRAH_STORAGE_CLASS_H

#include <time.h>
#include <pthread.h>
#include "persistent_volume.h"

// StorageClass provides a way to describe the "classes" of storage offered
// StorageClasses are cluster-level resources (not namespaced)

// Provisioner type - determines how storage is provisioned
typedef enum {
    PROVISIONER_MANUAL = 1,        // Manual creation (user creates PVs)
    PROVISIONER_HOSTPATH = 2,      // hostPath provisioner (creates host directories)
    PROVISIONER_NFS = 3,           // NFS provisioner (future)
    PROVISIONER_ISCSI = 4          // iSCSI provisioner (future)
} provisioner_type_t;

// VolumeBindingMode - when to bind volume
typedef enum {
    BINDING_MODE_IMMEDIATE = 1,    // Bind immediately when PVC is created
    BINDING_MODE_WAIT_FIRST_CONSUMER = 2  // Bind when pod using PVC is scheduled
} volume_binding_mode_t;

// StorageClass configuration parameters
typedef struct {
    char provisioner[256];          // Provisioner name
    char replication_type[64];      // "Replicated", "Distributed", etc. (optional)
    char csi_driver[256];           // CSI driver name (future)
    int allow_volume_expansion;     // Can PVCs be resized? (1=yes, 0=no)
} provisioner_config_t;

// StorageClass object (cluster-scoped)
typedef struct {
    char name[256];                          // StorageClass name (cluster-wide unique)
    
    provisioner_config_t provisioner;        // Provisioner configuration
    pv_reclaim_policy_t reclaim_policy;      // Default reclaim policy for provisioned PVs
    volume_binding_mode_t binding_mode;      // When to bind volumes
    
    long long default_capacity_bytes;        // Default size if not specified in PVC (0 = no default)
    pv_access_mode_t allowed_access_modes;   // Bitmask of supported access modes
    
    int allow_volume_expansion;              // Allow resizing PVCs?
    int is_default;                          // Is this the default storage class?
    
    time_t created_at;                       // When StorageClass was created
    char created_by[256];                    // User who created it
    
    // Parameters for provisioner (key=value pairs)
    char parameters[512];                    // JSON format or key=value
} storage_class_t;

// StorageClass Manager
typedef struct {
    pthread_mutex_t lock;
    storage_class_t classes[100];           // Max 100 storage classes
    int class_count;
    int initialized;
} sc_manager_t;

// ===== StorageClass API Functions =====

// Initialize and shutdown
int sc_manager_init(void);
int sc_manager_shutdown(void);

// CRUD Operations
int sc_create(const storage_class_t* sc);
int sc_get(const char* name, storage_class_t* out);
int sc_update(const char* name, const storage_class_t* updated);
int sc_delete(const char* name);
int sc_list(storage_class_t* out_array, int max_count);
int sc_list_count(void);

// Default storage class management
int sc_set_default(const char* name);
int sc_get_default(char* out_name);
int sc_is_default(const char* name);

// Provisioning operations
typedef struct {
    int success;                    // 1 = provisioned, 0 = failed
    char pv_name[256];             // Name of created PV
    char error_message[256];       // Error details if failed
} provisioning_result_t;

int sc_provision_pv(const char* sc_name, long long capacity_bytes,
                    pv_access_mode_t access_modes, const char* pvc_namespace,
                    const char* pvc_name, provisioning_result_t* out_result);

// Reclaim operations
int sc_reclaim_pv(const char* sc_name, const char* pv_name);

// Provisioner queries
int sc_get_provisioner(const char* name, char* out_provisioner);
int sc_get_reclaim_policy(const char* name);
int sc_get_binding_mode(const char* name);

// Capacity and access mode checks
long long sc_get_default_capacity(const char* name);
int sc_supports_access_mode(const char* name, pv_access_mode_t mode);
int sc_allows_expansion(const char* name);

// Status and debugging
int sc_get_status(const char* name, char* out_json_buffer, int buffer_size);
int sc_format_list_json(char* out_json_buffer, int buffer_size);

// Host Path provisioning (default for session 4)
int sc_provision_hostpath_pv(const char* sc_name, long long capacity_bytes,
                             pv_access_mode_t access_modes, const char* pvc_namespace,
                             const char* pvc_name, char* out_pv_name);

// Helper to generate PV name
int sc_generate_pv_name(const char* sc_name, const char* pvc_namespace, 
                        const char* pvc_name, char* out_pv_name, int buffer_size);

#endif // SIRAH_STORAGE_CLASS_H
