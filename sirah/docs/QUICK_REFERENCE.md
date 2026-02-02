# Quick Reference: QEMU Integration

## TL;DR - What Works Now

✅ QEMU unikernel VMs spawn on WSL2 with container lifecycle logging

## Quick Test

```bash
cd sirah
bash tests/qemu-spawn-test.sh
```

**Expected**: Shows "✅ SUCCESS! 4 QEMU processes spawned!"

## Key Components

### 1. API Server (Port 6443)
- Stores pods
- HTTP REST API
- In-memory storage

```bash
./bin/sirah-apiserver
```

### 2. Pod Controller (Polling)
- Discovers pods every 5 seconds
- Extracts resources (memory, CPU)
- Validates unikernel images
- Spawns QEMU VMs
- Logs container events

```bash
./bin/sirah-controller
```

### 3. QEMU Runtime
- Spawns via `fork()` + `execvp()`
- Logs to `/tmp/qemu-*.log`
- Tracks by PID
- KVM accelerated

## Creating Test Pods

```bash
# Pod with resource limits
curl -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H "Content-Type: application/json" \
  -d '{
    "metadata": {"name": "test-vm", "namespace": "default"},
    "spec": {
      "containers": [{
        "name": "app",
        "image": "/tmp/sirah-unikernels/test-kernel",
        "resources": {
          "memory": "256Mi",
          "cpu": "2"
        }
      }]
    }
  }'
```

## Checking QEMU Processes

```bash
# List all QEMU processes
pgrep -a qemu

# Count QEMU processes
pgrep -c qemu

# Check QEMU resource usage
ps aux | grep qemu

# Check KVM module
cat /sys/module/kvm/refcount  # Should be > 0

# Check nested virtualization
ls -l /dev/kvm  # Should exist
```

## QEMU Log Files

```bash
# Location: /tmp/qemu-<pod-name>.log
ls -lh /tmp/qemu-*.log

# View logs
cat /tmp/qemu-default-my-app.log

# Tail logs
tail -f /tmp/qemu-default-my-app.log
```

## Container Lifecycle Events

```
[POD EVENT] <namespace>/<pod> container=<name> | <event>: <message>

Examples:
[POD EVENT] default/my-app | Pulling: Pulling image...
[POD EVENT] default/my-app | Pulled: Successfully pulled image
[POD EVENT] default/my-app | Creating: Creating unikernel VM...
[POD EVENT] default/my-app | Created: VM instance created successfully
[POD EVENT] default/my-app | Started: VM booting up...
[POD STATUS] default/my-app: Pending → Running
[POD EVENT] default/my-app | Ready: Unikernel application is running
```

## Resource Formats Supported

### Memory
- `128Mi` → 128 MB
- `1Gi` → 1024 MB
- `256M` → 256 MB
- `1G` → 1024 MB
- `512Ti` → 512 TB (insane but works!)

### CPU
- `1` → 1 core
- `2` → 2 cores
- `500m` → 0.5 cores (rounds up to 1)
- `0.5` → 0.5 cores (rounds up to 1)

## Unikernel Detection

Image is unikernel if:
- Filename contains: `unikernel`, `kernel`, `mirage`, `nemesis`, `nanos`, `rumprun`, `elf`
- Path contains: `/unikernels/` or `/unikernel-`
- Extension: `.elf` or `.img`

Examples that work:
- `/tmp/sirah-unikernels/test-kernel` ✓
- `/opt/unikernels/mirage-app.img` ✓
- `/home/user/nemesis-app` ✓

## Troubleshooting

### QEMU not spawning?

```bash
# 1. Check KVM available
ls -l /dev/kvm

# 2. Check QEMU installed
which qemu-system-x86_64

# 3. Check controller logs
tail -f /tmp/controller.log | grep -E "\[QEMU\]|\[POD"

# 4. Check API has pods
curl http://localhost:6443/api/v1/pods

# 5. Check image exists
ls -la /tmp/sirah-unikernels/test-kernel
```

### Nested virtualization not working?

