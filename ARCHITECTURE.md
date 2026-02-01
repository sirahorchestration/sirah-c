# Kubernetes-Compatible Unikernel Orchestration Platform - Architecture (C Implementation)

## Overview

This document describes the architecture of a **100% Kubernetes API-compatible orchestration platform** built in C for minimal overhead and maximum performance. This is not a Kubernetes fork or distribution—it's a complete reimplementation written in C optimized for unikernel characteristics and QEMU/Firecracker VM scheduling.

## Core Principles

1. **API Compatibility Over Code Reuse**: Implement Kubernetes API contracts in native C code rather than forking Kubernetes
2. **C for Performance**: All components in C for minimal footprint, fast startup, and native speed
3. **QEMU/Firecracker as Container Runtime**: Pod = lightweight unikernel VM instance
4. **API-First Development**: Full Kubernetes API compatibility, pass conformance tests
5. **Optimization for Unikernels**: Minimal memory, fast boot, strong isolation

## System Architecture

```
┌────────────────────────────────────────────────────────────────┐
│                    Kubernetes API Surface                      │
│  (kubectl, helm, operators, standard K8s tools)                │
└────────────────────────┬───────────────────────────────────────┘
                         │
┌────────────────────────▼───────────────────────────────────────┐
│        Kubernetes Control Plane (100% Compatible)              │
│  ┌──────────────────┐ ┌──────────────────┐                   │
│  │  API Server      │ │    Scheduler     │                   │
│  │  (Unikernel)     │ │  (Unikernel)     │                   │
│  └──────────────────┘ └──────────────────┘                   │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │  Control Manager (Unikernel)                             │ │
│  └──────────────────────────────────────────────────────────┘ │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │  etcd (Unikernel)                                        │ │
│  └──────────────────────────────────────────────────────────┘ │
└────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────┐
│             Worker Nodes (All Unikernels)                      │
│  ┌────────────────────────┐ ┌────────────────────────┐        │
│  │  kubelet (Unikernel)   │ │  Container Runtime     │        │
│  │  kube-proxy (UK)       │ │  (CRI-compatible)      │        │
│  └────────────────────────┘ └────────────────────────┘        │
└────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────┐
│             Unikernel Runtime Layer                            │
│  Unikernel-based minimal OS                                    │
└────────────────────────────────────────────────────────────────┘
```

## Component Architecture

### Control Plane Components

#### API Server (C)
- **Purpose**: RESTful Kubernetes API endpoint
- **Tech**: C, libmicrohttpd, gRPC-C client
- **Footprint**: **<8MB binary**, 64-128MB memory
- **Port**: 6443 (HTTPS)
- **Dependencies**: libcurl, openssl, zlib
- **Key Features**:
  - Full Kubernetes v1 API implementation
  - JSON serialization and validation
  - OpenAPI schema validation
  - RBAC enforcement
  - Admission webhooks
- **Startup**: <50ms

#### Scheduler (C)
- **Purpose**: Assign pods to nodes
- **Tech**: C, HTTP client, custom algorithm
- **Footprint**: **<5MB binary**, 32-64MB memory
- **Dependencies**: libcurl, pthread
- **Key Features**:
  - Watch API for pending pods
  - Node fitness evaluation
  - Filtering and scoring
  - Affinity/anti-affinity support
  - Taints and tolerations
- **Startup**: <30ms

#### Controller Manager (C)
- **Purpose**: Run Kubernetes control loops
- **Tech**: C, pthread, API client
- **Footprint**: **<10MB binary**, 128-256MB memory
- **Controllers**:
  - Deployment, ReplicaSet, StatefulSet, DaemonSet
  - Job, CronJob
  - Service, Endpoints
  - Node, Namespace lifecycle
- **Features**: Leader election for HA, watch API
- **Startup**: <100ms

#### Distributed KV Store / etcd Clone (C)
- **Purpose**: Kubernetes-compatible distributed data store
- **Tech**: C, RocksDB, Raft, gRPC-C
- **Footprint**: **<12MB binary**, 256MB-1GB memory
- **Ports**: 2379 (gRPC), 2380 (Raft)
- **Key Features**:
  - Kubernetes-compatible gRPC API
  - Raft consensus for HA
  - Watch API for notifications
  - Transaction support
  - Lease management (TTL)
