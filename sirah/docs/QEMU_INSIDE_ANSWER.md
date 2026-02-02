# Inside QEMU Monitoring - Your Answer

## Your Question
> "But how can I see within QEMU if the unikernel is running?"

## Complete Answer: 4 Levels of Visibility

---

## Level 1: Kubernetes View (Easiest)
**Question**: "Is the pod scheduled and running?"

```bash
bash scripts/view-qemu-pods.sh
```

**Example Output**:
```
Pod Name     Status    Node      QEMU
my-pod       ● Run     worker1   ✓ QEMU
```

**✓ If you see**: Pod name, node assigned, ✓ QEMU  
**✗ If missing**: Pod not scheduled yet

---

## Level 2: QEMU Process View (Best Overall)
**Question**: "Is QEMU actually running the unikernel?"

```bash
bash scripts/check-qemu-inside.sh default my-pod
```

**Example Output**:
```
✓ QEMU Process (PID: 12345)
   Command: qemu-system-x86_64 -kernel /tmp/unikernel/my-app ...

✓ Container is READY
✓ RUNNING - Unikernel is executing

✅ UNIKERNEL IS RUNNING INSIDE QEMU
```

**✓ If you see**: Container READY = true, RUNNING state  
**✗ If you see**: Container NOT READY = unikernel didn't start

---

## Level 3: Kernel Boot View (Technical)
**Question**: "Did the unikernel actually boot?"

```bash
kubectl logs default my-pod | head -20
```

**Example Output**:
```
[    0.000000] Linux version 5.10.0-qemu (root@build)
[    0.000000] Command line: root=/dev/vda console=ttyS0
[    0.000000] SLUB: HWalign=64, Order=0-3, MinObjects=0, CPUs=1
[    0.001000] ftrace: allocating 40000 entries in 157 pages
[    0.040000] rcu: Hierarchical RCU implementation
[ Ready ] Application started successfully
```

**✓ If you see**: "Linux version" = kernel booting, "Ready" = ready  
**✗ If you see**: "panic" = kernel error, nothing = no console output

---

## Level 4: Work Verification (Proof It Works)
**Question**: "Is the unikernel actually doing something?"

```bash
# If HTTP service:
curl http://pod-ip:8080

# Check if listening on ports:
netstat -tlnp | grep qemu

# Check CPU/memory usage:
top | grep qemu
```

**Example Output**:
```
$ curl http://10.0.1.5:8080
{"status": "ok", "uptime": "2m"}
200 OK

$ netstat -tlnp | grep qemu
LISTEN  0  128  10.0.1.5:8080  0.0.0.0:*  PID 12345

$ top | grep qemu
12345  root  2.5%  1.8%  1.1g  55m  qemu-system...
```

**✓ If you see**: Service responds, port listening, CPU active  
**✗ If you see**: No response, no ports, 0% CPU (if should be working)

---

## The Decision Tree

```
START
  ↓
bash scripts/view-qemu-pods.sh
  ├─ Pod NOT found? → Pod not scheduled, fix scheduling
  ├─ Pod Pending? → Wait, scheduler is working
  ├─ Pod Running? → Continue ↓
  │
  ↓
bash scripts/check-qemu-inside.sh default my-pod
  ├─ Container NOT READY? → QEMU didn't start, check logs
  ├─ Container READY? → Continue ↓
  │
  ↓
kubectl logs default my-pod | head
  ├─ No output? → No serial console, check startup
  ├─ "panic"? → Kernel error, fix unikernel image
  ├─ "Ready"? → Continue ↓
  │
  ↓
curl http://pod-ip:8080  (or your service)
  ├─ No response? → Service not running, check app config
  ├─ 200 OK? → Continue ↓
  │
  ↓
✅ UNIKERNEL IS FULLY RUNNING AND WORKING
```

---

## What Each Layer Shows

### Layer 1: Pod Running
- ✅ Kubernetes scheduled the pod
- ✅ Pod container is active
- ❌ Doesn't tell you if unikernel inside is working

### Layer 2: Container Ready
- ✅ QEMU process started
- ✅ Container reached ready state
- ✅ Unikernel signals it's ready
- ❌ Doesn't tell you if service is actually working

### Layer 3: Boot Messages
- ✅ Kernel loaded and initialized
- ✅ Kernel services started
- ✅ Application startup code ran
- ❌ Doesn't tell you if service is responding

### Layer 4: Working Service
- ✅ Service is listening on ports
- ✅ Service is responding to requests
- ✅ Unikernel is **actively doing work**
- ✅ **Proof it's all working**

---

## Real-World Scenarios

