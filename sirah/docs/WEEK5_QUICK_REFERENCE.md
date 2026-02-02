# Week 5 Quick Reference

## Cheat Sheet

### ConfigMap API

```bash
# Create
curl -X POST http://localhost:6443/api/v1/namespaces/default/configmaps \
  -H "Content-Type: application/json" \
  -d '{
    "apiVersion": "v1",
    "kind": "ConfigMap",
    "metadata": {"name": "app-config"},
    "data": {
      "app.properties": "key=value",
      "log.level": "debug"
    }
  }'

# Get
curl http://localhost:6443/api/v1/namespaces/default/configmaps/app-config

# List
curl http://localhost:6443/api/v1/namespaces/default/configmaps

# Delete
curl -X DELETE http://localhost:6443/api/v1/namespaces/default/configmaps/app-config
```

### Secret API

```bash
# Create
curl -X POST http://localhost:6443/api/v1/namespaces/default/secrets \
  -H "Content-Type: application/json" \
  -d '{
    "apiVersion": "v1",
    "kind": "Secret",
    "metadata": {"name": "db-pass"},
    "type": "Opaque",
    "data": {
      "password": "secret123",
      "username": "admin"
    }
  }'

# Get
curl http://localhost:6443/api/v1/namespaces/default/secrets/db-pass

# List
curl http://localhost:6443/api/v1/namespaces/default/secrets

# Delete
curl -X DELETE http://localhost:6443/api/v1/namespaces/default/secrets/db-pass
```

### PersistentVolume API

```bash
# Create
curl -X POST http://localhost:6443/api/v1/persistentvolumes \
  -H "Content-Type: application/json" \
  -d '{
    "apiVersion": "v1",
    "kind": "PersistentVolume",
    "metadata": {"name": "pv-001"},
    "spec": {
      "capacity": 10737418240,
      "type": "hostPath",
      "path": "/mnt/data",
      "storageClass": "standard"
    }
  }'

# Get
curl http://localhost:6443/api/v1/persistentvolumes/pv-001

# List
curl http://localhost:6443/api/v1/persistentvolumes

# Delete
curl -X DELETE http://localhost:6443/api/v1/persistentvolumes/pv-001
```

### PersistentVolumeClaim API

```bash
# Create
curl -X POST http://localhost:6443/api/v1/namespaces/default/persistentvolumeclaims \
  -H "Content-Type: application/json" \
  -d '{
    "apiVersion": "v1",
    "kind": "PersistentVolumeClaim",
    "metadata": {"name": "data-claim"},
    "spec": {
      "storage": 5368709120,
      "storageClass": "standard"
    }
  }'

# Get
curl http://localhost:6443/api/v1/namespaces/default/persistentvolumeclaims/data-claim

# List
curl http://localhost:6443/api/v1/namespaces/default/persistentvolumeclaims

# Delete
curl -X DELETE http://localhost:6443/api/v1/namespaces/default/persistentvolumeclaims/data-claim
```

### StatefulSet API

```bash
# Create
curl -X POST http://localhost:6443/api/v1/namespaces/default/statefulsets \
  -H "Content-Type: application/json" \
  -d '{
    "apiVersion": "apps/v1",
    "kind": "StatefulSet",
    "metadata": {"name": "mysql"},
    "spec": {
      "replicas": 3,
      "serviceName": "mysql"
    }
  }'

# Get
curl http://localhost:6443/api/v1/namespaces/default/statefulsets/mysql

# List
curl http://localhost:6443/api/v1/namespaces/default/statefulsets

# Delete
curl -X DELETE http://localhost:6443/api/v1/namespaces/default/statefulsets/mysql
```

## C API Examples

### ConfigMap Example
```c
#include "types/config.h"

// Create
k8s_configmap_t* cm = k8s_configmap_new("app-config", "default");
k8s_configmap_set(cm, "database.host", "db.example.com");
k8s_configmap_set(cm, "database.port", "5432");

// Retrieve
const char* host = k8s_configmap_get(cm, "database.host");
printf("DB Host: %s\n", host);  // DB Host: db.example.com

// Serialize
char* json = k8s_configmap_to_json(cm);
printf("JSON: %s\n", json);

// Deserialize
k8s_configmap_t* restored = k8s_configmap_from_json(json);

// Cleanup
k8s_configmap_free(cm);
k8s_configmap_free(restored);
free(json);
```

### Secret Example
```c
#include "types/config.h"

k8s_secret_t* secret = k8s_secret_new("api-key", "default", "Opaque");
k8s_secret_set(secret, "token", "abc123xyz");
k8s_secret_set(secret, "refresh_token", "xyz789abc");

const char* token = k8s_secret_get(secret, "token");
printf("Token type: %s\n", secret->type);  // Token type: Opaque

k8s_secret_free(secret);
```