- **Startup**: <100ms per node

### Worker Node Components

#### kubelet / Node Agent (C)
- **Purpose**: Node agent managing unikernel instances
- **Tech**: C, HTTP client, direct Runtime Manager calls
- **Footprint**: **<8MB binary**, 64-128MB memory
- **Port**: 10250 (HTTPS)
- **Dependencies**: libcurl, pthread, openssl
- **Key Features**:
  - Node registration with API server
  - Pod assignment watch API
  - Pod lifecycle management (create, run, delete)
  - Health probe execution
  - Metrics collection and reporting
  - CRI-like interface to Runtime Manager
  - Volume mounting and storage
- **Startup**: <50ms

#### Unikernel Runtime Manager (C)
- **Purpose**: Manage QEMU/Firecracker instance lifecycle
- **Tech**: C, process management, netlink API
- **Footprint**: **<10MB binary**, 128-256MB memory
- **Key Features**:
  - Launch/terminate Firecracker VMs (<100ms boot)
  - Launch/terminate QEMU VMs (full features)
  - Network device setup (veth, bridges)
  - Volume and block device management
  - Instance metrics collection
  - Health monitoring and crash recovery
  - CRI-like interface for kubelet
- **Startup**: <50ms

#### Container Registry (C)
- **Purpose**: Store and retrieve OCI images
- **Tech**: C, HTTP client, tar extraction
- **Footprint**: **<6MB binary**, 64-128MB memory
- **Dependencies**: libcurl, zlib, openssl
- **Key Features**:
  - OCI image pulling from registries
  - Layer caching and deduplication
  - Image extraction to rootfs
  - Registry authentication
  - Image validation
- **Startup**: <50ms

#### Networking Layer (C)
- **Purpose**: Pod-to-pod and service networking
- **Tech**: C, netlink API, raw sockets, iptables
- **Footprint**: **<5MB binary**
- **Key Features**:
  - veth pair and bridge management
  - Pod IP assignment and routing
  - Service IP to endpoint routing
  - Overlay networks (VXLAN)
  - Load balancing across endpoints
- **Startup**: <30ms

#### DNS Service (C)
- **Purpose**: Service name resolution
- **Tech**: C, custom DNS server
- **Footprint**: **<3MB binary**, 32-64MB memory
- **Port**: 53 (UDP/TCP)
- **Key Features**:
  - Kubernetes service resolution
  - Pod DNS configuration
  - Watch API integration
  - Caching for performance

### Hypervisor Plugin Architecture

The system uses a **pluggable hypervisor abstraction layer** allowing seamless switching between multiple runtime engines:

#### Plugin Interface (C Header)
```c
// hypervisor_plugin.h
typedef struct {
    char* name;              // "firecracker", "qemu", "gvisor"
    char* version;
    
    // Initialization
    int (*init)(void);
    int (*cleanup)(void);
    
    // Instance lifecycle
    int (*create_instance)(
        const char* instance_id,
        const char* kernel_path,
        const char* rootfs_path,
        int vcpu,
        int memory_mb,
        struct vm_config* config
    );
    
    int (*start_instance)(const char* instance_id);
    int (*stop_instance)(const char* instance_id);
    int (*delete_instance)(const char* instance_id);
    
    // Networking
    int (*setup_network)(const char* instance_id, struct net_config* cfg);
    int (*get_instance_ip)(const char* instance_id, char* ip_buffer);
    
    // Storage
    int (*attach_volume)(const char* instance_id, const char* volume_path);
    int (*detach_volume)(const char* instance_id, const char* volume_path);
    
    // Metrics & monitoring
    int (*get_metrics)(const char* instance_id, struct vm_metrics* metrics);
    int (*get_console_output)(const char* instance_id, char* output, int size);
    
    // Health & recovery
    int (*is_alive)(const char* instance_id);
    int (*reboot_instance)(const char* instance_id);
    
    // Configuration
    struct vm_capabilities* (*get_capabilities)(void);
} hypervisor_plugin_t;
```

