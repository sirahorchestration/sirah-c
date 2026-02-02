# Pod Creation Debugging Guide

## Problem: "Empty reply from server"

When creating a pod with `curl`, you may encounter this error:

```
curl: (52) Empty reply from server
```

This means the API server **crashed or terminated unexpectedly** when handling the request.

### Common Causes

1. **Server binary wasn't recompiled** with the latest pod creation fix
2. **Crash in the pod creation code** (`endpoint_create_pod()`)
3. **Server process isn't running** at all
4. **Memory or resource issue** causing crash

---

## Debugging Steps

### Step 1: Check if Server is Running

```bash
ps aux | grep sirah-apiserver | grep -v grep
```

**Expected output:**
```
jhaigh  12345  0.0  0.5  ... sirah-apiserver
```

**If no output:** Server is not running. Go to Step 2.

### Step 2: Start Server with Error Output

Kill any existing processes:
```bash
pkill -f sirah-apiserver
sleep 1
```

Start the server and capture output:
```bash
/mnt/c/projects/k8s_unikernels/sirah/bin/sirah-apiserver 2>&1 | tee server.log &
sleep 2
```

The server should print startup messages. If it crashes immediately, you'll see the error.

### Step 3: Test Pod Creation

In another terminal, try creating a pod:

```bash
curl -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H 'Content-Type: application/json' \
  -u admin:admin \
  -d '{
    "apiVersion":"v1",
    "kind":"Pod",
    "metadata":{"name":"my-unikernel"},
    "spec":{
      "containers":[{"name":"app","image":"unikernel.img"}]
    }
  }'
```

### Step 4: Check Server Logs

View the server log for any crash messages:

```bash
cat server.log
```

Look for:
- Segmentation faults
- Memory allocation errors
- Assertion failures
- Stack traces

### Step 5: Verify Binary was Recompiled

Check the binary's timestamp to ensure it was recently rebuilt:

```bash
ls -lh /mnt/c/projects/k8s_unikernels/sirah/bin/sirah-apiserver
```

If the timestamp is old (not from today), rebuild:

```bash
cd /mnt/c/projects/k8s_unikernels/sirah
find . -name '*.o' -delete
make
```

---

## Clean Build & Test

If you suspect stale binaries or incomplete compilation:

```bash
cd /mnt/c/projects/k8s_unikernels/sirah

# Remove object files
find . -name '*.o' -delete

# Rebuild
make 2>&1 | tail -20

# Check for errors
make 2>&1 | grep -i error

# Kill old process
pkill -f sirah-apiserver
sleep 1

# Start new server
/mnt/c/projects/k8s_unikernels/sirah/bin/sirah-apiserver 2>&1
```

---

## Testing the Fix

Once server is running, test all pod operations:

### 1. Create a pod
```bash
curl -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H 'Content-Type: application/json' \
  -u admin:admin \
  -d '{
    "apiVersion":"v1",
    "kind":"Pod",
    "metadata":{"name":"test-pod"},
    "spec":{
      "containers":[{
        "name":"app",
        "image":"busybox:latest"
      }]
    }
  }' | jq .
```

**Expected:** HTTP 201 Created, returns pod JSON with spec.containers populated

### 2. Verify containers were stored
```bash
curl -s -u admin:admin http://localhost:6443/api/v1/namespaces/default/pods/test-pod | jq '.spec.containers'
```

**Expected:** Array with container objects:
```json
[
  {
    "name": "app",
    "image": "busybox:latest",
    "imagePullPolicy": "IfNotPresent"
  }
]
```

### 3. Check container statuses
```bash
curl -s -u admin:admin http://localhost:6443/api/v1/namespaces/default/pods/test-pod | jq '.status.containerStatuses'
```

**Expected:** Array with status for each container:
```json
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

### 4. Get pod logs
```bash
curl -s -u admin:admin http://localhost:6443/api/v1/namespaces/default/pods/test-pod/log
```

**Expected:** Returns logs (empty initially if no logs written)

### 5. List all pods
```bash
curl -s -u admin:admin http://localhost:6443/api/v1/namespaces/default/pods | jq '.items | length'
```

**Expected:** Number of pods created (e.g., 1)

---

## If Problem Persists

Collect diagnostic information:

```bash
# Show recent compile errors
cd /mnt/c/projects/k8s_unikernels/sirah
make 2>&1 | grep -E '(error|warning)' | head -20

# Show server startup output
/mnt/c/projects/k8s_unikernels/sirah/bin/sirah-apiserver 2>&1 &
sleep 2
jobs

# Show process info
ps aux | grep sirah

# Test connectivity
curl -v -u admin:admin http://localhost:6443/api/v1/namespaces/default/pods

# Check for core dumps
dmesg | tail -20
```

Share the output of these commands for further debugging.

---

## Key Files Involved

- **Pod creation endpoint:** `internal/apiserver/endpoints.c` (function: `endpoint_create_pod()`)
- **Pod retrieval endpoint:** `internal/apiserver/endpoints.c` (function: `endpoint_get_pod()`)
- **Pod list endpoint:** `internal/apiserver/endpoints.c` (function: `endpoint_list_pods()`)
- **Handler routing:** `internal/apiserver/handler.c` (routes HTTP requests to endpoints)
- **Pod store:** `internal/apiserver/endpoints.c` (global: `pod_store`)
- **Pod types:** `pkg/types/pod.h` and `pkg/types/pod.c`

---

## Common Issues & Solutions

| Issue | Cause | Solution |
|-------|-------|----------|
| Empty reply from server | Server crashed | Check logs, rebuild, restart |
| `spec.containers` is empty array | Old binary not recompiled | Run `make` and restart server |
| `curl: (7) Failed to connect` | Server not running | Start with `sirah-apiserver` |
| `curl: (52) Empty reply` | Server exited during request | Check for segfaults in logs |
| `containerStatuses` missing | Old endpoint code | Recompile with `find . -name '*.o' -delete && make` |

