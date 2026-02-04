# Sirah: Kubernetes on Unikernels

**Complete Kubernetes implementation in C, optimized for unikernel deployment.**

![Language](https://img.shields.io/badge/language-C-00599C?logo=c)
![License](https://img.shields.io/badge/license-Apache%202.0-green)

## 🏗️ Architecture Overview

**A 100% Kubernetes API-compatible orchestration platform built from scratch for unikernels.**

This is **not** a Kubernetes fork, distribution, or wrapper—it's a complete reimplementation of the Kubernetes control plane and node components maintaining full API compatibility while optimizing for unikernel characteristics. This is also a learning tool for those who want to learn Kubernetes. See Sirah Enhancement Proposals (SEPS) at https://github.com/sirahorchestration/enhancements. SEPS are our version of the Kubernetes Enhancement Proposals (KEPs). 

## Vision

Build a production-ready orchestration platform where:
- ✅ **kubectl works unmodified**
- ✅ **Helm charts deploy without changes**
- ✅ **Kubernetes operators function correctly**
- ✅ **Passes Kubernetes conformance tests**
- 🚀 **Cluster boots in <2 seconds**
- 💾 **Base footprint <100MB**
- ⚡ **Component boot <500ms**
- 🔒 **Security by design (unikernel isolation)**

## Key Principle

**API Compatibility Over Code Reuse**: We implement Kubernetes API contracts in native unikernel code rather than forking Kubernetes, ensuring compatibility while achieving superior performance.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│         Kubernetes API (100% Compatible)                │
│   kubectl | Helm | Operators | Standard Tools           │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│              Control Plane                              │
│  • API Server      • Scheduler                          │
│  • Controller Manager  • etcd                           │
└─────────────────────────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│              Worker Nodes                               │
│  • kubelet        • Container Runtime (CRI)             │
│  • kube-proxy     • CoreDNS                             │
└─────────────────────────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│         Unikernel Runtime Layer (Firecracker, Qemu)     │
└─────────────────────────────────────────────────────────┘
```

## Features

### Core Kubernetes APIs
- ✅ Pods, Deployments, ReplicaSets, StatefulSets, DaemonSets
- ✅ Services (ClusterIP, NodePort, LoadBalancer)
- ✅ ConfigMaps, Secrets, PersistentVolumes
- ✅ RBAC (Roles, RoleBindings, ServiceAccounts)
- ✅ Jobs, CronJobs
- ✅ Namespaces, ResourceQuotas, LimitRanges

### Unikernel Optimizations
- 🚀 Fast boot times (<500ms components, <2s cluster)
- 💾 Minimal footprint (<50MB per node)
- 🔒 Strong isolation (hypervisor-based)
- ⚡ Optimized performance (no OS overhead)
- 🎯 Purpose-built for cloud-native workloads

## Planned Project Structure

```
/
├── Makefile                  # Build automation
│
├── components/               # Kubernetes components
│   ├── api-server/          # RESTful API endpoint
│   ├── scheduler/           # Pod scheduling
│   ├── controller-manager/  # Control loops
│   ├── kubelet/            # Node agent
│   ├── container-runtime/  # CRI implementation
│   ├── kube-proxy/         # Service networking
│   ├── etcd/               # Data store
│   └── coredns/            # DNS service
│
└── tests/                   # Test suites
    ├── unit/               # Unit tests
    ├── integration/        # Integration tests
    ├── e2e/               # End-to-end tests
    └── conformance/       # K8s conformance
```

## Quick Start

### Prerequisites
- Unikernel development environment
- KVM/QEMU for virtualization
- kubectl installed

### Build Components
```bash
make 

# Build all components
make all

# Run tests
make test
```

### Bootstrap Cluster
```bash
# Create a single-node cluster
bash ./sirah/start-all.sh

# Verify cluster
kubectl cluster-info
kubectl get nodes
```

## Performance Targets

| Metric | Target | Rationale |
|--------|--------|-----------|
| Cluster boot time | <2s | Fast deployment |
| Component boot | <500ms | Rapid recovery |
| Base cluster memory | <100MB | Minimal footprint |
| Per-node overhead | <50MB | Efficient scaling |
| API latency | <10ms | Responsive |
| Conformance tests | >85% | API compatibility |

## Technology Stack

- **Language**: C
- **Unikernel Hypervisor**: Bring your own, QEMU, Firecracker, etc.
- **Data Store**: etcd 
- **Container Runtime**: Custom CRI implementation
- **Networking**: Simple overlay (v1.0)
- **Storage**: Local path provisioner (v1.0)

## Testing

```bash
# Unit tests
make test-unit

# Integration tests
make test-integration

# E2E tests
make test-e2e

# Kubernetes conformance
make conformance
```

## Conformance Testing**: We use [Sonobuoy](https://sonobuoy.io/) for Kubernetes conformance validation, ensuring API compatibility with official Kubernetes specifications.

## Community

- **Issues**: Report bugs and request features
- **Discussions**: Ask questions and share ideas
- **Pull Requests**: Contribute code improvements

## License

Apache 2.0 License - See [LICENSE](LICENSE) file for details.

## Acknowledgments

This project implements Kubernetes APIs as specified by the Cloud Native Computing Foundation. We maintain API compatibility while building a new platform optimized for unikernels.

---

**Note**: This is a clean-room implementation. We do not fork or modify Kubernetes code. We implement the Kubernetes API specification to ensure compatibility with the entire Kubernetes ecosystem.
