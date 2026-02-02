# How to See Inside QEMU - Unikernel Execution Monitoring

**Question**: "How can I see within QEMU if the unikernel is running?"

**Answer**: Multiple ways to peek inside the QEMU VM and verify unikernel execution.

---

## Quick Answer: What's Really Running?

### Three Layers to Check

1. **Kubernetes Layer** (Easy) ✅ 
   - Is the pod scheduled?
   - Is the pod status "Running"?
   - **Tool**: `view-qemu-pods.sh`

2. **QEMU Process Layer** (Medium) ✓
   - Is the QEMU process running?
   - What arguments is QEMU using?
   - **Tool**: `check-qemu-inside.sh`

3. **Unikernel Layer** (Hard)
   - What's executing inside QEMU?
   - What's the unikernel doing?
   - **Tool**: Serial console, QEMU monitor, or logs

---

## Level 1: Quick Check (Just Below Kubernetes)

```bash
bash scripts/check-qemu-inside.sh default my-pod
```

**Shows**:
- Pod status (Kubernetes view)
- QEMU process running (operating system view)
- Container ready status
- Unikernel startup state

**Example Output**:
```
✓ Found pod: my-pod
Pod Name:         my-pod
Node:             worker1
Status:           Running
Runtime Class:    qemu

✓ QEMU Process (PID: 12345)
   Command: qemu-system-x86_64 -kernel /path/to/unikernel -m 256 -smp 1

✓ Container is READY
✓ RUNNING - Unikernel is executing

✅ UNIKERNEL IS RUNNING INSIDE QEMU
```

**What This Tells You**:
- If QEMU process exists → QEMU was started
- If "READY" → unikernel booted successfully
- If "RUNNING" → unikernel is executing
- If no restarts → no errors or crashes

---

## Level 2: QEMU Process Details

### See What QEMU Is Doing

```bash
# List all QEMU processes
ps aux | grep qemu

# See full QEMU command line
ps aux | grep qemu | grep -v grep
```

**Example Output**:
```bash
$ ps aux | grep qemu
root  12345  0.5  2.1  1024000 55000 ?  Sl  17:10  0:05 \
  qemu-system-x86_64 \
    -kernel /tmp/unikernel/my-app \
    -m 256 \
    -smp 1 \
    -serial stdio \
    -device virtio-net-pci,netdev=net0 \
    -netdev user,id=net0 \
    -enable-kvm \
    -nographic
```

**What This Shows**:

| Argument | Meaning | Indicates |
|----------|---------|-----------|
| `-kernel /path` | Which unikernel | ✅ Unikernel loaded |
| `-m 256` | Memory: 256 MB | ✅ Resources allocated |
| `-smp 1` | CPUs: 1 | ✅ CPU allocated |
| `-serial stdio` | Console output to logs | ✅ Capture enabled |
| `-enable-kvm` | Hardware virtualization | ✅ Performance enabled |
| `-nographic` | No GUI | ✅ Server mode |

**Interpretation**:
- **All these args present** = QEMU properly configured
- **Process running** = QEMU is active
- **No process** = QEMU crashed or never started

---

## Level 3: Unikernel Serial Output

### See What Unikernel is Printing

The unikernel's serial console output shows kernel boot messages.

### Option A: From Pod Logs

```bash
kubectl logs default my-pod
```

**Example Output**:
```
[    0.000000] Linux version 5.10.0-qemu (root@build) ...
[    0.000000] Command line: root=/dev/vda console=ttyS0
[    0.000000] Kernel command line: root=/dev/vda console=ttyS0
[    0.000000] Dentry cache hash table entries: 32768 ...
[    0.000000] SLUB: HWalign=64, Order=0-3, MinObjects=0, CPUs=1, Nodes=1
[    0.001000] ftrace: allocating 40000 entries in 157 pages
[    0.040000] rcu: Hierarchical RCU implementation.
...
[ Ready ] Application started successfully
```

**What This Shows**:
- ✅ Kernel boot messages = Unikernel booting
- ✅ No errors = Successful startup
- ✅ "Ready" messages = Unikernel operational

### Option B: From QEMU Monitor

If QEMU has monitor enabled:

```bash
# Connect to QEMU monitor (if configured)
telnet localhost 5555  # port depends on QEMU setup

# Then use monitor commands
info registers       # CPU state
info cpus           # CPU info
info processes      # Running processes
```

### Option C: Real-time Monitoring

```bash
# Follow pod logs in real-time
kubectl logs -f default my-pod

# Will show:
# - Kernel boot messages as they appear
# - Application output in real-time
# - Errors as they occur
```

---

## Level 4: Check If Unikernel is Actually Doing Work

### Is It Just Running, or Actually Processing?

```bash
# Check CPU usage
top | grep qemu

# Example output:
# PID   USER  PR  NI  VIRT   RES  SHR S %CPU %MEM
# 12345 root  20   0  1.1g  55m   4m S  2.1  1.4  qemu-system-x86_64
```

**Interpretation**:
- **%CPU > 0** = Unikernel is using CPU (doing work)
- **%CPU ≈ 0** = Idle or sleeping
- **%CPU 100%** = CPU-bound workload or infinite loop

### Check Network Activity

```bash
# If unikernel runs a network service
netstat -tlnp | grep qemu

# Or check connections to QEMU's virtual network
ss -tlnp | grep -E ':(8080|3000|5000)'
```

**Example**:
```
LISTEN  0  128  127.0.0.1:8080  0.0.0.0:*  12345/qemu-system...
```

**Interpretation**:
- ✅ Port listening = Service accepting connections
- ✅ Active connections = Service handling traffic
- ❌ No ports = Service not exposed or not running

---

## The Complete Verification Flow

### Check 1: Is Kubernetes Happy?