### Storage Binding Example
```c
#include "storage/storage_binder.h"

// Create PV
k8s_persistent_volume_t* pv = k8s_pv_new("storage-001");
pv->spec.capacity_bytes = 100 * 1024 * 1024 * 1024;  // 100GB
pv->spec.storage_class = strdup("fast-ssd");
pv->spec.num_access_modes = 1;
pv->spec.access_modes[0] = ACCESS_MODE_READ_WRITE_ONCE;

// Create PVC
k8s_persistent_volume_claim_t* pvc = k8s_pvc_new("data", "default");
pvc->spec.storage_bytes = 50 * 1024 * 1024 * 1024;
pvc->spec.storage_class = strdup("fast-ssd");
pvc->spec.num_access_modes = 1;
pvc->spec.access_modes[0] = ACCESS_MODE_READ_WRITE_ONCE;

// Bind
storage_binding_context_t* ctx = storage_binding_context_new();
storage_binding_context_add_pv(ctx, pv);

int result = storage_bind_pvc(ctx, pvc);
if (result == 0) {
    printf("Bound: %s to %s\n", 
           pvc->status.volume_name,
           pv->claim_ref);
} else {
    printf("No suitable PV found\n");
}

storage_binding_context_free(ctx);
```

### StatefulSet Example
```c
#include "controller/statefulset.h"

// Create controller
statefulset_controller_t* sts_ctrl = statefulset_controller_new();

// Create StatefulSet
k8s_statefulset_t* sts = k8s_statefulset_new("redis", "default");
sts->spec.replicas = 5;
sts->spec.service_name = strdup("redis");

// Add to controller
statefulset_create(sts_ctrl, sts);

// Reconcile (creates pods)
statefulset_reconcile(sts_ctrl, "redis");

// Result: 5 pods created
// - redis-0
// - redis-1
// - redis-2
// - redis-3
// - redis-4

// List
int count;
k8s_statefulset_t** list = statefulset_list(sts_ctrl, "default", &count);
printf("Found %d StatefulSets\n", count);

// Get specific
k8s_statefulset_t* found = statefulset_get(sts_ctrl, "redis", "default");
printf("Replicas: %d\n", found->spec.replicas);

// Cleanup
statefulset_controller_free(sts_ctrl);
```

## File Structure

**Header Files** (Type definitions):
- `pkg/types/config.h` - ConfigMap, Secret structs & functions
- `pkg/types/storage.h` - PV, PVC, StatefulSet structs & functions

**Implementation**:
- `pkg/types/config.c` - ConfigMap/Secret implementation (228 lines)
- `pkg/types/storage.c` - PV/PVC/StatefulSet implementation (246 lines)

**Controllers**:
- `internal/controller/configmap.c` - ConfigMap CRUD (120 lines)
- `internal/controller/secret.c` - Secret CRUD (120 lines)
- `internal/controller/statefulset.c` - StatefulSet management (190 lines)

**Storage**:
- `internal/storage/storage_binder.c` - PVC-PV matching (131 lines)

**API**:
- `internal/apiserver/week5_endpoints.c` - REST handlers (589 lines)
- `internal/apiserver/endpoints.h` - Endpoint declarations

## Status Codes

| Code | Meaning |
|------|---------|
| 200 | OK (GET/successful operation) |
| 201 | Created (POST success) |
| 204 | No Content (DELETE success) |
| 400 | Bad Request (invalid JSON/parameters) |
| 404 | Not Found |
| 500 | Internal Server Error |

## Phase Constants

```c
// PersistentVolume Status
VOLUME_STATUS_AVAILABLE  = 0  // Ready for claiming
VOLUME_STATUS_BOUND      = 1  // Bound to a PVC
VOLUME_STATUS_RELEASED   = 2  // Released from PVC
VOLUME_STATUS_FAILED     = 3  // Binding failed/error

// Access Modes
ACCESS_MODE_READ_WRITE_ONCE   = 0  // RWO - single node RW
ACCESS_MODE_READ_ONLY_MANY    = 1  // ROX - many nodes RO
ACCESS_MODE_READ_WRITE_MANY   = 2  // RWX - many nodes RW
```

## Limits

- Max ConfigMaps per namespace: 10,000
- Max ConfigMap items: 100 key-value pairs
- Max Secrets per namespace: 10,000
- Max Secret items: 100 key-value pairs
- Max PersistentVolumes: 1,000
- Max PersistentVolumeClaims per namespace: 1,000
- Max StatefulSet replicas: Limited by pod count (default 10,000)

## Performance

| Operation | Time Complexity |
|-----------|-----------------|
| List ConfigMaps | O(n) where n = total CMs |
| Get ConfigMap | O(n) namespace scan |
| Create ConfigMap | O(1) array append |
| Delete ConfigMap | O(n) removal & shift |
| PVC-PV Binding | O(pv) where pv = available PVs |
| StatefulSet Reconcile | O(replicas) |

## Troubleshooting

**PVC not binding to PV?**
- Check PV status: should be AVAILABLE
- Check storage class match (exact name or empty)
- Check PV capacity ≥ PVC requested
- Check access mode compatibility

**StatefulSet pods not created?**
- Check controller is running
- Check replicas > 0 in spec
- Verify service_name is set
- Check pod count limit not exceeded

**ConfigMap/Secret not found?**
- Verify namespace is correct
- Check name spelling
- Ensure POST returned 201 (created)
- Use LIST endpoint to verify existence