#### Supported Hypervisors

**1. Firecracker Plugin**
- **Boot time**: <100ms
- **Memory overhead**: <20MB per instance
- **Use case**: Lightweight, fast, density-optimized
- **Platforms**:
  - ✅ **Linux**: Native support (requires KVM)
  - ✅ **Windows**: Via WSL2 (Windows Subsystem for Linux 2)
  - ✅ **macOS**: Via Lima or UTM (experimental)
- **Capabilities**:
  - MicroVM execution
  - virtio networking and block devices
  - Minimal feature set (intentional)
  - Best for containerized workloads
- **Configuration (Linux)**:
  ```
  hypervisor: firecracker
  firecracker:
    binary_path: /usr/bin/firecracker
    kernel: /opt/firecracker/vmlinux.bin
    jailer: true  # Use jailer for additional isolation
  ```
- **Configuration (WSL2)**:
  ```
  hypervisor: firecracker
  firecracker:
    binary_path: /usr/bin/firecracker  # From WSL2 distro
    kernel: /opt/firecracker/vmlinux.bin
    jailer: true
    wsl_mode: true  # WSL2-specific optimizations
  ```

**2. QEMU Plugin**
- **Boot time**: 500ms-1s
- **Memory overhead**: 100-256MB per instance
- **Use case**: Full-featured, broad compatibility
- **Platforms**:
  - ✅ **Linux**: Native support (optimal performance with KVM)
  - ✅ **Windows**: Native support (QEMU for Windows) - **Recommended for Windows**
  - ✅ **macOS**: Native support (via Homebrew)
  - ⚠️ **Windows (non-KVM)**: Slower, no hardware acceleration
- **Capabilities**:
  - Full x86_64 emulation
  - Multiple NIC and disk support
  - BIOS, UEFI boot options
  - GPU passthrough (optional, Linux/KVM)
  - Live migration support
  - Full device emulation
- **Configuration (Linux)**:
  ```
  hypervisor: qemu
  qemu:
    binary_path: /usr/bin/qemu-system-x86_64
    kernel: /opt/qemu/vmlinuz
    enable_kvm: true  # KVM acceleration
    additional_args: "-m 512 -enable-kvm"
  ```
- **Configuration (Windows - Native)**:
  ```
  hypervisor: qemu
  qemu:
    binary_path: C:\Program Files\qemu\qemu-system-x86_64.exe
    kernel: C:\opt\qemu\vmlinuz
    enable_kvm: false  # Windows doesn't have KVM
    enable_haxm: true  # Use HAXM acceleration (optional)
    additional_args: "-m 512"
  ```
- **Configuration (WSL2)**:
  ```
  hypervisor: qemu
  qemu:
    binary_path: /usr/bin/qemu-system-x86_64
    kernel: /opt/qemu/vmlinuz
    enable_kvm: true  # KVM available in WSL2
    wsl_mode: true
  ```

**3. gVisor Plugin**
- **Boot time**: <50ms
- **Memory overhead**: 10-50MB per instance
- **Use case**: Application-level sandboxing, security-focused
- **Platforms**:
  - ✅ **Linux**: Native support
  - ✅ **Windows**: Via WSL2 (Windows Subsystem for Linux 2)
  - ⚠️ **macOS**: Limited support (experimental)
- **Capabilities**:
  - Application kernel written in Go
  - Fine-grained system call interception
  - No nested virtualization needed (Linux only)
  - Per-process isolation
  - Security hardening via system call filtering
  - Better compatibility with existing Linux apps
- **Configuration (Linux)**:
  ```
  hypervisor: gvisor
  gvisor:
    binary_path: /usr/bin/runsc
    runtime_spec: runc
    system_call_filter: strict  # Filter dangerous syscalls
    debug: false
  ```
- **Configuration (WSL2)**:
  ```
  hypervisor: gvisor
  gvisor:
    binary_path: /usr/bin/runsc
    runtime_spec: runc
    system_call_filter: strict
    wsl_mode: true
    debug: false
  ```

