# Sirah Kubernetes Cluster - Startup Guide

**Quick Start for Running the Complete Sirah Cluster**

---

## 📦 What's Included

Sirah is a lightweight Kubernetes implementation with QEMU unikernel support. The cluster consists of:

- **API Server** - REST API for managing cluster resources
- **Scheduler** - Assigns pods to nodes  
- **Controller Manager** - Manages pod lifecycle and deployments
- **Kubelet** - Node agent that executes pods (with QEMU unikernel support)
- **etcd** - Persistent storage backend

---

## 🚀 Quick Start (3 commands)

### 1. Compile everything
```bash
cd sirah
make
```

### 2. Setup and start the cluster
```bash
./setup-and-run.sh
```

### 3. Test it works
```bash
curl http://localhost:6443/api/v1/nodes
```

That's it! You now have a running Kubernetes cluster.

---

## 📚 Available Scripts

### 1. `setup-and-run.sh` - **Recommended for most users**
Complete setup and startup of all components with etcd.

**What it does:**
- Checks dependencies (etcd, netcat)
- Creates required directories
- Starts etcd
- Starts all 4 Sirah components
- Displays connection information and logs

**Usage:**
```bash
./setup-and-run.sh
```

**Requirements:**
- etcd installed: `sudo apt-get install etcd-server`
- netcat: `sudo apt-get install netcat-openbsd`

---

### 2. `sirah.sh` - **Advanced cluster management**
Full-featured cluster management tool with interactive shell.

**Features:**
- Start/stop/restart the cluster
- View component status
- Stream logs from any component
- Run connectivity tests
- Create/list/delete pods
- Interactive shell for cluster commands

**Usage:**
```bash
# Start the cluster
./sirah.sh start

# Check status
./sirah.sh status

# View logs
./sirah.sh logs kubelet
./sirah.sh logs apiserver

# Test connectivity
./sirah.sh test

# Manage pods
./sirah.sh create-pod my-test-pod
./sirah.sh list-pods
./sirah.sh delete-pod my-test-pod

# Interactive shell
./sirah.sh shell
```

**Interactive shell commands:**
```
status              Show cluster status
pods                List all pods
nodes               List all nodes
create <name>       Create a test pod
delete <name>       Delete a pod
curl <args>         Run curl directly against API
help                Show all commands
exit                Exit the shell
```

**Example:**
```bash
$ ./sirah.sh shell
sirah> status
sirah> create my-pod
sirah> pods
sirah> curl /api/v1/nodes
sirah> exit
```

---

### 3. `start-sirah.sh` - **Full cluster startup with monitoring**
Comprehensive startup script with detailed progress and statistics.

**What it does:**
- Validates all binaries
- Checks QEMU environment
- Starts all 4 components with error checking
- Waits for API server readiness
- Runs connectivity tests
- Displays all PIDs and log locations
- Keeps running (Ctrl+C to stop)

**Usage:**
```bash
./start-sirah.sh
```

---

### 4. `stop-sirah.sh` - **Graceful shutdown**
Stops all running components.

**Usage:**
```bash
./stop-sirah.sh
```

---

## 🔧 Component Details

### API Server (Port 6443)
REST API for cluster operations.

**Log:** `tail -f /tmp/sirah-logs/apiserver.log`

**Key endpoints:**
- `/healthz` - Health check
- `/api/v1/nodes` - List nodes
- `/api/v1/namespaces` - List namespaces
- `/api/v1/namespaces/default/pods` - List pods
- `POST /api/v1/namespaces/default/pods` - Create pod

### Scheduler
Assigns unscheduled pods to nodes.

**Log:** `tail -f /tmp/sirah-logs/scheduler.log`

### Controller Manager
Manages pod replicas, deployments, jobs, etc.

**Log:** `tail -f /tmp/sirah-logs/controller.log`

### Kubelet (Node: worker1)
Executes pods on the local node using QEMU.

**Log:** `tail -f /tmp/sirah-logs/kubelet.log`

### etcd
Persistent key-value storage for cluster data.

**Log:** `tail -f /tmp/sirah-logs/etcd.log`

---

## 🧪 Testing the Cluster

### 1. Basic connectivity test
```bash
curl http://localhost:6443/api/v1/nodes
curl http://localhost:6443/api/v1/namespaces
```

### 2. Create a test pod
```bash
curl -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H 'Content-Type: application/json' \
  -d '{
    "apiVersion": "v1",
    "kind": "Pod",
    "metadata": {
      "name": "test-pod",
      "namespace": "default"
    },
    "spec": {
      "containers": [
        {
          "name": "app",
          "image": "unikernel.img"
        }
      ]
    }
  }'
```

### 3. Check pod status
```bash
curl http://localhost:6443/api/v1/namespaces/default/pods/test-pod
```

### 4. List all pods
```bash
curl http://localhost:6443/api/v1/namespaces/default/pods
```

### 5. Delete the pod
```bash
curl -X DELETE http://localhost:6443/api/v1/namespaces/default/pods/test-pod
```

### 6. Using the management script
```bash
./sirah.sh test           # Run full connectivity tests
./sirah.sh create-pod my-pod   # Create a pod
./sirah.sh list-pods      # List pods
./sirah.sh delete-pod my-pod   # Delete a pod
```

---

## 📊 Monitoring & Logs

### View specific component logs
```bash
tail -f /tmp/sirah-logs/apiserver.log
tail -f /tmp/sirah-logs/kubelet.log
tail -f /tmp/sirah-logs/scheduler.log
tail -f /tmp/sirah-logs/controller.log
tail -f /tmp/sirah-logs/etcd.log
```

