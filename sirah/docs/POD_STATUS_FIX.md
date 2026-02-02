# Pod Status JSON Structure Fix

## Problem

Pod status was showing incomplete container state information:

```json
{
  "containerStatuses": [
    {
      "name": "app",
      "ready": false,
      "state": {
        "waiting": null    // ❌ WRONG - missing "reason" field
      }
    }
  ]
}
```

This prevented proper Kubernetes-style status reporting, making it unclear why the container was waiting.

## Solution

Fixed the JSON serialization in [pkg/types/pod.c](pkg/types/pod.c#L103-L125) to properly structure the state object:

### For Pending/Waiting Containers:
```json
{
  "state": {
    "waiting": {
      "reason": "ContainerCreating"
    }
  }
}
```

### For Running Containers:
```json
{
  "state": {
    "running": {}
  }
}
```

## Code Changes

**File:** [pkg/types/pod.c](pkg/types/pod.c#L103-L125)

**Before:**
```c
json_object* state = json_object_new_object();
const char* state_str = "waiting";
if (pod->status.container_statuses[i].state == PHASE_RUNNING) state_str = "running";
json_object* state_obj = json_object_new_object();
json_object_object_add(state_obj, state_str, json_object_new_null());  // ❌ null instead of object
json_object_object_add(cs, "state", state_obj);
```

**After:**
```c
// Properly structured state object
json_object* state = json_object_new_object();
if (pod->status.container_statuses[i].state == PHASE_RUNNING) {
    // Running state
    json_object_object_add(state, "running", json_object_new_object());
} else {
    // Waiting state with reason
    json_object* waiting_obj = json_object_new_object();
    json_object_object_add(waiting_obj, "reason", json_object_new_string("ContainerCreating"));
    json_object_object_add(state, "waiting", waiting_obj);
}
json_object_object_add(cs, "state", state);
```

## Impact

✅ Pod status now matches Kubernetes API structure
✅ Container state includes proper reason field  
✅ Pending pods show: `"waiting": { "reason": "ContainerCreating" }`
✅ Running pods show: `"running": {}`
✅ Clearer status reporting for debugging

## Kubernetes API Conformance

Now properly implements:
- [Pod Status - kubernetes.io](https://kubernetes.io/docs/reference/generated/kubernetes-api/v1.28/#podstatus-v1-core)
- Container Status structure with proper state object
- Waiting state with reason field

## Related Issue

This was the missing piece preventing pods from showing proper status transitions:
1. Pod created → phase="Pending", state="waiting" with reason="ContainerCreating"
2. Controller spawns QEMU VM → phase="Running", state="running"
3. Container is ready → ready=true

The fix ensures the entire transition is visible and properly structured in the API responses.
