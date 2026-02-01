#ifndef SIRAH_STORAGE_H
#define SIRAH_STORAGE_H

#include "common.h"

// PersistentVolume - cluster-level storage
typedef enum {
    ACCESS_MODE_READ_WRITE_ONCE = 0,
    ACCESS_MODE_READ_ONLY_MANY = 1,
    ACCESS_MODE_READ_WRITE_MANY = 2
} k8s_access_mode_t;

typedef enum {
    VOLUME_STATUS_AVAILABLE = 0,
    VOLUME_STATUS_BOUND = 1,
    VOLUME_STATUS_RELEASED = 2,
    VOLUME_STATUS_FAILED = 3
} k8s_volume_status_t;

typedef struct {
    char* storage_class;
    int capacity_bytes;
    k8s_access_mode_t access_modes[3];
    int num_access_modes;
    char* type;  // "hostPath", "nfs", "iscsi", etc.
    char* path;  // For hostPath
} k8s_pv_spec_t;

typedef struct {
    k8s_metadata_t metadata;
    k8s_pv_spec_t spec;
    k8s_volume_status_t status;
    char* claim_ref;  // Bound PVC name
} k8s_persistent_volume_t;

// PersistentVolumeClaim - user request for storage
typedef struct {
    k8s_metadata_t metadata;
    
    struct {
        k8s_access_mode_t access_modes[3];
        int num_access_modes;
        int storage_bytes;
        char* storage_class;
    } spec;
    
    struct {
        k8s_volume_status_t phase;
        char* volume_name;  // Bound PV name
    } status;
} k8s_persistent_volume_claim_t;

// StatefulSet - for stateful applications
typedef struct {
    k8s_metadata_t metadata;
    
    struct {
        int replicas;
        char* service_name;
        char** labels;
        int num_labels;
    } spec;
    
    struct {
        int ready_replicas;
        int updated_replicas;
    } status;
} k8s_statefulset_t;

// PV operations
k8s_persistent_volume_t* k8s_pv_new(const char* name);
void k8s_pv_free(k8s_persistent_volume_t* pv);
char* k8s_pv_to_json(k8s_persistent_volume_t* pv);

// PVC operations
k8s_persistent_volume_claim_t* k8s_pvc_new(const char* name, const char* namespace);
void k8s_pvc_free(k8s_persistent_volume_claim_t* pvc);
char* k8s_pvc_to_json(k8s_persistent_volume_claim_t* pvc);

// StatefulSet operations
k8s_statefulset_t* k8s_statefulset_new(const char* name, const char* namespace);
void k8s_statefulset_free(k8s_statefulset_t* sts);
char* k8s_statefulset_to_json(k8s_statefulset_t* sts);

#endif
