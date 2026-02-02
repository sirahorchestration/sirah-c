# QEMU Visual Monitoring - Complete Solution

**Status**: ✅ Complete  
**Date**: January 30, 2026  
**User Question**: "Is there a way I can visually see if a unikernel was scheduled to QEMU?"

---

## Answer: YES! ✅

Three visual tools have been created to monitor QEMU unikernel scheduling in real-time.

---

## 🎯 Three Solutions

### 1. **Simple Status View** (Fastest)
```bash
bash scripts/view-qemu-pods.sh
```

**Best for**: Quick status checks
**Shows**: Pod names, status, node assignment, QEMU indicator
**Refresh**: Static (run again to update)
**Time to answer**: 1 second

---

### 2. **Live Dashboard** (Best for Monitoring)
```bash
bash scripts/qemu-scheduling-dashboard.sh
```

**Best for**: Watching scheduling happen in real-time
**Shows**: Live-updating statistics, pod table, QEMU processes
**Refresh**: Automatic (configurable)
**Time to answer**: Continuous

---

### 3. **Detailed Inspection** (For Deep Dive)
```bash
bash scripts/inspect-qemu-pod.sh default pod-name
```

**Best for**: Understanding why a specific pod is or isn't scheduled
**Shows**: Complete metadata, scheduling info, runtime details, resources
**Refresh**: Static (run again to update)
**Time to answer**: 2 seconds

---

## 📊 Visual Examples

### Example 1: Simple Status View

```bash
$ bash scripts/view-qemu-pods.sh

╔════════════════════════════════════════════════════════════════╗
║        QEMU UNIKERNEL SCHEDULING STATUS MONITOR               ║
╚════════════════════════════════════════════════════════════════╝

✓ Cluster is healthy

📊 QEMU Nodes Available:
  ✓ worker1 (Status: True)

📦 QEMU-Scheduled Unikernels in 'default' Namespace:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Pod Name                     Status       Node          QEMU
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
test-pod                     ● Running    worker1       ✓ QEMU  ← Scheduled!
my-app                       ● Running    worker1       ✓ QEMU  ← Scheduled!
pending-app                  ◐ Pending    unscheduled   ○       ← Not yet

📈 Summary:
  Total Pods:        3
  Scheduled:         2 (to QEMU nodes)
  Running:           2
  Pending:           1

🚀 Running QEMU Processes:
  ✓ 2 QEMU process(es) running
```

**Key Indicators**:
- **Node**: "worker1" = Scheduled, "unscheduled" = Not yet
- **Status**: ● = Running, ◐ = Pending
- **QEMU**: ✓ = Scheduled to QEMU, ○ = Not scheduled

---

### Example 2: Live Dashboard

```bash
$ bash scripts/qemu-scheduling-dashboard.sh

╔══════════════════════════════════════════════════════════════════════════════╗
║                  QEMU UNIKERNEL SCHEDULING DASHBOARD                         ║
║                      Real-Time Monitoring                                    ║
╚══════════════════════════════════════════════════════════════════════════════╝

Last Updated: 2026-01-30 17:10:45
Namespace: default
Refresh Rate: Every 5s (Ctrl+C to exit)

✓ Cluster Status: HEALTHY

═══ QEMU NODES ═══
  ✓ worker1 (True)

═══ POD DISTRIBUTION ═══
  Total:      2
  Running:    2
  Pending:    0
  Scheduled:  2

═══ UNIKERNEL PODS ═══

│ POD NAME              │ STATUS    │ NODE          │ PHASE        │
├───────────────────────┼───────────┼───────────────┼──────────────┤
│ test-pod              │ ✓ Running │ worker1       │ 1/1          │
│ my-unikernel          │ ✓ Running │ worker1       │ 1/1          │
└───────────────────────┴───────────┴───────────────┴──────────────┘

═══ QEMU PROCESS STATUS ═══
  ✓ Active QEMU Instances: 2

Next refresh in 5s... (Ctrl+C to exit)
```

**Live Updates**: Dashboard refreshes every 5 seconds automatically

---

### Example 3: Detailed Inspection

```bash
$ bash scripts/inspect-qemu-pod.sh default test-pod

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

═══ SUMMARY ═══
  ✓ UNIKERNEL IS ACTIVELY RUNNING ON QEMU
```

**Complete Picture**: All scheduling details visible at once

---

## 🔍 Reading the Signs

### To Know if a Unikernel is Scheduled to QEMU, Look For:

1. **Node Assignment**
   - Column shows "worker1" (or node name) = ✅ Scheduled
   - Column shows "unscheduled" = ⏳ Not yet

2. **Pod Status**
   - "● Running" = ✅ Actively executing
   - "◐ Pending" = ⏳ Waiting to start
   - "✓ Succeeded" = ✅ Completed
   - "✗ Failed" = ❌ Error

3. **QEMU Indicator**
   - "✓ QEMU" = ✅ Confirmed QEMU
   - "○" = ⏳ Not QEMU

4. **Condition Status**
   - "Scheduled: True" = ✅ Assigned to node
   - "Ready: True" = ✅ Container is ready
   - "Running" state = ✅ Actively running

### Quick Decision Tree

```
Is Node assigned?
  YES → Scheduled to node ✅
    Is Status Running?
      YES → Actively running on QEMU ✅
      NO → Still starting (check Phase)
  NO → Waiting for scheduler ⏳
    This is normal for new pods
```

---

## 📋 Workflow Examples

### Check if Pod is Scheduled

```bash
# Step 1: Quick status
bash scripts/view-qemu-pods.sh

# Step 2: If not sure, get full details
bash scripts/inspect-qemu-pod.sh default my-pod

# Step 3: Watch it schedule in real-time
bash scripts/qemu-scheduling-dashboard.sh default 2
```

