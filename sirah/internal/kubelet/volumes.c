#include "volumes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

// Global volume mappings (in-memory store)
static struct {
    pod_volume_mapping_t mappings[1000];
    int count;
} volume_mappings = {0};

int kubelet_create_mount_point(const char* mount_path) {
    if (!mount_path) return -1;
    
    struct stat st = {0};
    if (stat(mount_path, &st) == -1) {
        // Directory doesn't exist, create it
        if (mkdir(mount_path, 0755) == -1) {
            fprintf(stderr, "[kubelet] Failed to create mount point: %s\n", mount_path);
            return -1;
        }
    }
    
    return 0;
}

mount_result_t* kubelet_mount_volume(const char* volume_name, const char* source_path,
                                    const char* mount_point, int read_only) {
    if (!volume_name || !source_path || !mount_point) return NULL;
    
    mount_result_t* result = (mount_result_t*)malloc(sizeof(mount_result_t));
    
    // Create mount point
    if (kubelet_create_mount_point(mount_point) != 0) {
        result->success = 0;
        result->mount_point = strdup(mount_point);
        result->error_message = strdup("Failed to create mount point");
        return result;
    }
    
    // Check if source exists
    struct stat st = {0};
    if (stat(source_path, &st) == -1) {
        result->success = 0;
        result->mount_point = strdup(mount_point);
        result->error_message = strdup("Source path does not exist");
        return result;
    }
    
    // In a real system, we would use mount()/bind()/etc.
    // For MVP, we'll just record the mapping
    // This would typically be: mount -o bind /source /target
    
    result->success = 1;
    result->mount_point = strdup(mount_point);
    result->error_message = NULL;
    
    fprintf(stderr, "[kubelet] Mounted volume '%s' from %s to %s\n",
            volume_name, source_path, mount_point);
    
    return result;
}

int kubelet_unmount_volume(const char* mount_point) {
    if (!mount_point) return -1;
    
    // In a real system, we would use umount()
    // For MVP, we just verify the point exists and can be cleaned up
    struct stat st = {0};
    if (stat(mount_point, &st) == 0) {
        // Directory exists, we would unmount it
        fprintf(stderr, "[kubelet] Unmounted volume at %s\n", mount_point);
        return 0;
    }
    
    return -1;
}

int kubelet_create_emptydir_volume(const char* volume_name, const char* mount_path) {
    if (!volume_name || !mount_path) return -1;
    
    if (kubelet_create_mount_point(mount_path) != 0) {
        return -1;
    }
    
    fprintf(stderr, "[kubelet] Created emptyDir volume '%s' at %s\n",
            volume_name, mount_path);
    
    return 0;
}

int kubelet_mount_pod_volumes_impl(const char* pod_name, const char* namespace,
                                  const char* node_name, const char* base_mount_path,
                                  volume_definition_t* volumes, int num_volumes) {
    if (!pod_name || !namespace || !node_name || !base_mount_path || !volumes || num_volumes <= 0) {
        return -1;
    }
    
    // Create pod mount base directory
    char pod_mount_base[512];
    snprintf(pod_mount_base, sizeof(pod_mount_base), "%s/%s-%s", base_mount_path, namespace, pod_name);
    
    if (kubelet_create_mount_point(pod_mount_base) != 0) {
        return -1;
    }
    
    // Record volume mapping
    if (volume_mappings.count >= 1000) {
        fprintf(stderr, "[kubelet] Volume mapping store is full\n");
        return -1;
    }
    
    pod_volume_mapping_t* mapping = &volume_mappings.mappings[volume_mappings.count++];
    mapping->pod_name = strdup(pod_name);
    mapping->namespace = strdup(namespace);
    mapping->node_name = strdup(node_name);
    mapping->volumes = (volume_definition_t*)malloc(sizeof(volume_definition_t) * num_volumes);
    mapping->num_volumes = num_volumes;
    mapping->mounted_time = time(NULL);
    
    // Mount each volume
    for (int i = 0; i < num_volumes; i++) {
        if (!volumes[i].name || !volumes[i].type) continue;
        
        // Copy volume definition
        mapping->volumes[i].name = strdup(volumes[i].name);
        mapping->volumes[i].type = strdup(volumes[i].type);
        mapping->volumes[i].path = volumes[i].path ? strdup(volumes[i].path) : NULL;
        mapping->volumes[i].pvc_name = volumes[i].pvc_name ? strdup(volumes[i].pvc_name) : NULL;
        mapping->volumes[i].storage_class = volumes[i].storage_class ? strdup(volumes[i].storage_class) : NULL;
        
        // Create mount point for this volume
        char volume_mount_point[512];
        snprintf(volume_mount_point, sizeof(volume_mount_point), "%s/%s", pod_mount_base, volumes[i].name);
        
        // Handle different volume types
        if (strcmp(volumes[i].type, "emptyDir") == 0) {
            // Create temporary directory
            if (kubelet_create_emptydir_volume(volumes[i].name, volume_mount_point) != 0) {
                fprintf(stderr, "[kubelet] Failed to create emptyDir volume '%s'\n", volumes[i].name);
            }
        } else if (strcmp(volumes[i].type, "hostPath") == 0) {
            // Mount from host path
            if (volumes[i].path) {
                mount_result_t* result = kubelet_mount_volume(volumes[i].name, volumes[i].path,
                                                             volume_mount_point, 0);
                if (result && !result->success) {
                    fprintf(stderr, "[kubelet] Failed to mount hostPath volume: %s\n",
                           result->error_message);
                }
                if (result) kubelet_free_mount_result(result);
            }
        } else if (strcmp(volumes[i].type, "persistentVolumeClaim") == 0) {
            // Mount PersistentVolume (actual path would come from PV binding)
            if (volumes[i].pvc_name) {
                // In production, would look up the bound PV's path
                fprintf(stderr, "[kubelet] Mounting PVC '%s' for pod '%s'\n",
                       volumes[i].pvc_name, pod_name);
                kubelet_create_mount_point(volume_mount_point);
            }
        }
    }
    
    fprintf(stderr, "[kubelet] Mounted %d volumes for pod %s/%s on node %s\n",
           num_volumes, namespace, pod_name, node_name);
    
    return 0;
}

