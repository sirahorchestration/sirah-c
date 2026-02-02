# QEMU Compatibility Layer - Quick Start Guide

## 🚀 5-Minute Setup

### 1. Install QEMU

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install qemu-system-x86-64 qemu-kvm libvirt-bin
```

**CentOS/RHEL:**
```bash
sudo yum install qemu-system-x86 qemu-kvm libvirt
```

**macOS:**
```bash
brew install qemu
```

### 2. Initialize Runtime

Add to your Sirah startup code:

```c
#include "unikernel_runtime.h"

int main() {
    // Initialize QEMU backend
    if (unikernel_runtime_init("qemu", "/var/lib/sirah/unikernels") != 0) {
        fprintf(stderr, "Failed to initialize runtime\n");
        return 1;
    }
    
    // Your application code...
    
    // Cleanup
    unikernel_runtime_shutdown();
    return 0;
}
```

### 3. Build

```bash
cd sirah
# Add to Makefile:
# APISERVER_SRC += internal/kubelet/qemu_manager.c internal/kubelet/unikernel_runtime.c

make clean
make
```

### 4. Run

```bash
./bin/sirah-apiserver &
```

---

## 📝 Common Operations

### Create and Run a Pod

```bash
# Create pod with unikernel image
curl -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H "Content-Type: application/json" \
  -d '{
    "apiVersion": "v1",
    "kind": "Pod",
    "metadata": {"name": "unikernel-app"},
    "spec": {
      "containers": [{
        "name": "app",
        "image": "app-unikernel.img",
        "resources": {
          "requests": {"memory": "256Mi", "cpu": "2"}
        }
      }]
    }
  }'
```

### Check Pod Status

```bash
curl http://localhost:6443/api/v1/namespaces/default/pods/unikernel-app
```

### View Logs

```bash
curl http://localhost:6443/api/v1/namespaces/default/pods/unikernel-app/log
```

### Delete Pod

```bash
curl -X DELETE http://localhost:6443/api/v1/namespaces/default/pods/unikernel-app
```

---

## 🖥️ Directory Structure

```bash
# Create directories
mkdir -p /var/lib/sirah/{unikernels,.vms}
chmod 755 /var/lib/sirah/{unikernels,.vms}

# Copy unikernel images
cp app-v1.img /var/lib/sirah/unikernels/
cp nginx.img /var/lib/sirah/unikernels/
ls /var/lib/sirah/unikernels/
```

---

## 🧪 Test Unikernel Image Creation

### Option 1: Simple Busybox Unikernel

```bash
# Using Rumprun (if available)
rumprun-bake hw_generic -r /app -d /tmp/data app-unikernel.img app

# Or use existing unikernel
wget https://example.com/prebuilt-unikernel.img \
  -O /var/lib/sirah/unikernels/test.img
```

### Option 2: Minimal Linux Kernel

```bash
# Create minimal Linux kernel (advanced)
# This would require kernel compilation, typically done via Docker:

docker run --rm -v /tmp:/out alpine:latest \
  /bin/sh -c "apk add linux-headers musl-dev && \
  wget https://kernel.org/linux-5.10.tar.xz && \
  tar xf linux-5.10.tar.xz && cd linux-5.10 && \
  make menuconfig && make -j4 && \
  cp arch/x86_64/boot/bzImage /out/kernel.img"
```

### Option 3: Mock Image for Testing

```bash
# Create a dummy ELF image for testing
python3 << 'EOF'
import struct

# Minimal ELF header
elf_header = bytearray([
    0x7f, 0x45, 0x4c, 0x46,  # Magic
    0x02,                      # 64-bit
    0x01,                      # Little endian
    0x01,                      # ELF version
    0x00,                      # SystemV ABI
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  # Padding
    0x02, 0x00,                # ET_EXEC
    0x3e, 0x00,                # x86-64
    0x01, 0x00, 0x00, 0x00,    # Version 1
])

# Pad to minimum size
while len(elf_header) < 0x10000:  # 64KB
    elf_header.append(0x00)

with open('/var/lib/sirah/unikernels/test.img', 'wb') as f:
    f.write(elf_header)

print("Created test image: test.img")
EOF

chmod 644 /var/lib/sirah/unikernels/test.img
```

---

## 🔍 Verify Installation

### Check QEMU

```bash
# Verify QEMU installation
which qemu-system-x86_64
qemu-system-x86_64 --version

# Test QEMU can start (may need to Ctrl+C to exit)
qemu-system-x86_64 -help | head -20
```

### Check KVM Support

```bash
# Check if KVM is available
grep -c kvm /proc/cpuinfo
# If output > 0, KVM is available
# If output = 0, will use QEMU emulation (slower)

# Check permissions
ls -l /dev/kvm
# Should be readable/writable by your user
sudo usermod -aG kvm $USER

# Or use Docker
docker run --privileged -it ubuntu:20.04 \
  bash -c "apt-get update && apt-get install -y qemu-kvm && qemu-system-x86_64 --version"
```

### Test Runtime Initialization

Create `test_runtime.c`:

```c
#include <stdio.h>
#include "internal/kubelet/unikernel_runtime.h"

