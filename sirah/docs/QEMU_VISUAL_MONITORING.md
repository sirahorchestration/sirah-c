# Visual Monitoring of QEMU Unikernel Scheduling

**Status**: ✅ Complete  
**Date**: January 30, 2026

---

## Overview

Multiple tools have been created to visually monitor and inspect whether unikernels have been scheduled to QEMU. Choose the tool that best fits your needs.

---

## 🎯 Quick Start

### Option 1: Simple Pod Status View (Fastest)
```bash
bash scripts/view-qemu-pods.sh
```

**Output**:
```
✓ Cluster is healthy

📊 QEMU Nodes Available:
  ✓ worker1 (Status: True)

📦 QEMU-Scheduled Unikernels in 'default' Namespace:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Pod Name                   Status        Node          QEMU
test-pod                   ● Running     worker1       ✓ QEMU
e2e-qemu-pod               ● Running     worker1       ✓ QEMU
unscheduled-pod            ◐ Pending     unscheduled   ○
─────────────────────────────────────────────────────
```

### Option 2: Real-Time Dashboard (Best for Monitoring)
```bash
bash scripts/qemu-scheduling-dashboard.sh [namespace] [refresh-interval]
```

**Example**:
```bash
bash scripts/qemu-scheduling-dashboard.sh default 5
```

**Output**: Live-updating dashboard that refreshes every 5 seconds showing:
- Cluster health status
- Active QEMU nodes
- Pod distribution (Running/Pending/Scheduled)
- QEMU process count
- Detailed pod table

### Option 3: Detailed Pod Inspection (For Deep Dive)
```bash
bash scripts/inspect-qemu-pod.sh [namespace] [pod-name]
```

**Example**:
```bash
bash scripts/inspect-qemu-pod.sh default test-pod
```

**Output**: Complete pod details including:
- Metadata and labels
- Scheduling information (node assignment)
- Runtime class
- Container specifications
- Pod status and phase
- QEMU-specific indicators
- Useful debugging commands

---

## 📊 What Each Tool Shows

### view-qemu-pods.sh - Simple Overview

**Best for**: Quick status check

**Shows**:
- ✅ Cluster health
- ✅ Available QEMU nodes
- ✅ Pod name, status, assigned node
- ✅ QEMU scheduling indicator
- ✅ Statistics (total, scheduled, running)
- ✅ Number of QEMU processes

**Example Output**:
```
╔════════════════════════════════════════════════════════════════╗
║        QEMU UNIKERNEL SCHEDULING STATUS MONITOR               ║
╚════════════════════════════════════════════════════════════════╝

✓ Cluster is healthy

📊 QEMU Nodes Available:
  ✓ worker1 (Status: True)

📦 QEMU-Scheduled Unikernels in 'default' Namespace:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Pod Name                     Status       Node          QEMU
─────────────────────────────────────────────────────────────────
test-pod                     ● Running    worker1       ✓ QEMU
qemu-pod-2                   ● Running    worker1       ✓ QEMU
pending-pod                  ◐ Pending    unscheduled   ○
─────────────────────────────────────────────────────────────────

📈 Summary:
  Total Pods:        3
  Scheduled:         2 (to QEMU nodes)
  Running:           2
  Pending:           1

🚀 Running QEMU Processes:
  ✓ 2 QEMU process(es) running
```

---

### qemu-scheduling-dashboard.sh - Live Dashboard

**Best for**: Real-time monitoring and watching scheduling happen

**Shows**:
- ✅ Live-updating display (configurable refresh interval)
- ✅ Current timestamp
- ✅ Cluster health status
- ✅ Node availability
- ✅ Pod statistics with color-coded counts
- ✅ Detailed pod table with visual formatting
- ✅ QEMU process count and details
- ✅ Quick command reference

**Features**:
- Auto-refreshes every N seconds (default 5s)
- Color-coded pod status
- Visual pod state indicators (●, ◐, ✓, ✗)
- Live-updating statistics
- Keyboard shortcuts shown

**Example Usage**:
```bash
# Refresh every 2 seconds (fast monitoring)
bash scripts/qemu-scheduling-dashboard.sh default 2

# Default 5 second refresh
bash scripts/qemu-scheduling-dashboard.sh

# Custom namespace
bash scripts/qemu-scheduling-dashboard.sh production 3
```

---

### inspect-qemu-pod.sh - Detailed Inspection

**Best for**: Debugging specific pods

**Shows Complete Information**:
- ✅ Pod metadata (name, namespace, UID, creation time)
- ✅ Labels and annotations
- ✅ Scheduling details (node assigned, runtime class)
- ✅ Affinity rules configuration
- ✅ Container specifications with resources
- ✅ Pod status and phase
- ✅ Container states and restart counts
- ✅ QEMU-specific indicators
- ✅ Useful debugging commands