## Cross-Platform Support

### Windows Support

The platform supports running on Windows in two configurations:

#### 1. Windows + WSL2 (Recommended for Development)
**Setup**:
- Windows 11 with WSL2 enabled
- Linux distribution (Ubuntu 22.04 or later) installed in WSL2
- Hypervisor Platform enabled in Windows
- Docker Desktop with WSL2 backend (optional)

**Supported Hypervisors**:
- ✅ **Firecracker**: Full support via WSL2
- ✅ **QEMU**: Full support via WSL2 with KVM
- ✅ **gVisor**: Full support via WSL2

**Configuration**:
```bash
# In Windows PowerShell (run as Admin)
wsl --list --verbose                    # Check WSL2 status
wsl -d Ubuntu-22.04                      # Enter WSL2 distro

# Inside WSL2:
sudo apt update && sudo apt install firecracker qemu gvisor
./cluster-bootstrap --platform wsl2 --hypervisor firecracker
```

**Advantages**:
- Native Linux environment
- KVM available (faster emulation)
- All hypervisors supported
- Development-grade performance

#### 2. Windows Native (QEMU Only)
**Setup**:
- Windows 10/11 Pro, Enterprise, or Home (with Hyper-V capable CPU)
- QEMU for Windows installed
- Optional: HAXM (Intel Hardware Acceleration) for better performance

**Supported Hypervisors**:
- ✅ **QEMU**: Native support (without KVM, slower)
- ❌ **Firecracker**: Not supported (requires Linux kernel)
- ❌ **gVisor**: Not supported (requires Linux kernel)

**Configuration**:
```powershell
# Download and install QEMU for Windows
# https://www.qemu.org/download/

# Configure cluster
cluster-bootstrap.exe --platform windows --hypervisor qemu

# Or with HAXM acceleration (optional):
cluster-bootstrap.exe --platform windows --hypervisor qemu --acceleration haxm
```

**Performance Comparison**:
| Config | Boot Time | Memory | Performance |
|--------|-----------|--------|-------------|
| QEMU (no accel) | 1-2s | 150-300MB | 50-70% |
| QEMU + HAXM | 800ms-1s | 150-300MB | 70-85% |
| WSL2 + Firecracker | <100ms | 20-50MB | 95%+ |
| WSL2 + QEMU+KVM | 500ms-1s | 100-200MB | 90%+ |

### macOS Support

**Supported Hypervisors**:
- ✅ **QEMU**: Via Homebrew with HVF acceleration
- ⚠️ **Firecracker**: Via Lima (experimental)
- ⚠️ **gVisor**: Limited/experimental

**Configuration**:
```bash
# Install QEMU
brew install qemu

# Configure cluster
cluster-bootstrap --platform macos --hypervisor qemu --acceleration hvf
```

### Linux Support (Optimized)

**Supported Hypervisors**:
- ✅ **Firecracker**: Optimal (native KVM)
- ✅ **QEMU**: Optimal (native KVM)
- ✅ **gVisor**: Optimal (native support)

**Configuration**:
```bash
# Check for KVM support
grep -c -w vmx /proc/cpuinfo  # Intel
grep -c -w svm /proc/cpuinfo  # AMD

# Install
sudo apt install firecracker qemu gvisor

# Configure cluster
cluster-bootstrap --platform linux --hypervisor firecracker
```

**Platform Summary**:

| Platform | Firecracker | QEMU | gVisor | Recommended |
|----------|-------------|------|--------|-------------|
| **Linux** | ✅ Optimal | ✅ Optimal | ✅ Optimal | Firecracker (density) |
| **Windows + WSL2** | ✅ Full | ✅ Full | ✅ Full | Firecracker (via WSL2) |
| **Windows Native** | ❌ No | ✅ Available | ❌ No | QEMU + HAXM |
| **macOS** | ⚠️ Lima | ✅ Good | ⚠️ Experimental | QEMU + HVF |

#### Plugin Loading & Management (C)

