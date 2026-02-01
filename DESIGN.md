# Design Document - Kubernetes-Compatible Unikernel Platform

## Executive Summary

This document outlines the detailed design decisions for building a **100% Kubernetes API-compatible orchestration platform** from scratch using unikernels. The platform maintains full compatibility with the Kubernetes API while achieving superior performance characteristics through unikernel optimization.

## Design Goals

### Primary Goals
1. **Full API Compatibility**: Pass Kubernetes conformance tests (>85%)
2. **Minimal Footprint**: <100MB base cluster, <50MB per node
3. **Fast Boot**: <2s cluster bootstrap, <500ms component boot
4. **Standard Tooling**: kubectl, Helm, operators work unmodified
5. **Production Ready**: HA, persistent storage, multi-node networking

### Non-Goals (v1.0)
- Kubernetes code reuse or forking
- Windows container support
- Service mesh integration
- GPU scheduling
- Advanced RBAC features

## Technical Design Decisions

### 1. Unikernel Framework Selection

**Decision**: Use unikernel

**Rationale**:
- Modern, actively developed
- Modular library OS architecture
- Strong KVM/Xen support
- Good tooling and documentation
- Community support
- Small footprint potential

**Alternatives Considered**:
- MirageOS: OCaml-based, limited Go support
- IncludeOS: C++ focus, less active
- OSv: Larger footprint, JVM-oriented

### 2. Implementation Language

**Decision**: Go for all components

**Rationale**:
- Kubernetes ecosystem in Go
- Excellent concurrency (goroutines)
- Rich standard library
- Unikernel support available
- gRPC and HTTP libraries
- Easy protobuf integration

**Alternatives Considered**:
- Rust: Steeper learning curve, less K8s ecosystem
- C/C++: More complex, fewer libraries

### 3. etcd Strategy

**Decision**: Port existing etcd to unikernel

**Rationale**:
- Proven, battle-tested
- Full API compatibility guaranteed
- Less implementation risk
- etcd is Go-based (portable)

**Fallback**: Custom minimal implementation if porting fails

**API Requirements**:
- gRPC KV service
- Watch API
- Lease API
- Transaction support
- Raft consensus

### 4. Container Runtime Approach

**Decision**: Custom minimal CRI implementation

**Rationale**:
- Full control over implementation
- Optimized for unikernels
- Smaller footprint than containerd
- Focused feature set for v1.0
- Less dependency complexity

**CRI Implementation**:
```go
// RuntimeService
- RunPodSandbox(config) -> sandboxID
- StopPodSandbox(sandboxID)
- CreateContainer(sandboxID, config) -> containerID
- StartContainer(containerID)
- StopContainer(containerID)
- RemoveContainer(containerID)
- ListContainers(filter)
- ContainerStatus(containerID)
- ExecSync(containerID, cmd)

// ImageService
- PullImage(imageSpec, auth) -> imageID
- RemoveImage(imageID)
- ListImages()
- ImageStatus(imageSpec)
```

**Container Execution**:
- Each container as lightweight unikernel/microVM
- OCI image format support
- Namespace-like isolation via hypervisor
- Resource limits via hypervisor controls

### 5. Networking Model

**Decision**: Simple overlay network for v1.0

**Implementation Options**:
1. **Custom VXLAN overlay** (recommended for MVP)
   - Simple to implement
   - Works across any network
   - Standard encapsulation

2. **Port Flannel** (alternative)
   - Proven solution
   - More complex

3. **eBPF-based** (future)
   - Best performance
   - More development time

**Service Networking**:
- kube-proxy implements iptables-equivalent rules
- ClusterIP: Virtual IPs load-balanced to pods
- NodePort: External access via node ports
- LoadBalancer: Integration with cloud providers (future)

### 6. Storage Strategy

**Decision**: Local path provisioner for v1.0

**Rationale**:
- Simplest implementation
- Sufficient for StatefulSets
- No external dependencies
- Focus on core functionality

**StorageClass**:
```yaml
kind: StorageClass
apiVersion: storage.k8s.io/v1
metadata:
  name: local-storage
provisioner: unikernel.k8s.io/local-path
volumeBindingMode: WaitForFirstConsumer
```