**Example Output**:
```
╔════════════════════════════════════════════════════════════════╗
║        POD QEMU SCHEDULING DETAILS - test-pod                 ║
╚════════════════════════════════════════════════════════════════╝

═══ METADATA ═══
  Name:        test-pod
  Namespace:   default
  UID:         abc12345...
  Created:     2026-01-30T17:10:23Z

  Labels:
    app: web
    runtime: qemu

═══ SCHEDULING INFORMATION ═══
  ✓ Node Assigned:  worker1 (✓ SCHEDULED)
  Runtime Class:    qemu

═══ CONTAINER SPECIFICATIONS ═══
  Container 1: test-container
    Image:        alpine:latest
    Resources:
      Requests: CPU=100m, Memory=64Mi
      Limits:   CPU=200m, Memory=128Mi

═══ POD STATUS ═══
  ● Phase:        Running
  Host IP:        10.0.0.1
  Pod IP:         10.0.1.5
  Start Time:     2026-01-30T17:10:25Z

  Conditions:
    ✓ Scheduled: True
    ✓ Ready: True
    ✓ PodScheduled: True

═══ CONTAINER RUNTIME STATUS ═══
  test-container:
    Ready:        true
    Restart Count: 0
    State:        Running
    Started:      2026-01-30T17:10:25Z

═══ QEMU SCHEDULING DETAILS ═══
  ✓ SCHEDULED TO QEMU
    Node:          worker1
    Assignment:    Complete

  QEMU Indicators:
    ✓ Runtime label: qemu
    ✓ RuntimeClassName: qemu
```

---

## 🔍 Key Visual Indicators

### Pod Status Symbols

| Symbol | Meaning |
|--------|---------|
| ● | Running - Pod is actively executing |
| ◐ | Pending - Pod is waiting to be scheduled |
| ✓ | Succeeded - Pod completed successfully |
| ✗ | Failed - Pod encountered an error |
| ? | Unknown - Status not determined |

### Scheduling Status

| Indicator | Meaning |
|-----------|---------|
| ✓ QEMU | Pod is scheduled to run on QEMU node |
| ○ | Pod is not QEMU-scheduled (unscheduled) |
| ✓ Scheduled | Pod has been assigned to a node |
| ⟳ Pending | Pod is waiting for scheduler |

### Color Coding

| Color | Meaning |
|-------|---------|
| 🟢 Green | Running/Healthy/Scheduled |
| 🟡 Yellow | Pending/Waiting |
| 🔴 Red | Failed/Unhealthy |
| 🔵 Blue | Information |
| 🟣 Magenta | Status/Formatting |

---

## 💡 How to Tell if a Pod is Running on QEMU

### Visual Clues

1. **Node Name Assigned**
   ```
   Pod Name: test-pod
   Node: worker1 ← Pod is assigned to a node
   ```
   If there's a node name (not "unscheduled"), the pod is scheduled.

2. **Status is Running**
   ```
   Status: ● Running ← Pod is actively executing
   ```
   Running pods are scheduled and executing.

3. **QEMU Indicator**
   ```
   QEMU: ✓ QEMU ← Explicitly marked as QEMU
   ```
   The QEMU column shows ✓ if scheduled to QEMU.

4. **Container is Ready**
   ```
   Conditions:
     ✓ Ready: True ← Container is ready
     ✓ Scheduled: True ← Scheduled complete
   ```

---

## 📋 Common Scenarios

### Scenario 1: Pod Just Created (Not Yet Scheduled)
```
Pod Name: new-pod
Status: ◐ Pending
Node: unscheduled
QEMU: ○

Phase: Pending
Conditions:
  ✓ Scheduled: False
  ○ Ready: False
```
**Status**: Waiting for scheduler to assign to node

### Scenario 2: Pod Scheduled But Not Running Yet
```
Pod Name: test-pod
Status: ◐ Pending
Node: worker1
QEMU: ✓ QEMU

Phase: Pending
Conditions:
  ✓ Scheduled: True
  ○ Ready: False
```
**Status**: Assigned to node, waiting for container to start

### Scenario 3: Pod Running on QEMU ✅
```
Pod Name: test-pod
Status: ● Running
Node: worker1
QEMU: ✓ QEMU

Phase: Running
Conditions:
  ✓ Scheduled: True
  ✓ Ready: True

Container Runtime Status:
  State: Running
```
**Status**: ✅ UNIKERNEL IS ACTIVELY RUNNING ON QEMU

### Scenario 4: Pod Failed
```
Pod Name: failed-pod
Status: ✗ Failed
Node: worker1
QEMU: ✓ QEMU

Phase: Failed
Conditions:
  ✓ Scheduled: True
  ✗ Ready: False

Container Runtime Status:
  State: Terminated
  Exit Code: 1
  Reason: Error
```
**Status**: Pod was scheduled but encountered an error