int kubelet_bind_persistent_volume(const char* pod_name, const char* namespace,
                                  const char* pvc_name, const char* pv_path,
                                  const char* mount_path) {
    if (!pod_name || !namespace || !pvc_name || !pv_path || !mount_path) {
        return -1;
    }
    
    // Find the pod's volume mapping
    for (int i = 0; i < volume_mappings.count; i++) {
        pod_volume_mapping_t* mapping = &volume_mappings.mappings[i];
        if (strcmp(mapping->pod_name, pod_name) == 0 &&
            strcmp(mapping->namespace, namespace) == 0) {
            
            // Find the PVC volume in this pod
            for (int j = 0; j < mapping->num_volumes; j++) {
                if (mapping->volumes[j].pvc_name &&
                    strcmp(mapping->volumes[j].pvc_name, pvc_name) == 0) {
                    
                    // Mount the PV
                    mount_result_t* result = kubelet_mount_volume(pvc_name, pv_path,
                                                                 mount_path, 0);
                    int success = result && result->success;
                    if (result) kubelet_free_mount_result(result);
                    
                    fprintf(stderr, "[kubelet] Bound PVC '%s' to PV at %s (mounted at %s)\n",
                           pvc_name, pv_path, mount_path);
                    
                    return success ? 0 : -1;
                }
            }
        }
    }
    
    return -1;  // Pod or PVC not found
}

int kubelet_cleanup_pod_mounts(const char* pod_name, const char* namespace,
                              const char* base_mount_path) {
    if (!pod_name || !namespace || !base_mount_path) return -1;
    
    // Find and remove the pod's volume mapping
    for (int i = 0; i < volume_mappings.count; i++) {
        pod_volume_mapping_t* mapping = &volume_mappings.mappings[i];
        if (strcmp(mapping->pod_name, pod_name) == 0 &&
            strcmp(mapping->namespace, namespace) == 0) {
            
            // Unmount all volumes
            for (int j = 0; j < mapping->num_volumes; j++) {
                char volume_mount_point[512];
                snprintf(volume_mount_point, sizeof(volume_mount_point), 
                        "%s/%s-%s/%s", base_mount_path, namespace, pod_name,
                        mapping->volumes[j].name);
                kubelet_unmount_volume(volume_mount_point);
                
                // Free volume definition
                free(mapping->volumes[j].name);
                free(mapping->volumes[j].type);
                if (mapping->volumes[j].path) free(mapping->volumes[j].path);
                if (mapping->volumes[j].pvc_name) free(mapping->volumes[j].pvc_name);
                if (mapping->volumes[j].storage_class) free(mapping->volumes[j].storage_class);
            }
            
            // Free the mapping
            free(mapping->volumes);
            free(mapping->pod_name);
            free(mapping->namespace);
            free(mapping->node_name);
            
            // Remove from list
            for (int j = i; j < volume_mappings.count - 1; j++) {
                volume_mappings.mappings[j] = volume_mappings.mappings[j + 1];
            }
            volume_mappings.count--;
            
            fprintf(stderr, "[kubelet] Cleaned up mounts for pod %s/%s\n", namespace, pod_name);
            return 0;
        }
    }
    
    return -1;
}

void kubelet_free_mount_result(mount_result_t* result) {
    if (!result) return;
    if (result->mount_point) free(result->mount_point);
    if (result->error_message) free(result->error_message);
    free(result);
}