**Future Extensions**:
- NFS provisioner
- Cloud provider integration (EBS, etc.)
- CSI driver support

### 7. API Server Design

**Architecture**:
```
┌─────────────────────────────────────────┐
│           API Server (Port 6443)        │
├─────────────────────────────────────────┤
│  REST Handlers (Gin Framework)          │
│  ├── /api/v1/*                          │
│  ├── /apis/apps/v1/*                    │
│  └── /apis/{group}/{version}/*          │
├─────────────────────────────────────────┤
│  Request Pipeline                        │
│  ├── Authentication (TLS, tokens)       │
│  ├── Authorization (RBAC)               │
│  ├── Admission (validation, mutation)   │
│  └── OpenAPI Schema Validation          │
├─────────────────────────────────────────┤
│  Storage Layer (etcd client)            │
│  ├── CRUD operations                    │
│  ├── Watch streams                      │
│  └── Optimistic locking                 │
└─────────────────────────────────────────┘
```

**Key Design Choices**:
- RESTful API matching K8s exactly
- OpenAPI 3.0 schema validation
- gRPC client to etcd
- Watch API via long-polling or WebSockets
- Admission webhooks support
- RBAC enforcement at authorization layer

### 8. Scheduler Design

**Algorithm**:
```
1. Watch for pods with spec.nodeName == ""
2. For each unscheduled pod:
   a. Filter Phase:
      - Remove nodes not meeting requirements
      - Check resource availability
      - Apply node selectors
      - Evaluate taints/tolerations
   b. Score Phase:
      - Rank remaining nodes
      - Balance resource usage
      - Apply affinity rules
   c. Bind Phase:
      - Update pod.spec.nodeName
      - Write to API server
```

**Scheduling Policies**:
- Resource requests/limits
- Node affinity/anti-affinity
- Pod affinity/anti-affinity
- Taints and tolerations
- Topology constraints

### 9. Controller Manager Design

**Controller Pattern**:
```go
type Controller struct {
    name string
    informer cache.SharedInformer
    queue workqueue.RateLimitingInterface
}

func (c *Controller) Run(stopCh <-chan struct{}) {
    // Start informer
    go c.informer.Run(stopCh)
    
    // Wait for cache sync
    cache.WaitForCacheSync(stopCh, c.informer.HasSynced)
    
    // Worker goroutines
    for i := 0; i < workers; i++ {
        go c.worker()
    }
    
    <-stopCh
}

func (c *Controller) worker() {
    for c.processNextItem() {
    }
}

func (c *Controller) processNextItem() bool {
    key, quit := c.queue.Get()
    if quit {
        return false
    }
    defer c.queue.Done(key)
    
    err := c.reconcile(key)
    if err != nil {
        c.queue.AddRateLimited(key)
    } else {
        c.queue.Forget(key)
    }
    return true
}
```

**Controllers to Implement**:
1. **ReplicaSet**: Maintain desired pod count
2. **Deployment**: Rolling updates, rollback
3. **StatefulSet**: Ordered pod management
4. **DaemonSet**: One pod per node
5. **Job**: Run-to-completion pods
6. **CronJob**: Scheduled jobs
7. **Service**: Manage service endpoints
8. **Node**: Monitor node health
9. **Namespace**: Cleanup on deletion
10. **ResourceQuota**: Enforce limits

### 10. Security Design

**Certificate Architecture**:
```
Root CA
├── API Server Certificate
├── etcd Server Certificate
├── etcd Peer Certificate
├── kubelet Client Certificates (per node)
├── Scheduler Client Certificate
├── Controller Manager Client Certificate
└── kube-proxy Client Certificates
```

**RBAC Model**:
- ClusterRole: Cluster-wide permissions
- Role: Namespace-scoped permissions
- ClusterRoleBinding: Bind ClusterRole to subjects
- RoleBinding: Bind Role to subjects
- ServiceAccount: Pod identity

**Default Roles**:
- system:admin (cluster admin)
- system:node (kubelet)
- system:kube-scheduler
- system:kube-controller-manager
- system:kube-proxy

### 11. Build System Design

