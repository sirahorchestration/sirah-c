# Sirah Kubernetes Platform - Build Guide

## Prerequisites

### Linux (Recommended)
```bash
# Ubuntu/Debian
sudo apt-get install build-essential libcurl4-openssl-dev libjson-c-dev \
                     libssl-dev zlib1g-dev uuid-dev

# Fedora/RHEL
sudo dnf install gcc gcc-c++ libcurl-devel json-c-devel openssl-devel \
                 zlib-devel libuuid-devel
```

### macOS
```bash
brew install curl json-c openssl zlib ossp-uuid
```

### Windows (WSL2 Recommended)
Use Ubuntu on Windows Subsystem for Linux 2:
```bash
wsl --install
wsl
sudo apt-get install build-essential libcurl4-openssl-dev libjson-c-dev \
                     libssl-dev zlib1g-dev uuid-dev
```

## Building

### Quick Build
```bash
cd sirah
make
```

This will compile three binaries:
- `bin/sirah-apiserver` - Kubernetes API Server
- `bin/sirah-scheduler` - Pod scheduler
- `bin/sirah-controller-manager` - Controllers (Deployments, ReplicaSets, etc.)

### With Specific Compiler
```bash
make CC=/usr/bin/gcc
```

### Clean Build
```bash
make clean
make
```

### Install (Optional)
```bash
make install  # Installs to /usr/local/bin/
```

## Running

### Start etcd (required)
```bash
# Using Docker
docker run -d --name etcd -p 2379:2379 \
  quay.io/coreos/etcd:latest \
  /usr/local/bin/etcd --advertise-client-urls http://localhost:2379

# Or using kind/etcd locally
# See https://etcd.io/docs/v3.5/quickstart/
```

### Start the API Server
```bash
./bin/sirah-apiserver --port=6443 --etcd=localhost:2379
```

### Start the Scheduler (in another terminal)
```bash
./bin/sirah-scheduler --apiserver=http://localhost:6443
```

### Start the Controller Manager (in another terminal)
```bash
./bin/sirah-controller-manager --apiserver=http://localhost:6443
```

### Configure kubectl
```bash
kubectl config set-cluster sirah \
  --server=https://localhost:6443 \
  --insecure-skip-tls-verify=true

kubectl config set-context sirah --cluster=sirah

kubectl config use-context sirah

# Verify connection
kubectl get nodes
```
  json-c-devel \
  libuuid-devel \
  libcurl-devel \
  openssl-devel
```

### macOS
```bash
brew install json-c ossp-uuid
```

## Building

### Simple Build
```bash
cd sirah
make
```

This will:
1. Compile all C source files
2. Link them together
3. Create `bin/sirah` binary

### Verbose Build
```bash
make CFLAGS="-Wall -Wextra -O2 -fPIC -g -I. -DDEBUG"
```

### Clean Build
```bash
make clean
make
```

### Install
```bash
make install
# Now use: sirah create cluster my-dev
```

## Development Build

For development with debug symbols:
```bash
make clean
make CFLAGS="-Wall -Wextra -g -O0 -fPIC -I. -DDEBUG"
```

## Testing the Build

After building:
```bash
./bin/sirah help
./bin/sirah list clusters
```

## Binary Size

Current Week 1 MVP:
- ~500KB (dynamic linking)
- <10MB with static linking

Target (full implementation):
- <10MB static binary

## Architecture

```
cmd/sirah/
├── main.c          - CLI entry point
├── create.c        - Create cluster command
├── delete.c        - Delete cluster command
├── list.c          - List clusters command
└── kubeconfig.c    - Get kubeconfig command

internal/cluster/
├── cluster.h       - Data structures
└── cluster.c       - Cluster state management

internal/runtime/   - (Task 1.3)
│   └── manager.c   - Hypervisor plugin management

internal/k8s/       - (Task 1.5)
│   └── bootstrap.c - Kubernetes bootstrapping
```

## Next Steps

### Task 1.3: Hypervisor Plugin Integration
- Create `internal/runtime/manager.c`
- Implement dlopen/dlsym for plugin loading
- Support firecracker, qemu, gvisor plugins

### Task 1.4: Network Setup
- Create `internal/cluster/network.c`
- Implement bridge creation
- Setup veth pairs and DNS

### Task 1.5: Kubernetes Bootstrap
- Create `internal/k8s/bootstrap.c`
- Integrate kubeadm
- Configure control plane

## Troubleshooting

### Missing json-c library
```bash
# Ubuntu
sudo apt-get install libjson-c-dev

# macOS
brew install json-c
```

### Missing uuid library
```bash
# Ubuntu
sudo apt-get install uuid-dev

# CentOS
sudo yum install libuuid-devel

# macOS
brew install ossp-uuid
```

### Compilation errors
Clean and rebuild:
```bash
make clean
make
```

Enable verbose output:
```bash
make 2>&1 | head -50
```