---

## 🚀 Advanced Usage

### Monitor Specific Namespace
```bash
# View pods in 'production' namespace
bash scripts/view-qemu-pods.sh production

# Dashboard for production namespace
bash scripts/qemu-scheduling-dashboard.sh production 3
```

### Watch Real-Time Changes
```bash
# Refresh every 1 second (very fast)
bash scripts/qemu-scheduling-dashboard.sh default 1

# Or use watch command
watch -n 1 'bash scripts/view-qemu-pods.sh default'
```

### Get Detailed Pod Info
```bash
# Inspect a specific pod
bash scripts/inspect-qemu-pod.sh default test-pod

# Then create a pod and inspect it
bash scripts/inspect-qemu-pod.sh default my-new-pod
```

### Combine with Other Tools
```bash
# View pods, then get details on first one
POD=$(curl -s http://localhost:6443/api/v1/namespaces/default/pods | python3 -c "import sys, json; pods=json.load(sys.stdin)['items']; print(pods[0]['name'] if pods else '')")
bash scripts/inspect-qemu-pod.sh default "$POD"
```

---

## 🔧 Troubleshooting

### "Cluster is not responding"
```bash
# Start the cluster
bash start-cluster-full.sh

# Then try again
bash scripts/view-qemu-pods.sh
```

### "Pod not found"
```bash
# View all pods first
bash scripts/view-qemu-pods.sh

# Then use the exact pod name
bash scripts/inspect-qemu-pod.sh default exact-pod-name
```

### "QEMU processes not showing"
```bash
# The cluster might not have started pods yet
# Create a test pod first:
curl -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H 'Content-Type: application/json' \
  -d '{...pod spec...}'

# Then check again
bash scripts/view-qemu-pods.sh
```

---

## 📊 Understanding the Output

### Status Symbols
- `●` = Running (actively executing)
- `◐` = Pending (waiting to start)
- `✓` = Succeeded (completed)
- `✗` = Failed (error occurred)

### Node Assignment
- Pod name in "Node" column = Scheduled to that node
- "unscheduled" = Waiting for scheduler

### QEMU Column
- `✓ QEMU` = Scheduled to QEMU node
- `○` = Not scheduled or pending

---

## 📈 Interpreting Statistics

```
📈 Summary:
  Total Pods:        5      ← All pods in namespace
  Scheduled:         4      ← Pods assigned to nodes
  Running:           3      ← Pods actively executing
  Pending:           1      ← Pods waiting to start

🚀 Running QEMU Processes:
  ✓ 3 QEMU process(es) running
```

**Perfect case**: Scheduled = Total - Pending, Running = QEMU processes

---

## ✅ Verification Checklist

To confirm a unikernel is scheduled to QEMU:

- [ ] View status: `bash scripts/view-qemu-pods.sh`
- [ ] Check if pod name appears in list
- [ ] Verify "Node" column has a node name (not "unscheduled")
- [ ] Check "QEMU" column shows `✓ QEMU`
- [ ] Inspect details: `bash scripts/inspect-qemu-pod.sh default pod-name`
- [ ] Verify `Node Assigned: worker1 (✓ SCHEDULED)`
- [ ] Confirm `Phase: Running`
- [ ] Check condition `Scheduled: True`
- [ ] Check QEMU processes running: `pgrep -f qemu | wc -l`

All checks passing = **Unikernel Successfully Running on QEMU** ✅

---

## 🎓 Quick Reference

| Task | Command |
|------|---------|
| Quick status check | `bash scripts/view-qemu-pods.sh` |
| Live monitoring | `bash scripts/qemu-scheduling-dashboard.sh` |
| Inspect pod | `bash scripts/inspect-qemu-pod.sh default pod-name` |
| Watch changes | `watch -n 1 'bash scripts/view-qemu-pods.sh'` |
| Custom namespace | `bash scripts/view-qemu-pods.sh namespace-name` |
| Fast refresh (2s) | `bash scripts/qemu-scheduling-dashboard.sh default 2` |

---

## Summary

✅ **Three visual monitoring tools created**:
1. **view-qemu-pods.sh** - Simple overview
2. **qemu-scheduling-dashboard.sh** - Live dashboard
3. **inspect-qemu-pod.sh** - Detailed inspection

✅ **Easy to use**:
- Just run the script
- Color-coded output
- Clear status indicators
- No configuration needed

✅ **Shows everything you need**:
- Is the pod scheduled? → Check Node column
- Is it running? → Check Status column
- Is it QEMU? → Check QEMU column
- Detailed info? → Use inspect script

---

**Version**: 1.0  
**Status**: ✅ Complete  
**Date**: January 30, 2026
