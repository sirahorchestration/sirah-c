# NOTICE: Project Separation

This project (`k8s_unikernels`) focuses on the **core Kubernetes orchestration platform** for unikernels.

## Project Focus: k8s_unikernels

This project implements:
- Complete Kubernetes orchestration system in C
- Hypervisor plugin system (Firecracker, QEMU, gVisor)
- Distributed control plane (api-server, scheduler, controller-manager)
- Networking, storage, and runtime management
- 100% Kubernetes API compatibility
- Production-ready platform

---

## Architecture Overview

```
Main Platform (this project)
├── Control Plane Components
│   ├── API Server (6443)
│   ├── Scheduler
│   ├── Controller Manager
│   └── etcd
├── Runtime Components
│   ├── Kubelet (each node)
│   ├── Container Runtime (VM-based)
│   └── CNI/CSI
├── Hypervisor Abstraction
│   ├── Plugin System
│   ├── Firecracker Plugin
│   ├── QEMU Plugin
│   └── gVisor Plugin
└── Shared Libraries
    ├── Networking
    ├── Storage
    ├── Image Management
    └── Utilities