### Monitor New Pod as It Schedules

```bash
# Terminal 1: Create a new pod
curl -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H 'Content-Type: application/json' \
  -d '{...pod spec...}'

# Terminal 2: Watch it schedule
bash scripts/qemu-scheduling-dashboard.sh default 1

# You'll see:
# 1. Pod appears with Pending status
# 2. Node becomes assigned
# 3. Status changes to Running
# 4. QEMU processes appear
```

### Debug Why Pod Isn't Scheduled

```bash
# Get full details
bash scripts/inspect-qemu-pod.sh default stuck-pod

# Look for:
# - Condition "Scheduled: False" or "Ready: False"
# - Resource limit issues
# - Node affinity mismatches
# - Error messages in Status section
```

---

## 🎯 What Each Script Does

### view-qemu-pods.sh
**Fastest status check**

```
Input:  (optional namespace)
Output: Table of all pods with status, node, and QEMU indicator
Speed:  Instant
Use:    "Is my pod scheduled?"
```

### qemu-scheduling-dashboard.sh
**Real-time monitoring**

```
Input:  (optional namespace) (optional refresh interval in seconds)
Output: Live-updating dashboard
Speed:  Continuous
Use:    "Watch scheduling happen live"
```

### inspect-qemu-pod.sh
**Deep dive into one pod**

```
Input:  namespace, pod-name
Output: Complete pod details
Speed:  2 seconds
Use:    "Why is this pod in this state?"
```

---

## 💡 Common Scenarios & What You'll See

### Scenario 1: Pod Just Created
```
Pod Name: new-pod
Node:     unscheduled
Status:   ◐ Pending
QEMU:     ○
```
→ Normal! Give it a few seconds, scheduler will assign it.

---

### Scenario 2: Pod Scheduled, Starting
```
Pod Name: test-pod
Node:     worker1
Status:   ◐ Pending
QEMU:     ✓ QEMU
Phase:    Pending
Conditions: Scheduled: True, Ready: False
```
→ Container is being started. Wait a moment for it to be ready.

---

### Scenario 3: Pod Running Successfully
```
Pod Name: test-pod
Node:     worker1
Status:   ● Running
QEMU:     ✓ QEMU
Phase:    Running
Conditions: Scheduled: True, Ready: True
State:    Running
```
→ ✅ **SUCCESS! Unikernel is actively running on QEMU**

---

### Scenario 4: Pod Failed
```
Pod Name: failed-pod
Node:     worker1
Status:   ✗ Failed
QEMU:     ✓ QEMU
Phase:    Failed
State:    Terminated (Exit Code: 1)
```
→ Pod was scheduled but encountered an error. Check logs for details.

---

## 🚀 Usage Examples

### Example 1: Quick Check
```bash
# "Is my pod running?"
bash scripts/view-qemu-pods.sh

# Takes 1 second
# Shows table of all pods
# Look for your pod with "● Running" and "✓ QEMU"
```

### Example 2: Watch Live
```bash
# "Show me pods as they schedule"
bash scripts/qemu-scheduling-dashboard.sh

# Auto-refreshes every 5 seconds
# Watch pod go from Pending → Running
# See QEMU process count increase
```

### Example 3: Debug One Pod
```bash
# "Why isn't my pod running?"
bash scripts/inspect-qemu-pod.sh default problem-pod

# Shows all details
# Look for error messages or Failed conditions
# Shows what went wrong
```

---

## 📊 Tools Summary Table

| Need | Command | Output | Speed |
|------|---------|--------|-------|
| Quick status | `view-qemu-pods.sh` | Table | 1s |
| Live watch | `qemu-scheduling-dashboard.sh` | Dashboard | Live |
| Full details | `inspect-qemu-pod.sh namespace pod` | Detailed | 2s |

---

## ✅ Complete Solution

### What You Get:

✅ **3 Visual Tools**
- Simple status view (fastest)
- Live dashboard (best for watching)
- Detailed inspection (for troubleshooting)

✅ **Clear Indicators**
- Pod status (● Running, ◐ Pending, ✓ Succeeded, ✗ Failed)
- Node assignment (shows which node or "unscheduled")
- QEMU status (✓ QEMU or ○)

✅ **Easy to Use**
- Just run the script
- No configuration needed
- Colors and symbols are clear

✅ **Multiple Ways to Check**
- One-command status check
- Real-time live monitoring
- Detailed pod inspection

---

## 🎓 Quick Reference

```bash
# "Is my pod scheduled to QEMU?"
bash scripts/view-qemu-pods.sh
# → Look for your pod name, check Node column (not "unscheduled")
#   and QEMU column (shows ✓ QEMU)

# "Show me pods as they start"
bash scripts/qemu-scheduling-dashboard.sh
# → Watch live dashboard as pods transition from Pending to Running

# "What's the complete status of this pod?"
bash scripts/inspect-qemu-pod.sh default my-pod
# → See all metadata, scheduling info, resource specs, and status
```

---

## Documentation

**[QEMU_VISUAL_MONITORING.md](QEMU_VISUAL_MONITORING.md)** - Complete guide with all details  
**[QEMU_VISUAL_MONITORING_START.md](QEMU_VISUAL_MONITORING_START.md)** - Quick start guide

---

## 🎉 You're All Set!

You now have visual tools to see exactly:
- ✅ Which unikernels are scheduled to QEMU
- ✅ What status they're in
- ✅ What node they're running on
- ✅ Complete scheduling details
- ✅ Real-time status changes

**Start here**: `bash scripts/view-qemu-pods.sh`

---

**Version**: 1.0  
**Status**: ✅ Complete  
**Date**: January 30, 2026