**Build Pipeline**:
```
1. Component Build (Go)
   └── go build -o bin/{component}

2. Unikernel Configuration (unikernel)
   └── kraft configure --with-go

3. Unikernel Build
   └── kraft build

4. Image Packaging
   └── Generate bootable unikernel image

5. Testing
   └── Unit tests, integration tests

6. Release Artifacts
   └── Unikernel images, checksums, manifests
```

**Makefile Targets**:
```makefile
all: build-components build-unikernels
build-components: api-server scheduler controller kubelet ...
build-unikernels: wrap components in unikernels
test: unit-tests integration-tests
e2e: conformance-tests
clean: remove build artifacts
install: deploy to cluster
```

### 12. Testing Strategy

**Test Levels**:

1. **Unit Tests**
   - Component logic
   - API handlers
   - Controller reconciliation
   - Coverage target: >80%

2. **Integration Tests**
   - Component interaction
   - API server + etcd
   - Scheduler + API server
   - Controller + API server

3. **E2E Tests**
   - Full cluster scenarios
   - Pod lifecycle
   - Service networking
   - Persistent storage

4. **Conformance Tests**
   - Kubernetes conformance suite
   - Validates API compatibility
   - Target: >85% passing

**Test Infrastructure**:
```
tests/
├── unit/
│   ├── api_test.go
│   ├── scheduler_test.go
│   └── controller_test.go
├── integration/
│   ├── apiserver_etcd_test.go
│   └── scheduler_apiserver_test.go
├── e2e/
│   ├── pod_lifecycle_test.go
│   ├── service_networking_test.go
│   └── storage_test.go
└── conformance/
    └── sonobuoy_test.go
```

## Performance Optimization

### Memory Optimization
- Minimal Go allocations
- Object pooling for frequently created objects
- Efficient watch caching
- Compressed storage in etcd

### Boot Time Optimization
- Lazy initialization
- Parallel component startup
- Precompiled unikernel images
- Minimal dependency loading

### Network Optimization
- HTTP/2 for API communication
- gRPC streaming for watch API
- Connection pooling
- Efficient serialization (protobuf)

## Monitoring & Observability

**Metrics** (Prometheus-compatible):
- API request latency
- Controller reconciliation time
- Scheduler decision time
- etcd operation latency
- Pod startup time
- Resource utilization

**Logging**:
- Structured logging (JSON)
- Component logs aggregation
- Audit logging for API operations
- Debug levels for troubleshooting

**Events**:
- Kubernetes event objects
- Pod lifecycle events
- Node status changes
- Controller actions

## Deployment Model

**Single-Node Development**:
```
components/
├── api-server.img (unikernel)
├── etcd.img (unikernel)
├── scheduler.img (unikernel)
├── controller-manager.img (unikernel)
└── kubelet.img (unikernel)
```

**Multi-Node Production**:
```
Control Plane Nodes (3x HA):
├── api-server (load-balanced)
├── etcd (clustered)
├── scheduler (leader-elected)
└── controller-manager (leader-elected)

Worker Nodes (N):
├── kubelet
├── kube-proxy
└── container-runtime
```

## Migration Path

This platform is designed for **new deployments**, not migration from existing Kubernetes. However, workloads can migrate via:
1. Export manifests from existing K8s
2. Apply manifests to this platform
3. Verify workload behavior
4. Cutover DNS/traffic

## Success Metrics

- [ ] <2s cluster bootstrap time
- [ ] <100MB base cluster memory
- [ ] <50MB per node overhead
- [ ] >85% conformance tests passing
- [ ] kubectl 100% compatible
- [ ] Helm charts work unmodified
- [ ] Multi-node cluster stable for 7+ days
- [ ] 100+ node scale testing passed

## Open Questions

1. Should we support Docker/containerd runtimes in addition to custom CRI?
2. What level of CNI plugin compatibility should v1.0 target?
3. Should we implement cloud provider interfaces (LoadBalancer, PV)?
4. How deep should RBAC implementation go (webhook modes, aggregation)?
5. Should we support alpha/beta Kubernetes APIs or stable only?

## References

- Kubernetes API Conventions
- Kubernetes Enhancement Proposals (KEPs)
- CRI Specification
- unikernel Documentation
- etcd Documentation