### View all logs
```bash
tail -f /tmp/sirah-logs/*.log
```

### Check process status
```bash
ps aux | grep sirah
ps aux | grep etcd
```

### Using the management script
```bash
./sirah.sh status           # Show component status
./sirah.sh logs apiserver   # Stream API server logs
./sirah.sh logs kubelet     # Stream kubelet logs
```

---

## 🛠️ Troubleshooting

### API Server fails to start
**Error:** "Failed to start HTTP server on port 6443"

**Solution:**
- Check if port 6443 is already in use: `lsof -i :6443`
- Kill existing process: `pkill sirah-apiserver`
- Check etcd is running: `curl http://localhost:2379/version`

### etcd connection error
**Error:** "failed to dial default client"

**Solution:**
- Install etcd: `sudo apt-get install etcd-server`
- Start etcd manually: `etcd --listen-client-urls http://localhost:2379`
- Check etcd is running: `ps aux | grep etcd`

### Permission denied when creating directories
**Error:** "Cannot create /var/lib/sirah..."

**Solution:**
- Run the script with sudo: `sudo ./setup-and-run.sh`
- Or create directories first: `sudo mkdir -p /var/lib/sirah/{vms,unikernels}`

### Kubelet not executing pods
**Error:** Pod stays in "Pending" state

**Solution:**
- Check kubelet logs: `tail -f /tmp/sirah-logs/kubelet.log`
- Verify QEMU is installed: `which qemu-system-x86_64`
- Check VM directory: `ls -la /var/lib/sirah/vms/`

### QEMU not found
**Error:** "qemu-system-x86_64: command not found"

**Solution:**
- Install QEMU: `sudo apt-get install qemu-system-x86`
- Verify: `which qemu-system-x86_64`

---

## 📝 Environment Variables

Customize cluster behavior:

```bash
# Node name
export NODE_NAME=worker1
./setup-and-run.sh

# API server port
export API_PORT=8443
./sirah.sh

# Log level
export LOG_LEVEL=debug
./setup-and-run.sh
```

---

## 🎯 Example Workflows

### Workflow 1: Simple pod creation
```bash
# Start cluster
./setup-and-run.sh

# In another terminal, create pod
./sirah.sh create-pod my-app

# Check status
./sirah.sh list-pods

# View logs
./sirah.sh logs kubelet

# Cleanup
./sirah.sh delete-pod my-app
```

### Workflow 2: Interactive cluster shell
```bash
# Start cluster
./sirah.sh start

# Open interactive shell
./sirah.sh shell

# In the shell:
sirah> status
sirah> create web-app
sirah> pods
sirah> curl /api/v1/nodes
sirah> exit

# Stop cluster
./sirah.sh stop
```

### Workflow 3: Development/testing
```bash
# Start with full logging
./start-sirah.sh

# In another terminal
watch -n 1 'tail -1 /tmp/sirah-logs/*.log'

# Create test pod
curl -X POST http://localhost:6443/api/v1/namespaces/default/pods ...

# Monitor logs in real-time
tail -f /tmp/sirah-logs/kubelet.log
```

---

## 🚪 Accessing the Cluster

### From local machine
```bash
# API Server
curl http://localhost:6443/api/v1/nodes

# Inside WSL
curl http://localhost:6443/api/v1/nodes
```

### Using kubectl (if installed)
```bash
# Configure kubectl to use Sirah
kubectl config set-cluster sirah --server=http://localhost:6443
kubectl config set-context sirah --cluster=sirah
kubectl config use-context sirah

# Test
kubectl get nodes
kubectl get pods
```

---

## 📈 Next Steps

1. **Add more unikernel images**
   ```bash
   cp your-unikernel.img /var/lib/sirah/unikernels/
   ```

2. **Create deployments**
   ```bash
   curl -X POST http://localhost:6443/api/v1/namespaces/default/deployments ...
   ```

3. **Setup monitoring**
   ```bash
   tail -f /tmp/sirah-logs/*.log
   ```

4. **Run benchmarks**
   - Pod creation time
   - Memory usage per pod
   - Boot time comparison with containers

---

## 📚 Additional Resources

- [Sirah Architecture](QEMU_COMPATIBILITY_LAYER.md)
- [QEMU Integration Guide](QEMU_QUICK_START.md)
- [API Completeness](API_COMPLETENESS.md)
- [Build Instructions](BUILD.md)

---

## ✅ Checklist for Running the Full Cluster

- [ ] Make built all binaries (`make`)
- [ ] etcd installed (`apt-get install etcd-server`)
- [ ] netcat installed (`apt-get install netcat-openbsd`)
- [ ] Unikernel image in `/var/lib/sirah/unikernels/`
- [ ] Run `./setup-and-run.sh`
- [ ] Test with `./sirah.sh test`
- [ ] Create pods with `./sirah.sh create-pod my-pod`
- [ ] Monitor logs with `tail -f /tmp/sirah-logs/*.log`

---

## 🎉 Summary

You now have:
- ✅ A complete Kubernetes cluster
- ✅ QEMU unikernel support for lightweight pods
- ✅ Full cluster management tools
- ✅ Persistent storage (etcd)
- ✅ Pod scheduling and lifecycle management

**Start your cluster now:**
```bash
./setup-and-run.sh
```

**Manage your cluster:**
```bash
./sirah.sh status
./sirah.sh create-pod my-app
./sirah.sh list-pods
```

---

*Ready to run Kubernetes with unikernels!* 🚀
