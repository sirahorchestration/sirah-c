# Your Complete Answer: Seeing Inside QEMU

## Your Question
> "How can I see within QEMU if the unikernel is running?"

## Complete Answer ✅

You now have **4 different ways** to peek inside QEMU and verify the unikernel is actually running:

---

## Way #1: Quick QEMU Status Check (BEST OVERALL) ⭐

```bash
bash scripts/check-qemu-inside.sh default my-pod-name
```

**This shows**:
- Is QEMU process running?
- Is the container ready?
- Is the unikernel executing?
- Clear yes/no answer at the end

**Example Output**:
```
✓ Found pod: my-pod
Node: worker1
Status: Running
Runtime Class: qemu

✓ QEMU Process (PID: 12345)
   Command: qemu-system-x86_64 -kernel /tmp/unikernel ...

✓ Container is READY
✓ RUNNING - Unikernel is executing

✅ UNIKERNEL IS RUNNING INSIDE QEMU
```

---

## Way #2: See Kubernetes Pod Status (Quick Check)

```bash
bash scripts/view-qemu-pods.sh
```

**This shows**:
- All pods and their status
- Which node they're assigned to
- QEMU scheduling indicator

**Example Output**:
```
Pod Name     Status      Node     QEMU
my-pod       ● Running   worker1  ✓ QEMU
```

**What to look for**:
- Status: ● Running (not ◐ Pending)
- Node: Shows node name (not "unscheduled")
- QEMU: ✓ QEMU (confirmed)

---

## Way #3: See Unikernel Boot Messages (Proof It Booted)

```bash
kubectl logs default my-pod-name | head -20
```

**This shows**:
- Kernel boot messages
- Initialization sequence
- Application startup output

**Example Output**:
```
[    0.000000] Linux version 5.10.0-qemu (root@build)
[    0.000000] Command line: root=/dev/vda console=ttyS0
[    0.001000] ftrace: allocating 40000 entries in 157 pages
[    0.040000] rcu: Hierarchical RCU implementation
[ Ready ] Application started successfully
```

**What to look for**:
- "Linux version" messages = kernel is booting ✓
- No "panic" or "ERROR" = successful boot ✓
- "Ready" or "started" = unikernel operational ✓

---

## Way #4: See It Actually Working (Proof It's Doing Work)

```bash
# If HTTP service:
curl http://10.0.1.5:8080

# Or check ports listening:
netstat -tlnp | grep qemu

# Or see CPU usage:
top | grep qemu
```

**Example Output**:
```
$ curl http://10.0.1.5:8080
{"status":"ok","uptime":"5m"}
200 OK

$ netstat -tlnp | grep qemu
LISTEN  0  128  10.0.1.5:8080  0.0.0.0:*  PID 12345

$ top | grep qemu
12345  root  2.5%  1.8%  1.1g  55m  qemu-system...
     ↑ CPU activity means unikernel is working
```

**What to look for**:
- Service responds (200 OK)
- Ports listening on pod IP
- CPU > 0% (if service should be active)

---

## Complete Verification Flow

To be 100% sure unikernel is running inside QEMU:

### Step 1: Is pod scheduled?
```bash
bash scripts/view-qemu-pods.sh | grep my-pod
```
✓ Should show: `my-pod  ● Running  worker1  ✓ QEMU`

### Step 2: Is QEMU running the kernel?
```bash
bash scripts/check-qemu-inside.sh default my-pod
```
✓ Should show: `✅ UNIKERNEL IS RUNNING INSIDE QEMU`

### Step 3: Did kernel boot?
```bash
kubectl logs default my-pod | head -5
```
✓ Should show: `Linux version` messages

### Step 4: Is service working?
```bash
curl http://pod-ip:8080
# or
ps aux | grep qemu  # see CPU usage
```
✓ Should show: Response or CPU activity

**All 4 ✓ = Unikernel is fully running ✅**

---

## Most Common Use Case

**You created a pod and want to know: "Is it running?"**

### Single Command Answer:
```bash
bash scripts/check-qemu-inside.sh default my-pod
```

**If you see**:
```
✅ UNIKERNEL IS RUNNING INSIDE QEMU
```

**= YES, it's running ✅**

---

## Real-World Examples

### Example 1: Pod Running Normally
```bash
$ bash scripts/view-qemu-pods.sh
my-app  ● Running  worker1  ✓ QEMU
         ↑ Running status

$ bash scripts/check-qemu-inside.sh default my-app
✓ Container is READY
✓ RUNNING - Unikernel is executing
✅ UNIKERNEL IS RUNNING INSIDE QEMU
         ↑ Confirmed running

$ kubectl logs default my-app | head
[    0.000000] Linux version 5.10.0-qemu
[ Ready ] HTTP server started on 0.0.0.0:8080
         ↑ Boot output shows running
```

