# See Inside QEMU - Complete Monitoring Stack

## Your Question Answered

**"How can I see within QEMU if the unikernel is running?"**

**Answer**: Use the 4 tools below to see at all levels.

---

## The 4-Level Monitoring Stack

```
┌──────────────────────────────────────────────────────────────┐
│ LEVEL 4: Unikernel Work - Is it actually doing something?    │
│                                                               │
│ Tools: curl, telnet, top, netstat                            │
│ Shows: Service response, ports listening, CPU/memory usage   │
│ Goal:  Verify unikernel is responsive and working            │
│                                                               │
│ Example:                                                      │
│   $ curl http://pod-ip:8080                                  │
│   {"status":"ok"}  ← Unikernel responding!                   │
│                                                               │
│   $ top | grep qemu                                          │
│   12345  root  2.5%  1.8%  qemu-system-x86_64                │
│   ↑ CPU usage means doing work                               │
└──────────────────────────────────────────────────────────────┘
         ↑
         │ If working, unikernel is definitely running
         │
┌──────────────────────────────────────────────────────────────┐
│ LEVEL 3: Unikernel Boot - Did kernel initialize successfully?│
│                                                               │
│ Tools: kubectl logs                                          │
│ Shows: Kernel boot messages, startup sequence                │
│ Goal:  Verify unikernel successfully booted                  │
│                                                               │
│ Example:                                                      │
│   $ kubectl logs default my-pod | head                       │
│   [    0.000000] Linux version 5.10.0-qemu                   │
│   [    0.001000] ftrace: allocating...                       │
│   [ Ready ] Application started successfully                 │
│   ↑ Boot messages prove unikernel is running                 │
└──────────────────────────────────────────────────────────────┘
         ↑
         │ If booted, container is properly set up
         │
┌──────────────────────────────────────────────────────────────┐
│ LEVEL 2: QEMU Process - Is QEMU running the kernel?          │
│                                                               │
│ Tool: bash scripts/check-qemu-inside.sh [ns] [pod]          │
│ Shows: QEMU process, container readiness, kernel state       │
│ Goal:  Verify QEMU is executing unikernel successfully       │
│                                                               │
│ Example:                                                      │
│   $ bash scripts/check-qemu-inside.sh default my-pod         │
│   ✓ QEMU Process (PID: 12345)                                │
│   ✓ Container is READY                                       │
│   ✓ RUNNING - Unikernel is executing                         │
│   ✅ UNIKERNEL IS RUNNING INSIDE QEMU                        │
│   ↑ This is your main indicator                              │
└──────────────────────────────────────────────────────────────┘
         ↑
         │ If QEMU ready, pod is properly scheduled
         │
┌──────────────────────────────────────────────────────────────┐
│ LEVEL 1: Kubernetes Pod - Is pod scheduled and running?      │
│                                                               │
│ Tool: bash scripts/view-qemu-pods.sh [namespace]             │
│ Shows: Pod status, node assignment, QEMU indicator           │
│ Goal:  Verify pod is scheduled to QEMU node                  │
│                                                               │
│ Example:                                                      │
│   $ bash scripts/view-qemu-pods.sh                           │
│   my-pod  ● Running  worker1  ✓ QEMU                         │
│   ↑ Running status is the starting point                     │
└──────────────────────────────────────────────────────────────┘
```

---

## Quick Check: 30 Seconds

```bash
# All in one command chain:
bash scripts/view-qemu-pods.sh | grep my-pod && \
bash scripts/check-qemu-inside.sh default my-pod | grep -E "READY|RUNNING|UNIKERNEL" && \
kubectl logs default my-pod | head -3
```

**If you see**:
```
my-pod  ● Running  worker1  ✓ QEMU
✓ Container is READY
✓ RUNNING - Unikernel is executing
[    0.000000] Linux version 5.10.0-qemu
```

**= Unikernel is running ✅**

---

## Which Tool For What?

### 1️⃣ "Is my pod scheduled to QEMU?"

```bash
bash scripts/view-qemu-pods.sh
```

**Output**: Table of all pods with status and node  
**What to look for**: Node name (not "unscheduled"), ✓ QEMU indicator  
**Time**: 1 second

---

### 2️⃣ "Is the unikernel running inside QEMU?"

```bash
bash scripts/check-qemu-inside.sh default my-pod
```

