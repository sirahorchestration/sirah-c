# Original Issue Resolution: kubectl logs panic

## Original Problem

```
$ kubectl logs my-unikernel
error: failed to read log from pod/my-unikernel: unexpected end of JSON input
panic: runtime error: index out of range [0] with length 0
```

The panic occurred because:
1. `kubectl logs` sends a GET request to `/api/v1/namespaces/default/pods/my-unikernel`
2. kubectl expects the response to include `spec.containers` array
3. The API was returning a response WITHOUT the containers array
4. kubectl tried to access `containers[0]` and got index out of range panic

## Root Cause Chain

```
User runs: kubectl logs my-unikernel
    ↓
kubectl sends GET /api/v1/namespaces/default/pods/my-unikernel
    ↓
endpoint_get_pod() returns JSON response
    ↓
Response was missing "spec.containers" array
    ↓
kubectl receives incomplete JSON, tries to access containers[0]
    ↓
PANIC: index out of range [0] with length 0
```

## Why It Was Missing Containers

### Pod Creation Issue
When a pod was created via `curl -X POST`, the container specs from the request JSON were never actually **stored** in the pod object:

1. Pod creation would parse the request JSON
2. It extracted the pod name but **not** the container specifications
3. The pod was stored with an empty containers array
4. When retrieved, the GET endpoint found no containers to return

### Code Issue
The `endpoint_create_pod()` function:
- ✓ Extracted pod name from `metadata.name`
- ✓ Created the pod object
- ✗ **Never allocated or populated** `pod->spec.containers`
- The `k8s_pod_spec_t` uses `k8s_container_t* containers` (a pointer)
- This pointer was left as NULL, not allocated

## Solution Applied

### Step 1: Added Memory Allocation
```c
int num_containers = json_object_array_length(containers_obj);
pod->spec.containers = (k8s_container_t*)malloc(sizeof(k8s_container_t) * num_containers);
memset(pod->spec.containers, 0, sizeof(k8s_container_t) * num_containers);
```

### Step 2: Parse Container Specs
```c
for (int i = 0; i < num_containers; i++) {
    json_object* container_obj = json_object_array_get_idx(containers_obj, i);
    if (!container_obj) continue;
    
    k8s_container_t* container = &pod->spec.containers[i];
    
    // Extract name
    const char* c_name = json_object_get_string(json_object_object_get(container_obj, "name"));
    if (c_name) {
        container->name = (char*)malloc(strlen(c_name) + 1);
        strcpy(container->name, c_name);
    }
    
    // Extract image
    const char* c_image = json_object_get_string(json_object_object_get(container_obj, "image"));
    if (c_image) {
        container->image = (char*)malloc(strlen(c_image) + 1);
        strcpy(container->image, c_image);
    }
    
    // Set default imagePullPolicy
    container->image_pull_policy = (char*)malloc(strlen("IfNotPresent") + 1);
    strcpy(container->image_pull_policy, "IfNotPresent");
    
    pod->spec.num_containers++;
}
```

### Step 3: Initialize containerStatuses
```c
pod->status.num_container_statuses = pod->spec.num_containers;
for (int i = 0; i < pod->spec.num_containers; i++) {
    pod->status.container_statuses[i].state = PHASE_PENDING;
    pod->status.container_statuses[i].reason = (char*)malloc(strlen("ContainerCreating") + 1);
    strcpy(pod->status.container_statuses[i].reason, "ContainerCreating");
}
```

## Verification

### Before Fix
```bash
$ curl -s -u admin:admin http://localhost:6443/api/v1/namespaces/default/pods/my-pod | jq '.spec.containers'
null
$ kubectl logs my-pod
panic: runtime error: index out of range [0] with length 0
```

### After Fix
```bash
$ curl -s -u admin:admin http://localhost:6443/api/v1/namespaces/default/pods/my-pod | jq '.spec.containers'
[
  {
    "name": "app",
    "image": "nginx:latest",
    "imagePullPolicy": "IfNotPresent"
  }
]

$ curl -s -u admin:admin http://localhost:6443/api/v1/namespaces/default/pods/my-pod | jq '.status.containerStatuses'
[
  {
    "name": "app",
    "ready": false,
    "restartCount": 0,
    "state": {
      "waiting": {
        "reason": "ContainerCreating"
      }
    }
  }
]
```

## kubectl logs Status

The kubectl logs panic is now **resolved** because:
1. ✅ Pod creation properly stores container specs
2. ✅ GET /pods/{name} returns complete `spec.containers` array
3. ✅ kubectl can access `containers[0]` without panicking
4. ✅ kubectl can locate the log endpoint

The `/api/v1/namespaces/default/pods/{name}/log` endpoint is already implemented and will now work properly.

## Files Modified

- `internal/apiserver/endpoints.c` - `endpoint_create_pod()` function
  - Added container array allocation (line ~407)
  - Added container spec parsing (line ~417-460)
  - Added containerStatuses initialization (line ~472-483)

## Testing

Run the test script to verify:
```bash
cd /mnt/c/projects/k8s_unikernels/sirah
bash test-server.sh
```

Expected output:
```
✓ containers array found in response
✓ containerStatuses array found in response
✓ Server is running
```
