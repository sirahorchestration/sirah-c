#ifndef KUBELET_VOLUMES_H
#define KUBELET_VOLUMES_H

#include <time.h>

// Volume mount point in pod
typedef struct {
    char* mount_path;
    char* volume_name;
    int read_only;
} volume_mount_t;

// Volume definition in pod spec
typedef struct {
    char* name;
    char* type;                // "emptyDir", "hostPath", "persistentVolumeClaim"
    char* path;                // For hostPath
    char* pvc_name;            // For persistentVolumeClaim
    char* storage_class;
} volume_definition_t;

// Pod volume mount mapping
typedef struct {
    char* pod_name;
    char* namespace;
    char* node_name;
    
    volume_definition_t* volumes;
    int num_volumes;
    
    volume_mount_t* mounts;
    int num_mounts;
    
    time_t mounted_time;
} pod_volume_mapping_t;

// Volume mount result
typedef struct {
    int success;
    char* mount_point;
    char* error_message;
} mount_result_t;

// Initialize volume mounting for a pod
int kubelet_mount_pod_volumes_impl(const char* pod_name, const char* namespace,
                                  const char* node_name, const char* base_mount_path,
                                  volume_definition_t* volumes, int num_volumes);

// Mount a single volume (hostPath type)
mount_result_t* kubelet_mount_volume(const char* volume_name, const char* source_path,
                                    const char* mount_point, int read_only);

// Create mount point directory
int kubelet_create_mount_point(const char* mount_path);

// Unmount a volume
int kubelet_unmount_volume(const char* mount_point);

// Clean up all mounts for a pod
int kubelet_cleanup_pod_mounts(const char* pod_name, const char* namespace,
                              const char* base_mount_path);

// Create empty directory volume
int kubelet_create_emptydir_volume(const char* volume_name, const char* mount_path);

// Bind PersistentVolume to pod
int kubelet_bind_persistent_volume(const char* pod_name, const char* namespace,
                                  const char* pvc_name, const char* pv_path,
                                  const char* mount_path);

// Free mount result
void kubelet_free_mount_result(mount_result_t* result);

#endif // KUBELET_VOLUMES_H