### Scenario 1: You See This
```
✓ Pod Running
✓ Container Ready  
✓ Boot messages
✓ Service responds (200 OK)
```
→ **✅ UNIKERNEL IS RUNNING AND WORKING**

---

### Scenario 2: You See This
```
✓ Pod Running
✓ Container Ready
✓ Boot messages
✗ Service doesn't respond
```
→ **⚠️ Unikernel booted but service has issue**
- Port might not be listening
- Application crashed after boot
- Network not configured
- Firewall blocking access

**Fix**: Check service logs, verify port binding, test from inside pod

---

### Scenario 3: You See This
```
✓ Pod Running
✓ Container Ready
✗ No boot messages
```
→ **❌ Unikernel not booting**
- Serial console not configured
- Kernel panic on boot
- QEMU not capturing output

**Fix**: Check QEMU args, verify unikernel image, check kernel parameters

---

### Scenario 4: You See This
```
✓ Pod Running
✗ Container NOT ready
```
→ **❌ QEMU didn't start the unikernel**
- Container image not found
- QEMU not installed
- Runtime class misconfigured

**Fix**: Check pod events, verify image exists, check runtime

---

## Quick Status Checks (Copy-Paste Ready)

### "Is my pod scheduled?"
```bash
bash scripts/view-qemu-pods.sh | grep MY_POD_NAME
```

### "Is QEMU running?"
```bash
bash scripts/check-qemu-inside.sh default MY_POD_NAME | tail -5
```

### "Is it booting?"
```bash
kubectl logs default MY_POD_NAME | head -5
```

### "Is it working?"
```bash
curl http://MY_POD_IP:8080
```

---

## Tools You Have Now

| Tool | Command | Shows | Time |
|------|---------|-------|------|
| **Pod Status** | `view-qemu-pods.sh` | All pods, node assignment, QEMU status | 1s |
| **QEMU Inside** | `check-qemu-inside.sh default pod` | QEMU process, container ready, unikernel state | 2s |
| **Pod Details** | `inspect-qemu-pod.sh default pod` | Full pod metadata and scheduling info | 2s |
| **Live Dashboard** | `qemu-scheduling-dashboard.sh` | Real-time live view with auto-refresh | Live |
| **Kernel Messages** | `kubectl logs default pod` | Unikernel boot output and application logs | 1s |

---

## The Best Command

If you want to **fully verify** your unikernel is running inside QEMU:

```bash
# In sequence:
bash scripts/view-qemu-pods.sh | grep my-pod && \
bash scripts/check-qemu-inside.sh default my-pod | tail -3 && \
kubectl logs default my-pod | head -3 && \
curl http://$(kubectl get pod my-pod -o jsonpath='{.status.podIP}'):8080
```

**If all succeed without errors = unikernel is running ✅**

---

## Key Insight

A **pod running** in Kubernetes ≠ **unikernel working** in QEMU

Need to verify all 4 levels:

```
Level 1: Pod scheduled
Level 2: QEMU process running
Level 3: Unikernel booted
Level 4: Service responding
         ==================
         UNIKERNEL RUNNING ✅
```

---

## Most Important Command

For a quick, complete check:

```bash
bash scripts/check-qemu-inside.sh default MY_POD_NAME
```

**This shows**:
- ✓ Is pod scheduled?
- ✓ Is QEMU running?
- ✓ Is container ready?
- ✓ Is unikernel executing?

**All in one output.**

---

## Start Right Now

```bash
# 1. Create a test pod
kubectl run test-qemu --image=alpine:latest --restart=Never -- sleep 3600

# 2. Check everything
bash scripts/view-qemu-pods.sh
bash scripts/check-qemu-inside.sh default test-qemu
kubectl logs default test-qemu

# 3. See it working
kubectl get pod test-qemu
```

**That's it. You're now looking inside QEMU.**

---

## Summary

**Your question**: "How can I see within QEMU if the unikernel is running?"

**Your answer**: 
1. Use `check-qemu-inside.sh` to see QEMU status
2. Use `kubectl logs` to see kernel output
3. Use `curl` to see if it's working
4. Use `top/netstat` to see if it's active

**All these tools now available. Ready to use. ✅**

---

## Documentation

- **QEMU_INSIDE_COMPLETE.md** ← Complete overview (start here)
- **QEMU_INSIDE_MONITORING.md** ← Full technical guide
- **QEMU_INSIDE_QUICK_REF.md** ← Quick reference
- **QEMU_VISUAL_SOLUTION.md** ← Pod-level monitoring

**Pick the one that matches your learning style.**

---

**Status**: ✅ Complete QEMU inside monitoring solution ready  
**Your tools are ready**: 4 scripts + 7+ docs  
**Next step**: Run `bash scripts/check-qemu-inside.sh default pod-name`