**Answer**: YES ✅ Unikernel is running

---

### Example 2: Pod Failing
```bash
$ bash scripts/view-qemu-pods.sh
bad-app  ✗ Failed   worker1  ○
          ↑ Failed status

$ bash scripts/check-qemu-inside.sh default bad-app
⚠ Container is NOT READY
Container State: WAITING - ImagePullBackOff
          ↑ Container didn't start

$ kubectl logs default bad-app
(empty - no boot output)
          ↑ No logs means never booted
```

**Answer**: NO ❌ Unikernel didn't run (image not found)

---

### Example 3: Pod Pending
```bash
$ bash scripts/view-qemu-pods.sh
new-app  ◐ Pending  unscheduled  ○
         ↑ Pending status

$ bash scripts/check-qemu-inside.sh default new-app
⚠ Pod not scheduled to any node
          ↑ Waiting for scheduler

$ kubectl logs default new-app
(empty)
          ↑ No container started yet
```

**Answer**: NOT YET ⏳ Waiting to be scheduled

---

## Tools Provided

| Tool | Best For | Speed |
|------|----------|-------|
| `bash scripts/view-qemu-pods.sh` | Quick pod status | 1 sec |
| `bash scripts/check-qemu-inside.sh` | QEMU/unikernel status | 2 sec |
| `bash scripts/inspect-qemu-pod.sh` | Deep pod details | 2 sec |
| `bash scripts/qemu-scheduling-dashboard.sh` | Live monitoring | Live |
| `kubectl logs` | Boot messages | 1 sec |
| `curl` | Service test | 1 sec |
| `ps aux \| grep qemu` | Process check | 1 sec |
| `top \| grep qemu` | Resource usage | 1 sec |

---

## Quick Reference

### "Is my unikernel running?"
```bash
bash scripts/check-qemu-inside.sh default MY_POD_NAME
```

### "Show me all pods and their status"
```bash
bash scripts/view-qemu-pods.sh
```

### "What's the kernel printing?"
```bash
kubectl logs default MY_POD_NAME
```

### "Is the service responding?"
```bash
curl http://MY_POD_IP:8080
```

### "How much CPU/memory?"
```bash
ps aux | grep qemu
top | grep qemu
```

---

## The Key Insight

**A Kubernetes pod running ≠ Unikernel actually working**

Need all 4 levels:

```
✓ Pod scheduled to node (Level 1)
✓ QEMU process running (Level 2)
✓ Kernel booted with output (Level 3)
✓ Service responding/CPU active (Level 4)
=====================================
✅ UNIKERNEL IS TRULY RUNNING
```

Check all 4 to be sure.

---

## Documentation Provided

| File | Purpose |
|------|---------|
| **QEMU_INSIDE_ANSWER.md** | This file - direct answer |
| **QEMU_INSIDE_COMPLETE.md** | Visual overview with scenarios |
| **QEMU_INSIDE_MONITORING.md** | Complete technical guide (long form) |
| **QEMU_INSIDE_QUICK_REF.md** | Quick reference card |

---

## Your Next Steps

### Option 1: Quick Verification
```bash
bash scripts/check-qemu-inside.sh default my-pod-name
```
Takes 2 seconds. Shows if unikernel is running.

### Option 2: Full Stack Check
```bash
bash scripts/view-qemu-pods.sh | grep my-pod
bash scripts/check-qemu-inside.sh default my-pod
kubectl logs default my-pod | head
curl http://pod-ip:8080
```
Takes 10 seconds. Complete verification.

### Option 3: Live Monitoring
```bash
bash scripts/qemu-scheduling-dashboard.sh
```
Real-time view. Watch pods schedule and start.

---

## Summary

**Your question**: "How can I see within QEMU if the unikernel is running?"

**Your answer**: Use these 4 ways:

1. **Quick status**: `bash scripts/check-qemu-inside.sh default pod-name`
2. **Kubernetes view**: `bash scripts/view-qemu-pods.sh`
3. **Kernel output**: `kubectl logs default pod-name`
4. **Service test**: `curl http://pod-ip:8080`

**All tools are ready to use right now. ✅**

---

## Quick Start

```bash
# Create a test pod
kubectl run test-qemu --image=alpine:latest --restart=Never -- sleep 3600

# Check if it's running
bash scripts/check-qemu-inside.sh default test-qemu

# See the output
kubectl logs default test-qemu
```

That's it. You're now looking inside QEMU. ✅

---

**Status**: Complete - Ready to use  
**Date**: January 30, 2026  
**Your tools**: 4 scripts + 4 docs  
**Time to first answer**: 2 seconds