```c
// plugin_manager.c

typedef struct {
    hypervisor_plugin_t* plugin;
    void* plugin_handle;  // dlopen handle
    char* name;
    int initialized;
} loaded_plugin_t;

// Load hypervisor plugin dynamically
int load_hypervisor_plugin(const char* name) {
    char plugin_path[256];
    snprintf(plugin_path, sizeof(plugin_path), 
             "/opt/plugins/hypervisor_%s.so", name);
    
    void* handle = dlopen(plugin_path, RTLD_LAZY);
    if (!handle) {
        log_error("Failed to load plugin: %s", dlerror());
        return -1;
    }
    
    // Load plugin interface
    hypervisor_plugin_t* (*get_plugin)(void) = 
        (hypervisor_plugin_t* (*)(void))dlsym(handle, "get_hypervisor_plugin");
    
    if (!get_plugin) {
        log_error("Plugin missing get_hypervisor_plugin symbol");
        dlclose(handle);
        return -1;
    }
    
    hypervisor_plugin_t* plugin = get_plugin();
    
    // Initialize plugin
    if (plugin->init() != 0) {
        log_error("Failed to initialize hypervisor plugin: %s", name);
        dlclose(handle);
        return -1;
    }
    
    // Store loaded plugin
    loaded_plugins[num_plugins].plugin = plugin;
    loaded_plugins[num_plugins].plugin_handle = handle;
    loaded_plugins[num_plugins].name = strdup(name);
    loaded_plugins[num_plugins].initialized = 1;
    num_plugins++;
    
    log_info("Loaded hypervisor plugin: %s", name);
    return 0;
}

// Select hypervisor for pod
hypervisor_plugin_t* select_hypervisor(struct pod* pod) {
    // Check pod annotations for explicit hypervisor request
    const char* requested = get_pod_annotation(pod, "unikernel.io/hypervisor");
    if (requested) {
        return find_loaded_plugin(requested);
    }
    
    // Check pod class hints
    const char* class_hint = get_pod_annotation(pod, "unikernel.io/hypervisor-class");
    if (class_hint) {
        if (strcmp(class_hint, "lightweight") == 0) {
            return find_loaded_plugin("firecracker");
        } else if (strcmp(class_hint, "secure") == 0) {
            return find_loaded_plugin("gvisor");
        } else if (strcmp(class_hint, "compatible") == 0) {
            return find_loaded_plugin("qemu");
        }
    }
    
    // Fall back to default hypervisor
    return find_loaded_plugin(default_hypervisor);
}
```

#### Benefits of Plugin Architecture

| Benefit | Description |
|---------|-------------|
| **Flexibility** | Switch hypervisors per-pod or per-node |
| **Performance Optimization** | Use Firecracker for density, QEMU for features, gVisor for security |
| **Vendor Agnostic** | Support multiple hypervisors without code duplication |
| **Easy Extension** | Add new hypervisors by implementing plugin interface |
| **Security Options** | gVisor for strict isolation, VM-based for full separation |
| **Workload Optimization** | Choose best runtime for each workload type |
| **Gradual Migration** | Move workloads between hypervisors seamlessly |

## Design Principles

### 1. C-Based Implementation for Performance
- **Minimal overhead**: No garbage collection, no runtime initialization
- **Native speed**: Direct execution, no interpretation
- **Small binaries**: Static compilation, <10MB per component
- **Fast startup**: <50ms component startup, <1s cluster
- **Memory efficiency**: <50MB per-node overhead
- **Direct control**: System calls, explicit resource management

### 2. Pluggable Hypervisor Abstraction
- **Multiple backends**: Firecracker (lightweight), QEMU (full-featured), gVisor (app-level sandbox)
- **Dynamic selection**: Choose hypervisor per-pod or per-node
- **Unified interface**: Runtime Manager abstracts hypervisor differences
- **Easy extension**: Add new hypervisors via plugin interface
- **Optimization**: Use appropriate runtime for each workload type

### 3. Modular Component Architecture
Each C component is:
- Independent static binary
- Deployed separately
- Minimal dependencies (libcurl, openssl, zlib)
- Can run as process or unikernel
- Independent lifecycle and scaling