```bash
# 1. Check config
cat ~/.wslconfig
# Should have: nestedVirtualization=true

# 2. Restart WSL2
wsl --shutdown
# Wait 10 seconds, then restart

# 3. Verify KVM
ls -l /dev/kvm
# Should show: crw-rw---- ... /dev/kvm
```

### QEMU process crashes immediately?

```bash
# 1. Check log
cat /tmp/qemu-pod-name.log

# 2. Check resources
free -h  # Memory available
nproc    # CPU cores

# 3. Check image
file /tmp/sirah-unikernels/test-kernel
```

## Architecture

```
User Pod Creation
     ↓
API Server (REST)
     ↓
Poll (5s) ← Pod Controller → fork() + execvp()
     ↓
  QEMU Process (KVM accelerated)
```

## Performance Expectations

| Metric | Value |
|--------|-------|
| Startup time | 1-2 seconds |
| Memory per VM | 128MB - 1GB |
| CPU overhead | 5-10% |
| Concurrent VMs | 4+ tested |
| Poll interval | 5 seconds |

## Key Files

| File | Purpose |
|------|---------|
| `internal/runtime/qemu.c` | QEMU spawning (fork/exec) |
| `internal/controller/pod_controller.c` | Pod discovery & lifecycle |
| `internal/runtime/runtime.c` | Runtime abstraction |
| `tests/qemu-spawn-test.sh` | Test harness |
| `bin/sirah-apiserver` | API server binary |
| `bin/sirah-controller` | Controller binary |

## Build & Run

```bash
# Build
cd sirah
make clean && make

# Run API Server (Terminal 1)
./bin/sirah-apiserver

# Run Controller (Terminal 2)
./bin/sirah-controller

# Create pod (Terminal 3)
curl -X POST http://localhost:6443/api/v1/namespaces/default/pods ...

# Monitor (Terminal 4)
pgrep -a qemu  # See QEMU processes
tail -f /tmp/controller.log  # See controller logs
```

## What's Implemented

✅ Pod discovery from API
✅ Resource extraction (memory, CPU)
✅ Image validation (unikernel detection)
✅ QEMU spawning (fork/exec)
✅ Container lifecycle events
✅ Process tracking by PID
✅ Logging to /tmp/qemu-*.log
✅ KVM acceleration on WSL2
✅ Nested virtualization enabled

## What's TODO (Optional)

- [ ] API pod status updates (PATCH)
- [ ] QEMU process monitoring
- [ ] Graceful shutdown
- [ ] Health checks (liveness/readiness)
- [ ] Multi-container pods
- [ ] Pod log retrieval
- [ ] Network configuration

## Common Commands

```bash
# Start everything
make && ./bin/sirah-apiserver &
sleep 2 && ./bin/sirah-controller &

# List QEMU VMs
pgrep -a qemu

# Get pod status
curl http://localhost:6443/api/v1/namespaces/default/pods

# Create pod
curl -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H "Content-Type: application/json" \
  -d '...'

# View QEMU logs
ls -lh /tmp/qemu-*.log
cat /tmp/qemu-default-test-vm.log

# Stop QEMU
pkill -f qemu

# Stop API server
pkill -f sirah-apiserver

# Stop controller
pkill -f sirah-controller
```

## Success Indicators

✅ Controller log shows: `[POD CONTROLLER] FETCH: Found N pods`
✅ Controller log shows: `[POD EVENT]` messages
✅ `pgrep qemu` returns process IDs
✅ Log files created at `/tmp/qemu-*.log`
✅ KVM available: `ls -l /dev/kvm`

## Next Steps

1. Run test: `bash tests/qemu-spawn-test.sh`
2. Verify QEMU processes appear
3. Check logs at `/tmp/qemu-*.log`
4. Read full docs:
   - `QEMU_SPAWNING_SUCCESS.md` - Overview
   - `COMPLETE_END_TO_END_FLOW.md` - Detailed flow
   - `SESSION_SUMMARY_QEMU_INTEGRATION.md` - Changes made

---

**Status**: ✅ Working
**Test**: PASSING
**Date**: 2025-01-30
