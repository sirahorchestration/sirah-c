# Fix: kubectl get pods Now Shows Pod Names ✅

## Problem
When you ran `kubectl get pods`, the NAME column was empty or showed `<unknown>`:

```bash
$ kubectl get pods
NAMESPACE   NAME   AGE
                   <unknown>
                   <unknown>
```

## Root Cause
The API server's pod list endpoint (`GET /api/v1/namespaces/default/pods`) was returning pods in a **flattened format** instead of the proper Kubernetes API structure:

**Wrong (Old):**
```json
{
  "apiVersion": "v1",
  "kind": "PodList",
  "items": [
    {
      "name": "test-pod",
      "namespace": "default"
    }
  ]
}
```

**Correct (Fixed):**
```json
{
  "apiVersion": "v1",
  "kind": "PodList",
  "items": [
    {
      "apiVersion": "v1",
      "kind": "Pod",
      "metadata": {
        "name": "test-pod",
        "namespace": "default"
      },
      "status": {
        "phase": "Pending"
      }
    }
  ]
}
```

## Solution
Fixed two API endpoints in `internal/apiserver/endpoints.c`:

### 1. Fixed `endpoint_list_pods()` (line 48)
- Wrapped pod data in proper `metadata` object
- Added `apiVersion`, `kind`, and `status` fields
- Now returns fully formatted Pod objects

### 2. Fixed `endpoint_list_nodes()` (line 229)
- Wrapped node data in proper `metadata` object
- Added `apiVersion` and `kind` fields  
- Now returns fully formatted Node objects

## Verification

### Before Fix
```bash
$ kubectl get pods
NAMESPACE   NAME   AGE
                   <unknown>
```

### After Fix ✅
```bash
$ kubectl get pods
NAMESPACE   NAME        AGE
default     hello-pod   <unknown>
default     test-pod    <unknown>
```

**Pod names are now visible!**

## Files Modified
- `internal/apiserver/endpoints.c` - Fixed pod and node list endpoints
- `scripts/view-qemu-pods.sh` - Updated to parse new metadata structure

## Testing
```bash
# Create a pod
kubectl run test-pod --image=alpine:latest --restart=Never -- sleep 3600

# See it listed with name ✅
kubectl get pods

# Use visual monitoring tool
bash scripts/view-qemu-pods.sh
```

All tools now work correctly with proper pod names visible!