### 4. API-First Development
- Implement Kubernetes API contracts exactly
- Validate against OpenAPI specs
- Test with kubectl, Helm, standard tools
- Pass Kubernetes conformance tests
- 100% ecosystem compatibility

### 5. Optimization for Unikernels
- Minimal memory per instance
- Fast boot times
- No OS overhead
- Strong hypervisor isolation
- Security by design

## Performance Targets (C Implementation)

| Metric | Target | Rationale |
|--------|--------|-----------|
| **Component startup** | <50ms | Native C execution, minimal init |
| **Cluster bootstrap** | <1 second | All components boot in parallel |
| **Pod/VM startup** | 50-100ms | Firecracker lightweight boot |
| **Base cluster memory** | <300MB | All control plane components |
| **Per-node overhead** | <50MB | kubelet + runtime manager |
| **Per-pod memory** | <20MB | Firecracker + minimal OS |
| **API latency (p99)** | <100ms | Direct C HTTP implementation |
| **Pod scheduling** | <100ms | Efficient scheduling algorithm |
| **Total binary size** | <100MB | All components combined |
| **Individual binary** | <15MB | Largest component |
| **Pod density** | 100+ pods/node | With Firecracker optimization |

## Security Architecture

### Isolation
- Each component runs as separate unikernel
- Minimal shared dependencies
- Strong network boundaries
- Process isolation via hypervisor

### Authentication & Authorization
- TLS for all component communication
- Certificate-based authentication
- RBAC enforcement at API server
- ServiceAccount tokens for pods

### Data Protection
- Secret encryption at rest (etcd)
- TLS in transit
- Audit logging
- Pod Security Standards enforcement

## Scalability

### Control Plane HA
- 3-node etcd cluster (Raft)
- Multiple API server replicas
- Leader election for controllers
- Load balancing across replicas

### Worker Node Scaling
- Horizontal scaling to 100+ nodes
- Efficient watch API usage
- Minimal per-node overhead
- Fast node join/leave

## Technology Stack (C Implementation)

| Component | Language | Key Libraries | Rationale |
|-----------|----------|-----------|----------|
| **API Server** | C | libmicrohttpd, libcurl, openssl, jansson | Native performance, minimal overhead |
| **Scheduler** | C | libcurl, pthread | Efficient scheduling, lightweight |
| **Controllers** | C | libcurl, pthread, openssl | Concurrency via pthreads |
| **etcd Clone** | C | RocksDB, grpc-c, openssl | Distributed consensus, gRPC API |
| **kubelet** | C | libcurl, pthread, openssl | Direct API communication |
| **Runtime Manager** | C | pthread, netlink, libcurl | Process management, system calls |
| **Registry** | C | libcurl, zlib, openssl | OCI image handling |
| **Networking** | C | netlink, iptables, pthread | Network configuration |
| **DNS** | C | (minimal) | Custom DNS server |
| **Build System** | Make/Shell | gcc, static linking | Minimal dependencies |

### Why C Over Go for This Project
- **Binary size**: <10MB per component vs 40-100MB for Go
- **Startup time**: <50ms vs 100-200ms for Go with GC
- **Memory overhead**: 10-20MB baseline vs 50-100MB for Go
- **Cluster boot**: <1 second vs 2-3 seconds for Go
- **Native performance**: No GC pauses, direct syscalls
- **Unikernel alignment**: Matches unikernel philosophy

## Future Extensions

### Phase 2+ Features
- Advanced network policies
- GPU scheduling
- Service mesh integration
- Multi-cluster federation
- Advanced storage provisioners
- Observability enhancements
- Pod priority and preemption

## References

- **Kubernetes API Specification**: v1.28+
- **CRI Specification**: Container Runtime Interface
- **OCI Runtime Specification**: Container image and runtime formats
- **etcd API Documentation**: gRPC protocol and semantics
- **Firecracker Documentation**: Lightweight VM manager
- **QEMU Documentation**: Full-featured hypervisor
- **Linux netlink API**: Network configuration
- **RocksDB Documentation**: Embedded key-value store
