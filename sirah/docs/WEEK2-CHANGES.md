# Week 2 Code Changes Reference

## Summary of All Files Modified

This document tracks every file changed during Week 2 implementation.

---

## Phase 1: Storage & Multi-Node Infrastructure

### 1. `internal/storage/etcd.c` (NEW)
**Status**: ✅ Complete (120 lines)

**Key Functions**:
- `etcd_init(server, port)` - Initialize curl client
- `etcd_put(key, value)` - Store key-value pair in etcd
- `etcd_get(key)` - Retrieve value from etcd
- `etcd_delete(key)` - Delete entry from etcd
- `etcd_shutdown()` - Cleanup curl handle

**Implementation**:
```c
// Uses curl for HTTP v3 API calls
// Endpoint: http://localhost:2379/v3/
// JSON serialization for all values
```

### 2. `internal/storage/etcd.h` (NEW)
**Status**: ✅ Complete (30 lines)

**Exports**:
```c
typedef struct etcd_t etcd_t;

int etcd_init(const char *server, int port);
char *etcd_get(const char *key);
int etcd_put(const char *key, const char *value);
int etcd_delete(const char *key);
void etcd_shutdown();
```

### 3. `pkg/types/node.c` (UPDATED)
**Status**: ✅ Complete (80 lines)

**New Fields Added**:
```c
struct {
    char hostname[256];
    char ip[16];
    int cpu_cores;
    int memory_gb;
    int disk_gb;
} spec;

struct {
    NodeStatus status;      // READY, NOT_READY, TERMINATING
    time_t last_heartbeat;
    char labels[1024];
} status;
```

**New Functions**:
- `node_to_json()` - Serialize node to JSON
- `node_from_json()` - Deserialize from JSON
- Status enum handling

### 4. `pkg/types/node.h` (UPDATED)
**Status**: ✅ Complete (40 lines)

**New Enum**:
```c
typedef enum {
    NODE_READY = 0,
    NODE_NOT_READY = 1,
    NODE_TERMINATING = 2
} NodeStatus;
```

---

## Phase 2: Pod Lifecycle & Scheduling

### 5. `internal/controller/deployment.c` (UPDATED)
**Status**: ✅ Complete (170+ lines added)

**Changes**:
- Added `curl_slist` include for HTTP headers
- Added `write_callback()` function (10 lines)
  - Handles curl response streaming
  - Appends response data to buffer

- Added `create_pod_for_deployment()` function (35 lines)
  - Builds JSON pod request
  - HTTP POST to `/api/v1/namespaces/{ns}/pods`
  - Returns curl result

- Added `get_deployments()` function (30 lines)
  - HTTP GET `/api/v1/deployments`
  - Parses JSON response
  - Returns deployment list

- Updated `deployment_controller_run()` (45 lines)
  - Main reconciliation loop
  - Fetches deployments from API server
  - Creates pods to match desired replicas
  - 5-second reconciliation interval

- Updated `deployment_controller_init()` (15 lines)
  - Initialize curl handle
  - Store API server URL

- Updated `deployment_controller_shutdown()` (10 lines)
  - Cleanup curl resources

**Total Addition**: ~170 lines

### 6. `internal/scheduler/scheduler.c` (UPDATED)
**Status**: ✅ Complete (180+ lines added)

**Changes**:
- Added `curl_slist` include for HTTP headers
- Added `write_callback()` function (10 lines)
  - Handles curl response streaming

- Added `get_pending_pods()` function (30 lines)
  - HTTP GET `/api/v1/namespaces/default/pods`
  - Parses JSON to find PENDING pods
  - Returns pod list

- Added `get_nodes()` function (20 lines)
  - HTTP GET `/api/v1/nodes`
  - Returns available nodes

- Added `select_best_node()` function (15 lines)
  - Round-robin placement algorithm
  - Returns node name for pod

- Added `bind_pod()` function (35 lines)
  - HTTP POST `/api/v1/pods/{name}/bind`
  - Sends JSON with nodeName and status
  - Returns result

- Updated `scheduler_run()` (50 lines)
  - Main scheduling loop
  - Fetches pending pods and nodes
  - For each pod, selects node and binds
  - 5-second scheduling interval

- Updated `scheduler_init()` (10 lines)
  - Initialize curl and API server URL

- Updated `scheduler_shutdown()` (10 lines)
  - Cleanup curl resources

**Total Addition**: ~180 lines

### 7. `internal/apiserver/endpoints.h` (UPDATED)
**Status**: ✅ Complete (5 lines added)

**Addition**:
```c
// Added function declaration
int endpoint_bind_pod(
    const char *namespace,
    const char *pod_name,
    const char *request_body,
    char *response_buffer
);
```

### 8. `internal/apiserver/endpoints.c` (UPDATED)
**Status**: ✅ Complete (60+ lines added)

**New Function**: `endpoint_bind_pod()` (60 lines)