int main() {
    printf("Testing unikernel runtime initialization...\n");
    
    int ret = unikernel_runtime_init("qemu", "/var/lib/sirah/unikernels");
    if (ret == 0) {
        printf("✓ Runtime initialized successfully\n");
    } else {
        printf("✗ Runtime initialization failed\n");
        return 1;
    }
    
    // Test creating a container
    char* cid = unikernel_container_create(
        "test-pod", "default", "test.img", 256, 2
    );
    if (cid) {
        printf("✓ Container created: %s\n", cid);
    } else {
        printf("✗ Container creation failed\n");
        return 1;
    }
    
    unikernel_runtime_shutdown();
    printf("✓ Runtime shutdown complete\n");
    return 0;
}
```

Compile and run:
```bash
gcc -o test_runtime test_runtime.c \
  internal/kubelet/unikernel_runtime.c \
  internal/kubelet/qemu_manager.c \
  -ljson-c -lpthread

./test_runtime
```

---

## 🐛 Troubleshooting

### Issue: QEMU not found

```
Error: qemu-system-x86_64: No such file or directory
```

**Solution:**
```bash
# Install QEMU
sudo apt-get install qemu-system-x86-64

# Or find it
which qemu-system-x86_64
/usr/bin/qemu-system-x86_64

# Update path in code
qemu_set_binary_path("/usr/bin/qemu-system-x86_64");
```

### Issue: KVM acceleration not available

```
Warning: KVM not available, using QEMU emulation
```

**Solution:**
```bash
# Check if virtualization enabled in BIOS
grep vmx /proc/cpuinfo    # Intel
grep svm /proc/cpuinfo    # AMD

# If not present, enable virtualization in BIOS

# Disable KVM in code if unavailable
qemu_set_kvm_enabled(0);
```

### Issue: Permission denied for /dev/kvm

```
Error: /dev/kvm: Permission denied
```

**Solution:**
```bash
# Add user to kvm group
sudo usermod -aG kvm $USER
newgrp kvm

# Or use sudo
sudo ./bin/sirah-apiserver
```

### Issue: Socket address already in use

```
Error: Address already in use (port 6443)
```

**Solution:**
```bash
# Find and kill existing process
lsof -i :6443
kill -9 <PID>

# Or use different port
./bin/sirah-apiserver --port 6444
```

---

## 📊 Performance Testing

### CPU & Memory Usage

```bash
# Monitor while pod is running
watch -n 1 'ps aux | grep qemu'

# More detailed
top -p $(pgrep qemu)

# System-wide
vmstat 1
```

### Boot Time Measurement

```bash
# Add timing to pod creation
time curl -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H "Content-Type: application/json" \
  -d @pod.json
```

### Stress Test

```bash
# Create multiple pods
for i in {1..10}; do
  curl -X POST http://localhost:6443/api/v1/namespaces/default/pods \
    -H "Content-Type: application/json" \
    -d "{\"apiVersion\":\"v1\",\"kind\":\"Pod\",\"metadata\":{\"name\":\"pod-$i\"}}" &
done

wait

# Check all running
curl http://localhost:6443/api/v1/namespaces/default/pods | grep -c '"name"'
```

---

## 🔗 Integration Examples

### With kubelet

```c
// In internal/kubelet/kubelet.c
#include "qemu_manager.h"
#include "unikernel_runtime.h"

int kubelet_run_pod(pod_t* pod) {
    // Create and run unikernel container
    char* cid = unikernel_container_create(
        pod->metadata.name,
        pod->metadata.namespace,
        pod->spec.containers[0].image,
        pod->spec.containers[0].resources.memory_mb,
        pod->spec.containers[0].resources.cpu_count
    );
    
    if (!cid) return -1;
    
    if (unikernel_container_run(cid) != 0) {
        unikernel_container_remove(cid);
        return -1;
    }
    
    pod->status.container_id = strdup(cid);
    return 0;
}

int kubelet_stop_pod(pod_t* pod) {
    return unikernel_container_stop(pod->status.container_id, 5);
}
```

### With API Server

```c
// In internal/apiserver/endpoints.c
#include "unikernel_runtime.h"

int endpoint_create_pod(...) {
    // ... existing pod creation code ...
    
    // Before returning, kubelet would run it:
    kubelet_run_pod(&pod);
    
    // Return pod status
    return 0;
}
```

---

## 📚 Additional Resources

- **QEMU Documentation**: https://www.qemu.org/documentation/
- **KVM Documentation**: https://www.linux-kvm.org/
- **Rumprun Guide**: https://github.com/rumpkernel/rumprun/wiki
- **unikernel Docs**: https://unikernel.readthedocs.io/

---

## ✅ Checklist

- [ ] QEMU installed and verified
- [ ] KVM enabled or noted as unavailable
- [ ] `/var/lib/sirah/unikernels` directory created
- [ ] Unikernel images available
- [ ] Runtime initialization working
- [ ] Pod creation functional
- [ ] Logs accessible
- [ ] Graceful shutdown working

---

## Next Steps

1. **Compile**: Add qemu_manager.c and unikernel_runtime.c to Makefile
2. **Test**: Run test_runtime.c to verify initialization
3. **Deploy**: Start sirah-apiserver with QEMU backend
4. **Monitor**: Watch VM creation and resource usage
5. **Benchmark**: Compare performance vs. traditional containers
6. **Scale**: Test with multiple concurrent pods

---

**Status**: ✅ Ready to use  
**Support**: QEMU backend (MVP)  
**Future**: Firecracker, gVisor backends
