# Quick Ref: Seeing Inside QEMU - What's Actually Running?

## The 3-Layer Reality

```
┌─────────────────────────────────────────────────────┐
│ LAYER 3: Unikernel Interior                         │
│ "What is the kernel doing?"                         │
│ → Check: Serial console, logs, CPU usage            │
│ → Tool: kubectl logs, top, curl                     │
└─────────────────────────────────────────────────────┘
              ↑ You need all 3 layers
┌─────────────────────────────────────────────────────┐
│ LAYER 2: QEMU Process                               │
│ "Is QEMU running the unikernel?"                    │
│ → Check: Process list, QEMU arguments               │
│ → Tool: ps aux, check-qemu-inside.sh                │
└─────────────────────────────────────────────────────┘
              ↑ for complete verification
┌─────────────────────────────────────────────────────┐
│ LAYER 1: Kubernetes Pod                             │
│ "Is the pod scheduled and running?"                 │
│ → Check: Pod status, node assignment                │
│ → Tool: view-qemu-pods.sh, kubectl get pods         │
└─────────────────────────────────────────────────────┘
```

---

## One-Command Checks

### "Is my unikernel running inside QEMU?"

**Best Answer**: Run all three, get complete picture

```bash
# 1. K8s level (Is pod running?)
bash scripts/view-qemu-pods.sh | grep my-pod

# Output: my-pod  ● Running  worker1  ✓ QEMU
# ↑ OK if you see this

# 2. QEMU level (Is QEMU running the kernel?)
bash scripts/check-qemu-inside.sh default my-pod

# Output should show:
# ✓ Container is READY
# ✓ RUNNING - Unikernel is executing
# ✅ UNIKERNEL IS RUNNING INSIDE QEMU
```

### "What's the unikernel doing?"

```bash
# See actual unikernel output
kubectl logs default my-pod | head -20

# Example:
# [    0.000000] Linux version 5.10.0-qemu
# [    0.000000] Command line: root=/dev/vda console=ttyS0
# [    0.040000] rcu: Hierarchical RCU implementation.
# ...
# [ Ready ] Application started successfully
# ↑ This is your unikernel doing something

# Follow in real-time
kubectl logs -f default my-pod
```

### "Is QEMU actually using CPU/memory?"

```bash
# See resource usage
ps aux | grep qemu | grep -v grep

# Example:
# root  12345  2.5  1.8  1024000  55000  ?  Sl  17:10  0:05  qemu-system-x86_64

# Look for:
# - Column 3 (%CPU): 2.5 = CPU usage
# - Column 4 (%MEM): 1.8 = Memory usage
# - If 0.0 CPU: QEMU idle (or sleeping)
# - If >50% CPU: QEMU doing work
```

---

## Understanding the Status

### From `view-qemu-pods.sh`

```
my-pod  ● Running  worker1  ✓ QEMU
              ↓           ↓       ↓
         K8s says   Assigned   Confirmed
         pod is     to node    QEMU
         running    "worker1"  scheduling
```

**What this means**:
- ✅ Kubernetes scheduled the pod
- ✅ Pod container is running
- ✅ Assigned to a node

**Doesn't tell you**: If unikernel inside QEMU is working

---

### From `check-qemu-inside.sh`

```bash
✓ Container is READY
✓ RUNNING - Unikernel is executing
```

**What this means**:
- ✅ QEMU started successfully
- ✅ Container startup completed
- ✅ Ready signal received
- ✅ Unikernel booted and initialized

---

### From `kubectl logs`

```
[    0.000000] Linux version 5.10.0-qemu
[    0.000000] Command line: root=/dev/vda console=ttyS0
[    0.000000] SLUB: HWalign=64, Order=0-3, MinObjects=0
[    0.001000] ftrace: allocating 40000 entries
[ Ready ] Application started successfully
```

**What this means**:
- ✅ Kernel is actually booting
- ✅ Initialization steps succeeding
- ✅ Application reached ready state
- ✅ Unikernel is **actively running**

---

## Troubleshooting Decision Tree

```
┌─ Is pod Running? ─────────────────────┐
│  bash scripts/view-qemu-pods.sh       │
└─────────────────────────────────────────┘
         │
    YES │                      NO
        │                      └→ Pod not scheduled
        │                         Fix: Check resources, affinity
        ▼
┌─ Is Container Ready? ────────────────┐
│  bash scripts/check-qemu-inside.sh   │
└─────────────────────────────────────────┘
         │
    YES │                      NO
        │                      └→ QEMU not started
        │                         Fix: Check pod logs, image
        ▼
┌─ Does unikernel boot? ────────────────┐
│  kubectl logs default my-pod | head   │
└─────────────────────────────────────────┘
         │
    YES │                      NO
        │                      └→ Kernel panic or boot error
        │                         Fix: Check unikernel image
        ▼
┌─ Is it responding? ──────────────────┐
│  curl http://pod-ip:8080              │
│  or check ports: netstat -tlnp        │
└─────────────────────────────────────────┘
         │
    YES │                      NO
        │                      └→ Service not running
        │                         Fix: Check startup command
        ▼
    ✅ UNIKERNEL RUNNING & RESPONDING
```

---

## Quick Status Indicators

### From Process List

```bash
$ ps aux | grep qemu

root 12345 2.5 1.8 1024000 55000 ? Sl 17:10 0:05 qemu-system-x86_64 \
    -kernel /tmp/unikernel/app -m 256 -smp 1 -serial stdio
```

