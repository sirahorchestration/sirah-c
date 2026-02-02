# Component Comparison: k0s vs Kubernetes vs Sirah

## Control Plane Components

| Component | k0s | Standard Kubernetes | Sirah |
|-----------|-----|-------------------|-------|
| API Server | Built-in, single process | Separate component | Built-in, single C process (sirah-apiserver) |
| Scheduler | Built-in | Separate component | Built-in, single C process (sirah-scheduler) |
| Controller Manager | Built-in | Separate component | Built-in, single C process (sirah-controller) |
| etcd | SQLite (embedded alternative) | Required external/embedded database | Required external/embedded database |
| kubeconfig | Auto-generated | Manual setup required | Basic HTTP auth (admin:admin) |

## Data Storage

| Aspect | k0s | Standard Kubernetes | Sirah |
|--------|-----|-------------------|-------|
| Default DB | SQLite (embedded) | etcd | In-memory store (ephemeral) |
| Clustering | Etcd-less (uses SQLite) | etcd clustering for HA | No clustering (single instance) |
| High Availability | Via multiple k0s controller nodes syncing SQLite | Native etcd replication | Not implemented |
| Backup | Simple file backup | etcd snapshots | etcd snapshots |
| Data Persistence | File-based | etcd persistence | etcd persistence |

## Node Components

| Component | k0s | Standard Kubernetes | Sirah |
|-----------|-----|-------------------|-------|
| kubelet | Containerized (optional) or native | Native binary | Native C binary (sirah-kubelet) |
| kube-proxy | Containerized | Containerized | Not implemented |
| Container Runtime | Containerd (default), configurable | Configurable (Docker, containerd, etc.) | QEMU (hardware-level VM isolation) |
| Networking | Kube-router (default), customizable | Multiple options (Flannel, Calico, etc.) | Not implemented (QEMU handles isolation) |

## Additional Differences

| Aspect | k0s | Standard Kubernetes | Sirah |
|--------|-----|-------------------|-------|
| CNI Plugin | Built-in Kube-router by default | Must be installed separately | N/A - QEMU isolation |
| Package Management | Single binary deployment | Multiple installation methods | Multiple small C binaries |
| OS Compatibility | Linux optimized (Windows support limited) | Linux primary, Windows nodes available | Linux only |
| Upgrades | Single binary swap | Component-by-component upgrade | Individual binary replacements |
| Worker Node Setup | Simple join token system | kubeadm or manual cluster initialization | Direct pod creation via API |
| Implementation Language | Go | Go | C (minimal dependencies) |
| Resource Footprint | Very lightweight | Standard | Very lightweight (in-memory) |
| Target Use Case | Edge, IoT, embedded | General-purpose orchestration | Unikernel/VM orchestration |

## Feature Completeness

### Core Kubernetes Features

**Both k0s and Standard Kubernetes include:**
- ✅ Pods, Deployments, Services, ConfigMaps
- ✅ RBAC, NetworkPolicies, StorageClasses
- ✅ Helm, Operators compatibility
- ✅ Full kubectl API

**Sirah includes:**
- ✅ Pods (basic CRUD operations)
- ✅ Pod logs (streaming from QEMU)
- ✅ Pod status tracking (Pending → Running transitions)
- ✅ Services (basic endpoints)
- ✅ ConfigMaps, Secrets (basic)
- ⚠️ RBAC (hardcoded auth only)
- ❌ Deployments, StatefulSets, DaemonSets
- ❌ NetworkPolicies, Ingress
- ❌ Helm support
- ❌ Custom Resources (CRDs)

## Key Architectural Differences

### k0s Philosophy
**"Kubernetes, simplified"**
- Single binary containing API Server, Scheduler, and Controller Manager
- Embedded SQLite instead of external etcd
- Eliminates operational overhead by bundling components
- Maintains 100% Kubernetes API compatibility
- Designed for edge computing and resource-constrained environments

### Standard Kubernetes Philosophy
**"Maximum flexibility and scale"**
- Distributed architecture with separate, independently scalable components
- Uses external etcd for true distributed consensus
- Supports multi-master HA with Raft consensus
- Provides granular control over each component
- Designed for enterprise production clusters

### Sirah Philosophy
**"Specialized VM/Unikernel Orchestrator"**
- Lightweight C implementation focused on VM isolation via QEMU
- In-memory data structures for fast pod lifecycle management
- Hardware-level isolation (QEMU VMs) instead of namespace-based containers
- Minimal feature set focused on unikernel/VM workloads
- Trade-offs: No persistence, single-instance, but very efficient for target use case

## Main Differences Summary

| Aspect | k0s | Kubernetes | Sirah |
|--------|-----|-----------|-------|
| **Deployment Model** | Single binary | Multiple components | Multiple small C binaries |
| **Database** | Embedded SQLite | External etcd (HA) | In-memory only |
| **Complexity** | Minimal | High | Very minimal |
| **Scalability** | Medium (up to ~1000 nodes) | Large (100,000+ nodes) | Single machine |
| **HA Setup** | Multiple k0s instances + file sync | etcd cluster + multiple API servers | No HA |
| **Operational Overhead** | Very low | High | Very low |
| **Setup Time** | Minutes | Hours | Minutes |
| **Use Case** | Edge, testing, single clusters | Production enterprise | VM/unikernel orchestration |

## When to Use Each

### Use k0s When:
- ✅ Building edge/IoT clusters
- ✅ Need minimal operational overhead
- ✅ Resource-constrained environments
- ✅ Single-machine deployments
- ✅ Development and testing
- ✅ Need full Kubernetes API but don't want complexity

### Use Standard Kubernetes When:
- ✅ Production enterprise deployments
- ✅ Need true distributed HA with Raft consensus
- ✅ Running 100+ nodes
- ✅ Compliance/certification requirements
- ✅ Complex networking with multiple CNI options
- ✅ Need independent component scaling
- ✅ Existing Kubernetes ecosystem integration

### Use Sirah When:
- ✅ Orchestrating unikernels (MirageOS, IncludeOS, etc.)
- ✅ Running lightweight VMs with hardware isolation
- ✅ Need minimal memory/CPU overhead
- ✅ Specialized VM workloads vs traditional containers
- ✅ Single-machine VM orchestration
- ✅ Development of VM-based systems

## Summary

**k0s vs Kubernetes:**
> k0s eliminates operational overhead by bundling components into a single binary, while standard Kubernetes provides granular control over each component and true distributed HA.

**Sirah:**
> Specialized orchestrator for unikernels and lightweight VMs, trading full feature parity for efficiency and hardware-level isolation via QEMU.
