# Fix for kubectl logs panic

## Problem
When running `kubectl logs my-unikernel`, the command crashes with:
```
panic: runtime error: index out of range [0] with length 0
```

This occurs in `kubectl`'s log handler because it expects the pod GET endpoint to return:
1. `spec.containers[]` - Array of container definitions
2. `status.containerStatuses[]` - Array of container status information

## Root Cause
The original pod endpoints (`endpoint_list_pods` and `endpoint_get_pod`) returned minimal JSON:
```json
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": { "name": "...", "namespace": "..." },
  "status": { "phase": "Running" }
}
```

kubectl tries to access the containers array which doesn't exist, causing the panic.

## Solution Applied
Updated both `endpoint_list_pods()` and `endpoint_get_pod()` to return complete pod specifications:

### Changes to endpoint_list_pods():
- ✅ Added `spec.containers[].name` - Container name from pod spec
- ✅ Added `spec.containers[].image` - Container image
- ✅ Added `spec.containers[].imagePullPolicy`
- ✅ Added `spec.containers[].ports[]` - Port definitions
- ✅ Added `spec.nodeName` - Assigned node
- ✅ Added `spec.restartPolicy` - Restart policy
- ✅ Added `status.podIP` - Pod IP address
- ✅ Added `status.hostIP` - Host IP address
- ✅ Added `status.containerStatuses[].name` - Container name
- ✅ Added `status.containerStatuses[].ready` - Ready status
- ✅ Added `status.containerStatuses[].restartCount` - Restart count
- ✅ Added `status.containerStatuses[].state.running` - Running state info
- ✅ Added `status.containerStatuses[].containerID` - Container ID

### Changes to endpoint_get_pod():
- Same as above for single pod retrieval

## Files Modified
- `internal/apiserver/endpoints.c` (lines 50-147 for list_pods, lines 195-290 for get_pod)

## Testing
After compilation, kubectl logs should work:
```bash
kubectl logs my-unikernel
kubectl logs my-unikernel -c container-name
kubectl logs -f my-unikernel  # Follow logs
```

## Technical Details
The fix ensures that:
1. All container definitions from the pod spec are included in the response
2. Container status information is properly structured
3. kubectl can access `items[].spec.containers` without index out of range
4. kubectl can map containers to their statuses for proper log streaming

The changes are backward compatible and follow Kubernetes API v1 pod specification.