**Good signs**:
- ✓ Process exists
- ✓ %CPU > 0: doing work
- ✓ Reasonable memory usage (matches `-m` arg)
- ✓ `-kernel` arg points to unikernel file

**Bad signs**:
- ✗ Process doesn't exist: QEMU crashed
- ✗ %CPU 0: idle (check if expected)
- ✗ Memory much higher than `-m`: leak

---

### From Container Status

```bash
$ kubectl get pod my-pod -o jsonpath='{.status.containerStatuses[0].state}'

{"running":{"startedAt":"2026-01-30T17:10:25Z"}}  ← Running!
{"waiting":{"reason":"ContainerCreating"}}        ← Starting
{"terminated":{"exitCode":1,"reason":"Error"}}    ← Crashed
```

**Decoding**:
- `running`: ✅ Unikernel is executing
- `waiting`: ⏳ QEMU/kernel still starting
- `terminated`: ❌ Unikernel exited (error or normal completion)

---

### From Logs

```bash
$ kubectl logs my-pod | grep -E "ERROR|panic|ready|started"

[ERROR] Failed to mount filesystem
    ↑ Unikernel has an error

Application started successfully  
    ↑ Unikernel is ready and running
```

**Quick checks**:
- ERROR/panic: ❌ Something went wrong
- ready/started: ✅ Unikernel operational
- (no messages): ? Check full logs or QEMU monitor

---

## Most Important Commands

### Daily Checks (30 seconds)

```bash
# Check 1: Pod running?
bash scripts/view-qemu-pods.sh | grep my-pod
# ✓ Shows pod status and node

# Check 2: Unikernel ready?
bash scripts/check-qemu-inside.sh default my-pod | tail -5
# ✓ Shows if container is ready and unikernel state

# Check 3: Any errors?
kubectl logs default my-pod | grep -i error
# ✓ Empty output = no errors
```

### Deep Dive (when something's wrong)

```bash
# Full pod status
kubectl describe pod default my-pod

# Full logs from startup
kubectl logs default my-pod

# Real-time monitoring
kubectl logs -f default my-pod

# Process details
ps aux | grep qemu

# Network check
netstat -tlnp | grep qemu
```

---

## Summary: What Actually Happens

```
You create pod
    ↓
Kubernetes schedules to node
    ↓
kubelet starts container
    ↓
QEMU process launches with unikernel image
    ↓
QEMU loads unikernel as kernel
    ↓
Unikernel boots (prints boot messages to serial/logs)
    ↓
Unikernel reaches "ready" state
    ↓
Service/application becomes available
    ↓
✅ Unikernel is running inside QEMU
```

### How to See Each Step

| Step | How to Check | Tool |
|------|-------------|------|
| Scheduled? | Pod has nodeNAME | `view-qemu-pods.sh` |
| Container started? | containerStatuses | `kubectl get pod -o json` |
| QEMU running? | Process exists | `ps aux` |
| Unikernel booting? | Kernel messages | `kubectl logs` |
| Ready? | Container ready: true | `check-qemu-inside.sh` |
| Working? | Service responds | `curl` / `telnet` |

---

## Real Example

```bash
# User: "Is my unikernel running?"

# Step 1: Check Kubernetes level
$ bash scripts/view-qemu-pods.sh
web-server  ● Running  worker1  ✓ QEMU
            ↑ Pod says it's running

# Step 2: Check QEMU level
$ bash scripts/check-qemu-inside.sh default web-server
✓ Container is READY
✓ RUNNING - Unikernel is executing
✅ UNIKERNEL IS RUNNING INSIDE QEMU
            ↑ QEMU says it's running

# Step 3: Check unikernel output
$ kubectl logs default web-server | head
[    0.000000] Linux version 5.10.0-qemu
[    0.001000] ftrace: allocating 40000 entries
[ Ready ] HTTP server listening on 0.0.0.0:8080
            ↑ Unikernel says it's ready

# Step 4: Test it
$ curl http://10.0.1.5:8080
{"status":"ok"}
            ↑ Unikernel is responding

# Answer: YES ✅ UNIKERNEL IS RUNNING INSIDE QEMU AND RESPONDING
```

---

## Key Insight

**A running pod ≠ a working unikernel**

Three things must be true:

1. **K8s Level**: Pod is Running ✓
2. **QEMU Level**: Container Ready is true ✓  
3. **Boot Level**: Unikernel shows "ready" in logs ✓

Miss any one, and unikernel might not actually be running.

**Check all three to be sure.**

---

## Files Created

- `scripts/check-qemu-inside.sh` - Comprehensive inside-QEMU checker
- `QEMU_INSIDE_MONITORING.md` - Full guide (this file's source)
- `scripts/view-qemu-pods.sh` - Kubernetes-level pod status
- `scripts/inspect-qemu-pod.sh` - Detailed pod inspection
- `scripts/qemu-scheduling-dashboard.sh` - Live monitoring dashboard

---

## Next: Try It

```bash
# 1. Create a test pod
kubectl run test-qemu --image=alpine:latest --restart=Never -- sleep infinity

# 2. Wait for it to schedule
bash scripts/view-qemu-pods.sh

# 3. Check inside
bash scripts/check-qemu-inside.sh default test-qemu

# 4. See what it's doing
kubectl logs default test-qemu
```

That's it. Now you can see inside QEMU. ✅
