# API Stability and Monitoring Script Fix - Summary

## Issues Resolved

### Issue 1: Pod Creation Segmentation Fault ✅ FIXED
**Problem**: Server crashed with "Segmentation fault (core dumped)" when attempting to create pods via curl.

**Root Cause**: In `endpoint_create_pod()`, the code tried to access `pod->spec.containers[i]` without first allocating memory for the containers array. The `k8s_pod_spec_t.containers` is a pointer type that was initialized to NULL.

**Fix Applied**: Added memory allocation before parsing containers:
```c
// Allocate containers array
pod->spec.containers = (k8s_container_t*)malloc(sizeof(k8s_container_t) * num_containers);
if (!pod->spec.containers) { handle_error... }
memset(pod->spec.containers, 0, sizeof(k8s_container_t) * num_containers);
```

**File**: `internal/apiserver/endpoints.c` - `endpoint_create_pod()` function

---

### Issue 2: Null Pointer Access in Pod Listing ✅ FIXED
**Problem**: Server crashed when the `/api/v1/namespaces/{ns}/pods` endpoint was called on pods that had unallocated container pointers.

**Root Cause**: The `endpoint_list_pods()` function tried to access the containers array without checking if it was NULL (which could happen for pods that didn't go through the proper initialization).

**Fix Applied**: Added null checks before accessing container data:
```c
if (pod_store.pods[i]->spec.containers) {
    for (int c = 0; c < pod_store.pods[i]->spec.num_containers; c++) {
        // Access containers safely
    }
}
```

**File**: `internal/apiserver/endpoints.c` - `endpoint_list_pods()` function

---

### Issue 3: Monitoring Script JSON Parsing Failure ✅ FIXED
**Problem**: The `scripts/view-qemu-pods.sh` script failed with JSON parsing error:
```
json.decoder.JSONDecodeError: Expecting value: line 1 column 1 (char 0)
```

**Root Cause**: The script used bash heredocs with piped input to Python:
```bash
echo "$PODS" | python3 << 'PYTHON_SCRIPT'
json.load(sys.stdin)  # This received empty input!
PYTHON_SCRIPT
```

When you pipe data to a heredoc in bash (with single quotes), the heredoc doesn't read from the pipe - it reads from stdin which is closed. The `$PODS` variable was never expanded into Python.

**Fix Applied**: Changed heredoc delimiters from single quotes to no quotes, enabling variable expansion:
```bash
python3 << PYTHON_SCRIPT
import sys, json
pods_json = """$PODS"""  # Now $PODS is expanded!
pods = json.loads(pods_json).get('items', [])
PYTHON_SCRIPT
```

**Files Modified**:
- `scripts/view-qemu-pods.sh` - Lines ~39-53 (NODES parsing)
- `scripts/view-qemu-pods.sh` - Lines ~72-150 (PODS parsing)

---

## Test Results

### Single Pod Test ✅
```
✓ Pod creation successful
✓ Pod retrieval returns full spec with containers array
✓ Container information properly serialized
✓ Status correctly initialized with containerStatuses
```

### Multiple Pods Test ✅
```
✓ Created 3 pods successfully
✓ All pods listed in API response
✓ Monitoring script displays all 3 pods
✓ Summary statistics correct (Total: 3, Pending: 3)
```

### Monitoring Script Test ✅
```
✓ Cluster health check works
✓ Node listing displays (control-plane Ready)
✓ Pod listing shows all pods with proper formatting
✓ Status icons and colors render correctly
✓ QEMU process count displays
✓ Summary statistics calculate correctly
✓ Script no longer crashes with JSON errors
```

### API Stability ✅
```
✓ Server handles sequential requests without crashing
✓ Healthz endpoint responds reliably
✓ Pod creation works on demand
✓ Pod listing works after any number of requests
✓ No memory corruption or segmentation faults
```

---

## Files Modified

1. **internal/apiserver/endpoints.c**
   - `endpoint_create_pod()`: Added container array memory allocation
   - `endpoint_list_pods()`: Added null pointer safety checks

2. **scripts/view-qemu-pods.sh**
   - Fixed heredoc syntax for NODES JSON parsing (line 39)
   - Fixed heredoc syntax for PODS JSON parsing (line 72)
   - Changed from single quotes to unquoted heredoc delimiters to enable variable expansion

---

## How to Verify

Run the comprehensive test:
```bash
cd /mnt/c/projects/k8s_unikernels/sirah
bash comprehensive-test.sh
```

Or test the monitoring script directly:
```bash
./bin/sirah-apiserver &
sleep 2

# Create a pod
curl -X POST -H 'Content-Type: application/json' -u admin:admin \
  -d '{"apiVersion":"v1","kind":"Pod","metadata":{"name":"test","namespace":"default"},"spec":{"containers":[{"name":"app","image":"nginx"}]}}' \
  http://localhost:6443/api/v1/namespaces/default/pods

# Run the monitoring script
bash scripts/view-qemu-pods.sh default
```

---

## Key Learning

The bash heredoc issue was subtle: when using `echo "$VAR" | cmd << 'HEREDOC'`, the pipe and heredoc don't interact as one might expect. The heredoc takes its input from the file descriptor provided to it (stdin of the function), not from the pipe. The proper solution is to either:

1. Use unquoted heredoc delimiter to enable variable expansion
2. Use `-c` instead of heredoc with pipe
3. Use `<<<` redirector instead of pipe + heredoc
4. Use a temporary file

In this case, option 1 (unquoted delimiter) was the cleanest solution.
