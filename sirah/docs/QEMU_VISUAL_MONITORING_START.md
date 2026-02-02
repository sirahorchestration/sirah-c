# Quick Visual Monitoring - Start Here

## ⚡ The Simplest Way

Run this command to see all your QEMU-scheduled unikernels:

```bash
bash scripts/view-qemu-pods.sh
```

**That's it!** You'll see a table showing:
- Pod names
- Whether they're running or pending
- Which QEMU node they're assigned to
- Whether they're QEMU-scheduled ✓

---

## 📊 Example Output

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
my-unikernel                 ● Running    worker1       ✓ QEMU
pending-test                 ◐ Pending    unscheduled   ○
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

## 🎯 What Each Column Means

| Column | Meaning |
|--------|---------|
| **Pod Name** | Name of your unikernel pod |
| **Status** | ● Running, ◐ Pending, ✓ Done, ✗ Failed |
| **Node** | QEMU node it's running on (or "unscheduled") |
| **QEMU** | ✓ QEMU = Yes, ○ = Not scheduled |

---

## 🔍 Want More Details?

If you want to see everything about a specific pod:

```bash
bash scripts/inspect-qemu-pod.sh default pod-name
```

This shows:
- ✅ Whether it's scheduled
- ✅ Which node it's on
- ✅ Resource requirements
- ✅ Current status
- ✅ QEMU runtime details

---

## 👁️ Want Live Monitoring?

To watch pods scheduling in real-time:

```bash
bash scripts/qemu-scheduling-dashboard.sh
```

This auto-refreshes every 5 seconds showing live updates.

---

## ✅ Interpreting Results

### If you see this:
```
Pod Name:    test-pod
Status:      ● Running
Node:        worker1
QEMU:        ✓ QEMU
```

**Your unikernel IS running on QEMU! ✅**

---

### If you see this:
```
Pod Name:    test-pod
Status:      ◐ Pending
Node:        unscheduled
QEMU:        ○
```

**Your unikernel is waiting to be scheduled (still starting up)**

---

## 🚀 Try It Now

1. **Create a test pod**:
```bash
curl -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H 'Content-Type: application/json' \
  -d '{
    "apiVersion": "v1",
    "kind": "Pod",
    "metadata": {"name": "test-unikernel"},
    "spec": {
      "containers": [{
        "name": "app",
        "image": "alpine:latest"
      }]
    }
  }'
```

2. **Check its status**:
```bash
bash scripts/view-qemu-pods.sh
```

3. **See details**:
```bash
bash scripts/inspect-qemu-pod.sh default test-unikernel
```

---

## 📱 Mobile-Friendly View

The scripts use colors and symbols that work in any terminal:
- ✓ = Success/Yes
- ● = Running
- ◐ = Pending
- ✗ = Failed
- ○ = Not scheduled

---

## 🎓 Advanced Options

### View different namespace:
```bash
bash scripts/view-qemu-pods.sh production
```

### Watch with auto-refresh (2 seconds):
```bash
watch -n 2 'bash scripts/view-qemu-pods.sh'
```

### Dashboard with custom refresh (1 second):
```bash
bash scripts/qemu-scheduling-dashboard.sh default 1
```

---

## 💡 Remember

| To... | Run... |
|-------|--------|
| Quick status | `bash scripts/view-qemu-pods.sh` |
| Live updates | `bash scripts/qemu-scheduling-dashboard.sh` |
| Full details | `bash scripts/inspect-qemu-pod.sh default pod-name` |

---

## ✨ That's It!

You now have visual tools to see exactly which unikernels are scheduled to QEMU and their status.

**Start here**: `bash scripts/view-qemu-pods.sh`

---

**Questions?** See [QEMU_VISUAL_MONITORING.md](QEMU_VISUAL_MONITORING.md) for complete documentation.
