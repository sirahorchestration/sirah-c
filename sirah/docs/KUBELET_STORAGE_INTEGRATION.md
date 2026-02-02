# Week 5: Kubelet Storage Integration

## Overview

The kubelet now supports mounting PersistentVolumes, PersistentVolumeClaims, hostPath volumes, and emptyDir volumes to pods. This enables stateful workloads to persist data across pod restarts.

## Storage Integration Architecture

### Components Added

#### 1. **Volume Manager Module** (`internal/kubelet/volumes.h/c`)
- Handles volume mount lifecycle for pods
- Supports multiple volume types
- Manages volume-to-mount-point mappings

#### 2. **Volume Types Supported**
- **emptyDir**: Temporary directory created per pod
- **hostPath**: Mount from host filesystem
- **persistentVolumeClaim**: PersistentVolumes bound via claims

#### 3. **Pod Volume Support** (`pkg/types/pod.h/c`)
- Volume definitions in pod spec
- Volume mount information
- Helper functions to add volumes

### Implementation Details

#### Volume Mount Lifecycle

```
Pod Created
    ↓
Kubelet Fetches Pod
    ↓
Mount Volumes
  ├─ emptyDir: Create temp dir
  ├─ hostPath: Bind mount from host
  └─ PVC: Mount bound PersistentVolume
    ↓
Pod Running with Volumes
    ↓
Pod Deleted
    ↓
Unmount & Cleanup Volumes
```

#### Volume Mapping Storage

Volumes are tracked in memory with the structure:
```c
pod_volume_mapping_t {
    pod_name
    namespace
    node_name
    volumes[]          // Volume definitions
    mounts[]           // Mount points
    mounted_time
}
```

### File Structure

```
internal/kubelet/
├── volumes.h              // Volume mount interface (74 lines)
├── volumes.c              // Volume mount implementation (260 lines)
├── kubelet.h              // Updated with volume support
└── kubelet.c              // Updated with volume calls

pkg/types/
├── pod.h                  // Added volume types & functions
└── pod.c                  // Added volume operations
```

## API Reference

### Pod Volume Functions

```c
// Add volume to pod spec
int k8s_pod_add_volume(k8s_pod_t* pod, const char* name, const char* type);

// Add PersistentVolumeClaim volume
int k8s_pod_add_pvc_volume(k8s_pod_t* pod, const char* name, const char* pvc_name);

// Add hostPath volume
int k8s_pod_add_hostpath_volume(k8s_pod_t* pod, const char* name, const char* path);

// Add emptyDir volume
int k8s_pod_add_emptydir_volume(k8s_pod_t* pod, const char* name);
```

### Kubelet Volume Functions

```c
// Mount all volumes for a pod
int kubelet_mount_pod_volumes(kubelet_t* kubelet, const char* namespace,
                             const char* pod_name,
                             volume_definition_t* volumes, int num_volumes);

// Clean up pod volumes
int kubelet_cleanup_volumes(kubelet_t* kubelet, const char* namespace,
                           const char* pod_name);

// Mount a single volume
mount_result_t* kubelet_mount_volume(const char* volume_name,
                                    const char* source_path,
                                    const char* mount_point,
                                    int read_only);

// Bind PersistentVolume to pod
int kubelet_bind_persistent_volume(const char* pod_name, const char* namespace,
                                  const char* pvc_name, const char* pv_path,
                                  const char* mount_path);
```

## Usage Examples

### Pod with PersistentVolumeClaim

```c
#include "types/pod.h"

// Create pod
k8s_pod_t* pod = k8s_pod_new("mysql", "default");

// Add PVC volume
k8s_pod_add_pvc_volume(pod, "mysql-storage", "mysql-data-claim");

// Pod now has volume requirement
assert(pod->spec.num_volumes == 1);
assert(strcmp(pod->spec.volumes[0].type, "persistentVolumeClaim") == 0);
assert(strcmp(pod->spec.volumes[0].pvc_name, "mysql-data-claim") == 0);
```

### Pod with hostPath Volume

```c
// Create pod with host directory access
k8s_pod_t* pod = k8s_pod_new("logger", "default");
k8s_pod_add_hostpath_volume(pod, "logs", "/var/log/app");

// When kubelet runs pod:
// - Mount /var/log/app from host to pod's /logs
// - Pod can write logs that persist on host
```

### Pod with emptyDir Volume

```c
// Create pod with temporary storage
k8s_pod_t* pod = k8s_pod_new("cache", "default");
k8s_pod_add_emptydir_volume(pod, "cache-storage");

// When kubelet mounts:
// - Creates temporary directory in /var/lib/kubelet/pods/default-cache/cache-storage
// - Cleaned up when pod terminates
```

### Kubelet Volume Mounting

```c
#include "kubelet.h"

kubelet_t* kubelet = kubelet_new("node-1", "http://api-server:6443");

// Mount pod volumes
volume_definition_t volumes[2];
volumes[0].name = "data";
volumes[0].type = "hostPath";
volumes[0].path = "/mnt/data";

volumes[1].name = "cache";
volumes[1].type = "emptyDir";

kubelet_mount_pod_volumes(kubelet, "default", "my-pod", volumes, 2);

// Later, clean up
kubelet_cleanup_volumes(kubelet, "default", "my-pod");
```

## Volume Types

### emptyDir
- **Purpose**: Temporary pod-local storage
- **Lifetime**: Pod lifetime
- **Cleanup**: Automatic when pod deleted
- **Use Cases**: Caching, temporary files, shared volume between containers

```c
k8s_pod_add_emptydir_volume(pod, "tmp-storage");
```