**Output**: QEMU process details, container readiness, unikernel state  
**What to look for**: ✓ Container is READY, ✓ RUNNING - Unikernel is executing  
**Time**: 2 seconds

---

### 3️⃣ "What is the unikernel doing?"

```bash
kubectl logs default my-pod
```

**Output**: Kernel boot messages and application output  
**What to look for**: Linux version messages, no "panic" errors, "Ready" at end  
**Time**: 1 second

---

### 4️⃣ "Is the unikernel responsive/working?"

```bash
# HTTP service?
curl http://pod-ip:8080

# Any network service?
netstat -tlnp | grep qemu

# CPU usage?
top | grep qemu
```

**Output**: Service response, open ports, resource usage  
**What to look for**: 200 OK response, ports listening, CPU > 0 if doing work  
**Time**: 1 second

---

## Real-World Example: Debugging a Stuck Pod

### Symptom: Pod shows Running, but something's wrong

```bash
# Step 1: Is pod scheduled?
$ bash scripts/view-qemu-pods.sh
my-app  ● Running  worker1  ✓ QEMU
✓ Yes, pod is running

# Step 2: Is QEMU/container ready?
$ bash scripts/check-qemu-inside.sh default my-app
⚠ Container is NOT READY
Container State: WAITING - ImagePullBackOff: image not found
✗ QEMU didn't start - image problem

# Step 3: Check the error
$ kubectl describe pod default my-app
Events:
  Failed to pull image "wrong-image:tag"
  Error: image not found

# Solution: Fix the image name, recreate pod
```

---

### Symptom: Pod running, QEMU says ready, but service doesn't respond

```bash
# Step 1: Is pod scheduled?
$ bash scripts/view-qemu-pods.sh
web-app  ● Running  worker1  ✓ QEMU
✓ Yes

# Step 2: Is unikernel running?
$ bash scripts/check-qemu-inside.sh default web-app
✓ Container is READY
✓ RUNNING - Unikernel is executing
✓ Yes, it's running

# Step 3: Check boot output
$ kubectl logs default web-app | head
[    0.000000] Linux version 5.10.0-qemu
[ERROR] Failed to bind port 8080: Address already in use
✗ Port conflict - service can't start

# Step 4: Check what's using the port
$ netstat -tlnp | grep 8080
...

# Solution: Free the port or change service port
```

---

## The Complete Picture

### What Each Level Tells You

```
┌─ Level 1: Pod Running ─┐
│ Kubernetes is happy    │
│ Pod scheduled ✓        │
│ = K8s did its job      │
└──────────────────────────────────────────────┐
                                               │
        ┌─ Level 2: Container Ready ─┐        │
        │ QEMU started ✓             │        │
        │ Kernel booted ✓            │        │
        │ = QEMU did its job         │        │
        └──────────────────────────────────────┤
                                               │
                ┌─ Level 3: Boot Messages ─┐  │
                │ Kernel initialized ✓     │  │
                │ Services starting ✓      │  │
                │ = Unikernel booting      │  │
                └────────────────────────────────┐
                                                  │
                        ┌─ Level 4: Working ─┐  │
                        │ Service responds ✓ │  │
                        │ CPU is active ✓    │  │
                        │ = Unikernel doing  │  │
                        │   actual work      │  │
                        └────────────────────────┘

All 4 levels true = Unikernel is FULLY OPERATIONAL ✅
```

---

## Indicator Matrix

| Level | Tool | Success Indicator | Failure Indicator | Next Step |
|-------|------|-------------------|-------------------|-----------|
| **1: Scheduled** | `view-qemu-pods.sh` | Status: Running | Status: Pending/Failed | Fix scheduling |
| **2: QEMU** | `check-qemu-inside.sh` | Container Ready: true | Container Ready: false | Check pod logs |
| **3: Boot** | `kubectl logs` | "Linux version" msg | "panic" or no output | Check unikernel image |
| **4: Work** | `curl` / `top` | Service responds | No response / 0% CPU | Check port/firewall |

---

## Commands Quick Reference

```bash
# LEVEL 1: Pod scheduled?
bash scripts/view-qemu-pods.sh | grep my-pod

# LEVEL 2: Unikernel running?
bash scripts/check-qemu-inside.sh default my-pod

# LEVEL 3: Kernel booting?
kubectl logs default my-pod | head -20

# LEVEL 4: Actually working?
curl http://pod-ip:8080
# or
ps aux | grep qemu  # See %CPU
# or
netstat -tlnp | grep 8080  # See ports
```

