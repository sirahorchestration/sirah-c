# Kubernetes Pod Phase Management

Pod phases in Kubernetes v1.28 follow a specific lifecycle controlled by multiple components:

## Pod Phases

```
Pending → Running → (Succeeded | Failed | Unknown)
                  → Terminating (during deletion)
```

### **Pending Phase**
- Pod has been accepted by the cluster but containers haven't been created yet
- May be waiting for:
  - **Scheduler** to assign it to a node
  - Node resources to become available
  - Image to be pulled
  - Secrets/ConfigMaps to be available
  - Volume mounting

### **Running Phase**
- Pod is bound to a node
- At least one container is running (or starting/restarting)
- **All init containers** have completed successfully
- The kubelet on the node has started the containers

### **Succeeded Phase**
- All containers in the pod have terminated with exit code 0
- No containers are restarting
- Pod completed its work successfully
- **Terminal state** - no further state changes

### **Failed Phase**
- At least one container exited with non-zero exit code
- **Terminal state**

### **Unknown Phase**
- Pod state couldn't be determined (rare - communication issue with kubelet)

## How Phase Transitions Work

### **Pending → Running Transition:**

```
1. Scheduler assigns pod to node
   └─> Updates pod.spec.nodeName
   
2. Kubelet on the node:
   └─> Creates container runtime containers
   └─> Pulls images
   └─> Mounts volumes
   └─> Sets up networking
   
3. When first container reaches "running" state:
   └─> Kubelet updates pod.status.phase = "Running"
   └─> Reports container.status.state.running
```

### **Running → Succeeded Transition:**

```
1. Container process exits with code 0
   └─> Kubelet detects termination
   
2. Kubelet updates:
   └─> container.status.state = "terminated"
   └─> container.status.state.exitCode = 0
   
3. If ALL containers have terminated with exitCode 0:
   └─> Kubelet updates pod.status.phase = "Succeeded"
```

### **Running → Failed Transition:**

```
1. Container process exits with code != 0
   OR
2. Kubelet detects container crash
   
3. Kubelet updates:
   └─> container.status.state = "terminated"
   └─> container.status.state.exitCode = (non-zero)
   
4. After all containers terminated:
   └─> Kubelet updates pod.status.phase = "Failed"
```

## Container States (Within Each Pod Phase)

Containers have their own state machine:

```
waiting → running → terminated
   ↑                    ↓
   └────── restart ─────┘
```

### **Waiting State**
```json
{
  "state": {
    "waiting": {
      "reason": "ContainerCreating",  // or other reasons
      "message": "..."
    }
  }
}
```
Reasons include:
- `ContainerCreating` - Image pulling, mounting volumes
- `PullBackOff` - Failed to pull image
- `CreateContainerConfigError` - Invalid container config
- `InvalidImageName` - Malformed image reference

### **Running State**
```json
{
  "state": {
    "running": {
      "startedAt": "2026-01-31T20:00:00Z"
    }
  }
}
```

### **Terminated State**
```json
{
  "state": {
    "terminated": {
      "exitCode": 0,
      "signal": null,
      "reason": "Completed",  // or "Error", "OOMKilled", etc.
      "message": "...",
      "startedAt": "2026-01-31T20:00:00Z",
      "finishedAt": "2026-01-31T20:05:00Z"
    }
  }
}
```

## Key Components Involved

| Component | Role |
|-----------|------|
| **Scheduler** | Assigns pod to node (Pending → waiting for node prep) |
| **Kubelet** | Creates containers, monitors state, reports status updates |
| **API Server** | Stores pod status in etcd |
| **Container Runtime** | Actually runs the containers (Docker, containerd, etc.) |

## How This Works in Sirah

Currently in Sirah:

✅ **Working:**
- Pod creation in Pending phase
- Phase transitions via HTTP PATCH (just fixed!)
- Container state transitions: waiting → running
- Pod persistence in etcd

❌ **Missing:**
- **Automatic kubelet state reporting** - In real K8s, the kubelet monitors actual container runtime and reports changes automatically
- **Actual container runtime** - Sirah uses QEMU simulation, not full container runtime integration
- **Health checks** - Liveness, readiness, startup probes
- **Proper restart policies** - OnFailure, Always handling
- **Graceful shutdown** - PreStop hooks, termination grace period
- **Resource enforcement** - CPU/memory limits actual enforcement

In Sirah, status updates are triggered manually via the controller calling the PATCH endpoint, whereas in real Kubernetes the **kubelet continuously monitors** the container runtime and reports status changes automatically.

---

Summary: What's Missing in Sirah (vs Kubernetes v1.28)
I've just fixed the /version endpoint issue - it now responds at the correct standard path: GET /version

✅ Just Fixed:
GET /version endpoint - Now returns proper v1.28 version information at the standard Kubernetes path (was only at /api/v1/version before)
Major Missing Components in Sirah v1.28 Implementation:
Core API Resources:

❌ StatefulSets (full implementation)
❌ DaemonSets (full implementation)
❌ Ingress
❌ NetworkPolicies (full enforcement)
❌ RBAC (Role, RoleBinding, ClusterRole, ClusterRoleBinding)
❌ Pod Security Policies/Standards
❌ Custom Resource Definitions (CRD) - partial implementation
Control Plane:

❌ Scheduler (current: basic pod assignment)
❌ Storage subsystem (PersistentVolume, PersistentVolumeClaim)
❌ Kubelet (proper node agent)
❌ Container runtime interface (CRI)
❌ Service mesh support
Observability & Management:

❌ Metrics server
❌ Proper event logging
❌ Resource quotas
❌ Namespace isolation enforcement
❌ Pod disruption budgets
API Features:

❌ Watch/streaming for resource changes (partial)
❌ Client library support (go-client, python-client, etc.)
❌ OpenAPI schema endpoint
❌ Proper authentication/authorization plugins
❌ Admission webhooks (custom implementations)
Pod/Container Features:

❌ Init containers
❌ Ephemeral containers
❌ Container probes (liveness/readiness/startup)
❌ Resource limits enforcement
❌ QoS classes
❌ Proper container image pulling
Networking:

❌ CNI (Container Networking Interface)
❌ Service load balancing (proper implementation)
❌ DNS resolution (kube-dns/coredns)
❌ Network policies enforcement
What Sirah Does Have (Partial/Working):
✅ Basic pod CRUD operations
✅ Pod status tracking (just fixed container state transitions!)
✅ Pod phase management (Pending → Running → Succeeded)
✅ Basic deployment/replicaset CRUD
✅ ConfigMap/Secret storage
✅ Job/CronJob definitions
✅ Namespaces (basic)
✅ Nodes (basic)
✅ Services (basic)
✅ etcd integration for persistence
The major gap is that Sirah is more of a pod orchestrator than a full Kubernetes implementation. It handles pod scheduling and status, but lacks most of the production-grade features like proper scheduling, networking, storage, RBAC, and observability that make Kubernetes v1.28 what it is.