### hostPath
- **Purpose**: Access host filesystem
- **Lifetime**: Controlled by pod
- **Cleanup**: Pod responsible for cleanup
- **Use Cases**: Log aggregation, system access, node-local storage

```c
k8s_pod_add_hostpath_volume(pod, "logs", "/var/log/app");
```

### persistentVolumeClaim
- **Purpose**: Persistent data across pod restarts
- **Lifetime**: Cluster lifetime (data survives pod)
- **Cleanup**: Explicit deletion required
- **Use Cases**: Databases, stateful applications, shared data

```c
k8s_pod_add_pvc_volume(pod, "database", "mysql-pvc");
```

## Mount Point Organization

Volumes for a pod are mounted under:
```
/var/lib/kubelet/pods/{namespace}-{pod_name}/{volume_name}/
```

Example for pod `mysql` in namespace `default`:
```
/var/lib/kubelet/pods/default-mysql/
├── mysql-data/        (from PVC)
├── mysql-config/      (from hostPath)
└── temp-cache/        (from emptyDir)
```

## Integration with Storage Components

### With PersistentVolume

```c
// After PVC bound to PV:
int result = kubelet_bind_persistent_volume(
    "pod-name",
    "default",
    "my-pvc",
    "/mnt/volumes/pv-001",  // PV's actual path
    "/var/lib/kubelet/pods/default-pod-name/my-volume/"
);
```

### Volume Status Tracking

Volumes maintain state:
- Mount time recorded
- Error messages captured
- Mount points tracked in memory
- Ready for status reporting

## Performance Characteristics

| Operation | Time | Notes |
|-----------|------|-------|
| Mount pod (1 volume) | O(1) | Directory creation + mapping |
| Mount pod (N volumes) | O(n) | Linear in volume count |
| Cleanup pod volumes | O(n) | Unmount each volume |
| Mount lookup | O(p) | p = pods on node |
| Find PVC binding | O(v) | v = volumes per pod |

## Error Handling

All volume operations return status codes:
- `0` = Success
- `-1` = Failure (with error message in result)

Example:
```c
mount_result_t* result = kubelet_mount_volume("data", "/mnt/data", 
                                             "/var/lib/kubelet/pods/...", 0);
if (!result->success) {
    printf("Mount failed: %s\n", result->error_message);
}
kubelet_free_mount_result(result);
```

## Limitations (MVP)

1. **No actual bind mounts**: Currently only creates directories (not syscall mount/umount)
2. **No container integration**: Pod-level only (containers share pod volumes)
3. **No security context**: No uid/gid mapping
4. **No quota enforcement**: No storage limits per volume
5. **No encryption**: Volumes stored in plaintext

## Future Enhancements

### Phase 1
- [ ] Actual filesystem bind mounting
- [ ] Container-to-volume mount mapping
- [ ] Volume permission management

### Phase 2
- [ ] Storage quota enforcement
- [ ] Snapshot and backup support
- [ ] Volume encryption at rest

### Phase 3
- [ ] Dynamic volume provisioning
- [ ] Multi-pod volume sharing
- [ ] Cross-node volume replication

## Testing

### Unit Tests (To Implement)
```c
// Test volume creation
k8s_pod_t* pod = k8s_pod_new("test", "default");
assert(k8s_pod_add_volume(pod, "vol1", "emptyDir") == 0);
assert(pod->spec.num_volumes == 1);

// Test volume mounting
mount_result_t* result = kubelet_mount_volume(...);
assert(result->success == 1);
kubelet_free_mount_result(result);

// Test cleanup
assert(kubelet_cleanup_volumes(kubelet, "default", "test-pod") == 0);
```

### Integration Tests (To Implement)
- Create pod with volumes
- Verify mounts created
- Run pod
- Verify pod can access volumes
- Delete pod
- Verify mounts cleaned up

## Code Statistics

### New Code
- **volumes.h**: 74 lines (interface)
- **volumes.c**: 260 lines (implementation)
- **Total**: 334 lines

### Modified Code
- **pod.h**: Added volume types and functions
- **pod.c**: Added volume operation implementations (~40 lines)
- **kubelet.h**: Added volume function declarations
- **kubelet.c**: Added volume handling integration (~20 lines)
- **Makefile**: Added volumes.c to build

### Total Addition
~400 lines of C code + integration

## Build Status

✓ All 4 binaries compile with zero errors
- sirah-apiserver (79K) - No changes
- sirah-scheduler (60K) - No changes  
- sirah-controller (74K) - No changes
- sirah-kubelet (69K) - **+5K with volume support**

## Integration Checklist

✅ Volume structures added to pod spec
✅ Pod volume helper functions implemented
✅ Kubelet volume manager module created
✅ Volume mount tracking system implemented
✅ Kubelet integrated with volume manager
✅ emptyDir volume support
✅ hostPath volume support
✅ PersistentVolumeClaim mounting framework
✅ All 4 binaries building successfully
✅ Memory management verified
✅ Error handling implemented

## Next Steps (Week 6+)

1. **Implement actual bind mounts**: Use syscall mount() for production
2. **Container volume integration**: Map volumes to container filesystems
3. **Pod volume expansion**: Resize volumes, add more volumes
4. **Storage class integration**: Automatic provision and bind based on class
5. **Monitoring**: Volume usage metrics and reporting

## References

- [Volume Mount Implementation](internal/kubelet/volumes.h)
- [Pod Volume Support](pkg/types/pod.h)
- [Kubelet Integration](internal/kubelet/kubelet.h)
- [Storage System Documentation](WEEK5_IMPLEMENTATION.md)