---

## Most Common Scenarios

### Scenario A: "My pod says Running but nothing's happening"

```bash
# Check all levels
bash scripts/view-qemu-pods.sh        # ← Shows Running
bash scripts/check-qemu-inside.sh ... # ← Shows NOT READY
kubectl logs ...                      # ← Shows ImagePullBackOff

# Problem: Container image issue
# Fix: Check image exists, correct name/registry
```

### Scenario B: "Pod running, QEMU ready, but no logs"

```bash
bash scripts/view-qemu-pods.sh        # ← Running
bash scripts/check-qemu-inside.sh ... # ← Ready
kubectl logs ...                      # ← Empty

# Problem: No serial output configured
# Fix: Check unikernel startup command
```

### Scenario C: "Everything looks good but service not responding"

```bash
bash scripts/view-qemu-pods.sh        # ← Running  
bash scripts/check-qemu-inside.sh ... # ← Ready
kubectl logs ...                      # ← Shows "started"
curl http://pod-ip:8080               # ← No response

# Problem: Port binding or network issue
# Fix: Check netstat for listening ports, check firewall
```

### Scenario D: "All green across the board"

```bash
bash scripts/view-qemu-pods.sh        # ✓ Running
bash scripts/check-qemu-inside.sh ... # ✓ Ready
kubectl logs ...                      # ✓ Boot messages
curl http://pod-ip:8080               # ✓ 200 OK response

# ✅ UNIKERNEL IS FULLY OPERATIONAL
```

---

## The Golden Rule

**Check all 4 levels to know the real status:**

```
Level 1: Scheduled?  YES ✓
Level 2: QEMU Up?    YES ✓
Level 3: Booted?     YES ✓
Level 4: Working?    YES ✓
         ==================
         UNIKERNEL RUNNING ✅
```

Miss even one level, and you might have a false positive!

---

## Tools Provided

| Tool | File | Purpose | Output |
|------|------|---------|--------|
| **Pod Status** | `scripts/view-qemu-pods.sh` | Check all pods at K8s level | Table |
| **Inside Check** | `scripts/check-qemu-inside.sh` | Check QEMU process and readiness | Report |
| **Full Details** | `scripts/inspect-qemu-pod.sh` | Deep dive into one pod | Detailed report |
| **Live Watch** | `scripts/qemu-scheduling-dashboard.sh` | Monitor scheduling in real-time | Live dashboard |

---

## Documentation Files

| File | Content |
|------|---------|
| **QEMU_INSIDE_QUICK_REF.md** | This quick reference (you are here) |
| **QEMU_INSIDE_MONITORING.md** | Complete guide with all details |
| **QEMU_VISUAL_MONITORING.md** | Pod-level monitoring guide |
| **QEMU_VISUAL_MONITORING_START.md** | Quick start (5 minutes) |
| **QEMU_VISUAL_SOLUTION.md** | Complete solution overview |

---

## Start Here

### Your First Check (30 seconds)

```bash
# Create a test pod
kubectl run test-pod --image=alpine:latest --restart=Never -- sleep 3600

# Check it scheduled
bash scripts/view-qemu-pods.sh

# Check inside QEMU
bash scripts/check-qemu-inside.sh default test-pod

# See what it's doing
kubectl logs default test-pod
```

**You'll see all 4 levels working. ✅**

---

## The Big Picture

```
Your Unikernel
       ↑
       │ Inside QEMU
       ├─ Boot messages → kubectl logs
       ├─ Process running → ps aux
       ├─ CPU/memory → top
       └─ Working → curl/telnet
       ↑
   QEMU VM
       ↑
       │ Container managed by
       ├─ Readiness status → check-qemu-inside.sh
       ├─ Container state → kubectl describe
       └─ Restart count → check-qemu-inside.sh
       ↑
   Pod
       ↑
       │ Scheduled by
       ├─ Node assignment → view-qemu-pods.sh
       ├─ Status Running → view-qemu-pods.sh
       └─ QEMU indicator → view-qemu-pods.sh
       ↑
   Kubernetes
```

Each layer has tools to inspect it. **Use them to verify the full stack.**

---

**Status**: ✅ Complete monitoring solution ready  
**Date**: January 30, 2026