```bash
bash scripts/view-qemu-pods.sh
```

**Look for**:
- Pod status: "● Running"
- Node: Shows node name (not "unscheduled")
- QEMU: Shows "✓ QEMU"

If YES → go to Check 2  
If NO → pod not scheduled, fix scheduling first

---

### Check 2: Is QEMU Running?

```bash
bash scripts/check-qemu-inside.sh default my-pod
```

**Look for**:
- Container Ready: true
- QEMU Process: Present in output
- Container State: "RUNNING"

If YES → go to Check 3  
If NO → QEMU didn't start, check container logs

---

### Check 3: Is Unikernel Boot Output Visible?

```bash
kubectl logs default my-pod | head -20
```

**Look for**:
- Kernel boot messages (Linux version, etc.)
- No "panic" or "error" messages
- "Ready" or "started" message at end

If YES → unikernel booted successfully, go to Check 4  
If NO → unikernel failed to boot, check full logs

---

### Check 4: Is Unikernel Doing Work?

```bash
# If HTTP service:
curl http://pod-ip:8080

# If network service:
telnet pod-ip 3000

# Check processes in unikernel (if accessible)
ps aux  # from inside unikernel terminal

# Or check CPU usage:
top | grep qemu
```

**Look for**:
- Service responds to requests
- No errors in responses
- CPU usage shows activity (or idle if expected)

---

## Summary: What Tells You Unikernel is Running?

| Check | Command | Success Sign | Running Status |
|-------|---------|--------------|-----------------|
| **K8s Level** | `view-qemu-pods.sh` | Pod = Running | Scheduled ✓ |
| **QEMU Level** | `check-qemu-inside.sh` | Container Ready = true | Started ✓ |
| **Boot Level** | `kubectl logs` | Kernel boot messages | Booted ✓ |
| **Work Level** | `curl` or `top` | Service responds, CPU active | Running ✓ |

### Full Success = All Four ✓

```
✓ Pod scheduled
✓ QEMU process running
✓ Unikernel booted
✓ Unikernel doing work
= UNIKERNEL IS SUCCESSFULLY RUNNING IN QEMU ✅
```

---

## Troubleshooting Guide

### Symptom: Pod Running but QEMU Not Starting

```bash
# Check
bash scripts/check-qemu-inside.sh default my-pod

# You see:
# ⚠ No QEMU processes found

# Fix:
# 1. Check pod logs for errors
kubectl logs default my-pod

# 2. Check pod events
kubectl describe pod default my-pod

# 3. Check node resources
kubectl describe node worker1
```

---

### Symptom: QEMU Running but Unikernel Not Booting

```bash
# Check
kubectl logs default my-pod | head -30

# You see:
# (no kernel boot messages, or panic/error)

# Fix:
# 1. Check unikernel image
# 2. Check unikernel command line args
# 3. Check available memory/CPU

# Debug:
ps aux | grep qemu  # Check actual QEMU arguments
```

---

### Symptom: Unikernel Running but Not Responding

```bash
# Check
curl http://pod-ip:8080  # No response

# Debug steps:
# 1. Check if port is listening
netstat -tlnp | grep 8080

# 2. Check QEMU CPU usage
top | grep qemu

# 3. Check network config
kubectl describe pod default my-pod | grep -A 10 "Network"

# 4. Check logs
kubectl logs -f default my-pod
```

---

## Key Concepts

### Pod Status vs Unikernel Status

**Pod Status** = Kubernetes' view
```
Running = K8s says container is running
```

**Unikernel Status** = Actual execution
```
- QEMU process running = VM is active
- Booted = Kernel loaded and initialized
- Responding = Actually doing work
```

**Both must be true for success**:
```
✓ Pod Running (K8s) AND
✓ QEMU process exists AND
✓ Unikernel booted
= Unikernel is executing ✅
```

---

## Most Important Checks (Quick Reference)

### 1-Second Check
```bash
bash scripts/view-qemu-pods.sh | grep my-pod
```
→ Is pod running?

### 5-Second Check
```bash
bash scripts/check-qemu-inside.sh default my-pod
```
→ Is unikernel ready?

### 10-Second Check
```bash
kubectl logs default my-pod | head
```
→ Is unikernel booting?

### Full Check
```bash
# All three
bash scripts/view-qemu-pods.sh
bash scripts/check-qemu-inside.sh default my-pod
kubectl logs default my-pod
```
→ Complete verification

---

## Commands at a Glance

```bash
# Kubernetes level
kubectl get pods                        # All pods
kubectl get pod my-pod -o wide         # Pod details
kubectl describe pod my-pod             # Full pod info

# QEMU level
ps aux | grep qemu                      # QEMU processes
bash scripts/check-qemu-inside.sh default my-pod  # Unikernel status

# Unikernel level
kubectl logs my-pod                     # Boot/startup messages
kubectl logs -f my-pod                  # Real-time logs
kubectl logs --tail=50 my-pod          # Last 50 lines

# Network level
curl http://pod-ip:8080                # Test service
telnet pod-ip 3000                      # Test port
netstat -tlnp | grep qemu              # Open ports

# Performance level
top | grep qemu                         # CPU/memory usage
ps -eaf | grep qemu                    # Detailed process info
```

---

## Next Steps

1. **Verify pod is scheduled**: `bash scripts/view-qemu-pods.sh`
2. **Check QEMU is running**: `bash scripts/check-qemu-inside.sh default pod-name`
3. **See unikernel boot output**: `kubectl logs default pod-name`
4. **Test the service**: `curl` or connect to service port
5. **Monitor performance**: `top` or `kubectl top`

---

**Remember**: A running pod doesn't always mean a working unikernel. Use these multiple levels of checking to verify the entire stack is functional.
