# QEMU Integration Architecture

## Problem Identified

The initial implementation attempted to use direct inter-process function calls (`pod_controller_add_pod()`) between the API server process and the controller process. This doesn't work because:

1. `sirah-apiserver` and `sirah-controller` are **separate compiled binaries**
2. Each process has its own address space
3. Global variables in `pod_controller.c` exist in BOTH processes but are SEPARATE copies
4. Modifications to globals in one process don't affect the other

## Solution Architecture

### Separation of Concerns

- **API Server** (`sirah-apiserver`): Stores pods in its in-memory pod_store
- **Pod Controller** (`sirah-controller`): Queries API server to discover pods

### Data Flow

```
1. User creates pod via:
   POST /api/v1/namespaces/default/pods
   
2. API Server:
   - Stores pod in pod_store
   - Returns 201 Created
   
3. Pod Controller (every 5 seconds):
   - Queries API: GET /api/v1/pods
   - Receives JSON list of all pods
   - Compares with locally tracked pods
   - For new pods: spawns QEMU VM
   - For deleted pods: stops QEMU VM
   - Updates local tracking table
   
4. Pod Status Updates:
   - Controller monitors QEMU processes
   - Updates local pod status
   - (Future: Updates pod status back in API via PATCH)
```

### Inter-Process Communication

Instead of direct function calls, use **HTTP REST API**:

```c
// In pod_controller_fetch_pods():
GET http://localhost:6443/api/v1/pods
→ Returns JSON array of all pods across all namespaces
→ Controller parses JSON and processes pods
```

### Implementation Status

**Completed:**
✓ Runtime abstraction layer (runtime.h/runtime.c)
✓ QEMU executor (qemu.h/qemu.c)
✓ Pod controller framework (pod_controller.h/pod_controller.c)
✓ Controller manager integration
✓ API server pod storage (working)

**In Progress:**
⏳ pod_controller_fetch_pods() - needs HTTP request implementation
⏳ JSON parsing of API response
⏳ Pod comparison logic
⏳ VM spawn triggering based on API pod discovery

**Next Steps:**
1. Implement curl HTTP request in pod_controller_fetch_pods()
2. Parse JSON response from `/api/v1/pods`
3. Extract pod metadata (namespace, name, image)
4. Trigger VM spawn for new pending pods
5. Update pod status in controller's local tracking