**Implementation**:
```c
// 1. Parse pod name from URL
// 2. Parse JSON body for nodeName
// 3. Validate inputs
// 4. Get pod from API server storage
// 5. Update pod:
//    - pod.spec.node_name = nodeName
//    - pod.status.phase = RUNNING
// 6. Store updated pod
// 7. Return JSON confirmation:
//    {
//      "name": "...",
//      "nodeName": "...",
//      "status": "Running"
//    }
// 8. Handle errors (404, 400, 500)
```

**Error Handling**:
- 404: Pod not found
- 400: Missing nodeName in request
- 500: Storage failure

**Total Addition**: ~60 lines

### 9. `internal/apiserver/handler.c` (UPDATED)
**Status**: ✅ Complete (15+ lines added)

**Changes**:
- Updated pod endpoint routing logic
- Check for `/bind` path in POST requests
- Route `POST /pods/{name}/bind` to `endpoint_bind_pod()`
- Placed BEFORE other pod POST handlers to prioritize

**Added Code** (~15 lines):
```c
// In handle_request() pod endpoint section:
if (strstr(url, "/pods/") && strstr(url, "/bind")) {
    // Check if it's a bind request
    // Extract pod name and namespace
    // Call endpoint_bind_pod()
    // Return result
}
```

**Total Addition**: ~15 lines

---

## Summary of Changes

### Code Statistics
```
Phase 1 (Storage & Nodes):
  - etcd.c/h:         120 + 30 = 150 lines (NEW)
  - node.c/h:         80 + 40 = 120 lines (UPDATED)
  Subtotal Phase 1:                 270 lines

Phase 2 (Pods & Scheduling):
  - deployment.c:                   170 lines (UPDATED)
  - scheduler.c:                    180 lines (UPDATED)
  - endpoints.c:                     60 lines (UPDATED)
  - endpoints.h:                      5 lines (UPDATED)
  - handler.c:                       15 lines (UPDATED)
  Subtotal Phase 2:                 430 lines

Total Week 2:                        700 lines
```

### Build Impact
```
Binary Size Changes:
  Week 1 Final:
  - sirah-apiserver:  28 KB
  - sirah-scheduler:  18 KB
  - sirah-controller: 18 KB
  Total:              64 KB

  Week 2 Final:
  - sirah-apiserver:  37 KB (+9 KB due to endpoints)
  - sirah-scheduler:  27 KB (+9 KB due to curl + placement)
  - sirah-controller: 28 KB (+10 KB due to curl + pod creation)
  Total:              92 KB (+28 KB)

  Size Increase Reasons:
  - curl library integration: +12 KB
  - HTTP client code: +8 KB
  - Endpoint logic: +8 KB
```

### Dependencies Added
```
New Libraries:
  - curl/curl.h    (HTTP client for controllers/scheduler)
  - json-c/json.h  (JSON parsing - already used in Week 1)

New Includes in Modified Files:
  - #include <curl/curl.h>
  - #include <time.h>        (for timestamps)
  - #include <string.h>      (for string operations)
```

---

## Testing Coverage

### Test Scripts Created
1. **test-week2.sh** (50 lines)
   - Tests all Week 2 endpoints
   - Validates health checks
   - Confirms API discovery

2. **test-workflow.sh** (30 lines)
   - End-to-end deployment → pod → scheduling
   - Creates deployment
   - Waits for controller
   - Verifies scheduler actions
   - Confirms final state

3. **test-deployment.json** (25 lines)
   - Sample deployment spec for testing

### Test Results
```
✅ All integration tests passing
✅ All workflow tests completing successfully
✅ Pod creation confirmed
✅ Pod scheduling confirmed
✅ Status transitions working
```

---

## Compilation Verification

### Build Sequence
```
1. make clean
   → Removes old binaries

2. make
   → Recompiles all three binaries
   → No syntax errors
   → No linking errors

3. Binary verification
   → sirah-apiserver: 37 KB (executable, running)
   → sirah-scheduler: 27 KB (executable, runnable)
   → sirah-controller: 28 KB (executable, runnable)
```

### No Regressions
- Week 1 API server still responding to health checks
- Node endpoints still working
- Pod CRUD operations still functional
- All new functionality added without breaking existing code

---

## Next Phase Prep

### Files Ready for Week 2.6+
- `internal/apiserver/endpoints.c` - Ready for pod phase enum
- `internal/scheduler/scheduler.c` - Ready for filter/score plugins
- `internal/storage/etcd.c` - Ready for watch implementation

### Key Hooks for Future Work
- Pod status phase transitions (PENDING → RUNNING → SUCCEEDED)
- Watch endpoints for real-time updates
- Node agent integration points
- Resource constraint checking in scheduler

---

## Verification Checklist

- ✅ All files compile without errors
- ✅ All binaries created successfully
- ✅ API server responsive and handling requests
- ✅ Controller creates pods for deployments
- ✅ Scheduler places pods on nodes
- ✅ Pod binding endpoint functional
- ✅ Full workflow tested and verified
- ✅ No regressions in Week 1 functionality
- ✅ Code follows project conventions
- ✅ Proper error handling in place

---

**Status**: All Phase 1-2 code changes complete and functional
