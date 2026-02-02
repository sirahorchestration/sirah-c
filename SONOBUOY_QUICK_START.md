# Sonobuoy Quick Start Guide for Sirah

**TL;DR**: Test how well Sirah conforms to Kubernetes API specification

---

## 5-Minute Setup

### Prerequisites
- Sirah cluster running (API server, scheduler, controller)
- `kubectl` configured to connect to Sirah
- etcd running and accessible
- 15 minutes to 8 hours (depending on test mode)

### Quick Test (15 minutes total)

#### On Windows (PowerShell):
```powershell
# 1. Install Sonobuoy
.\setup-sonobuoy.ps1 -Command Install

# 2. Run quick conformance test
.\setup-sonobuoy.ps1 -Command RunQuick

# 3. Results displayed automatically
```

#### On Linux/Mac:
```bash
# 1. Install Sonobuoy
chmod +x ./setup-sonobuoy.sh
./setup-sonobuoy.sh install

# 2. Run quick conformance test
./setup-sonobuoy.sh quick

# 3. Results displayed automatically
```

---

## What Gets Tested?

Sonobuoy runs **~100 conformance tests** covering:

| Feature | Tests | Notes |
|---------|-------|-------|
| **Pod Operations** | ~30 | Create, delete, list, watch, logs |
| **Service Operations** | ~25 | Create, delete, DNS discovery, endpoints |
| **Deployment Operations** | ~20 | Create, update, rolling update |
| **Namespaces** | ~10 | Isolation, cleanup |
| **ConfigMaps/Secrets** | ~8 | CRUD, patching |
| **Events** | ~5 | Event recording |

---

## Expected Results

### For Sirah MVP (Week 8):
- **40-60% conformance** is realistic (2-3 thousand passing tests out of 5000 total)
- Pod operations: 85-95% pass rate
- Service operations: 70-85% pass rate
- Deployments: 75-85% pass rate
- RBAC: 0-20% pass rate (not implemented)
- Storage: 0% pass rate (not in scope)

### Example Output:
```
╔════════════════════════════════════════╗
║ SONOBUOY CONFORMANCE TEST RESULTS      ║
╠════════════════════════════════════════╣
║ Total Tests:     4987
║ Passed:          2243 (45%) ✓
║ Failed:          1589 (32%)
║ Skipped:         1155 (23%)
╠════════════════════════════════════════╣
║ Conformance Score: 45%
╚════════════════════════════════════════╝
```

---

## Test Modes

| Mode | Duration | Use Case |
|------|----------|----------|
| **lite** | 10-15 min | Quick sanity check, CI/CD |
| **quick** | 30-45 min | Weekly validation |
| **certified-conformance** | 4-8 hours | Official compliance certification |

---

## Step-by-Step Usage

### Step 1: Verify Sirah is Running

```bash
# Check API server
kubectl get nodes

# Check services are running
kubectl get svc -A

# Check pods can be scheduled
kubectl get pods -A
```

If these commands fail, start Sirah components first:
```bash
# Terminal 1: API Server
./bin/sirah-apiserver

# Terminal 2: Scheduler
./bin/sirah-scheduler

# Terminal 3: Controller Manager
./bin/sirah-controller

# Terminal 4 (optional): Kubelet
./bin/sirah-kubelet
```

### Step 2: Run Sonobuoy Tests

#### Option A: Quick Test (Recommended for first time)
```bash
# Windows
.\setup-sonobuoy.ps1 -Command RunQuick

# Linux
./setup-sonobuoy.sh quick
```

#### Option B: Full Certification (Official compliance)
```bash
# Windows
.\setup-sonobuoy.ps1 -Command RunFull

# Linux
./setup-sonobuoy.sh full
```

### Step 3: Wait for Results

Test progress (for full certification):
- Week 1 (Pods/Services): ~2 hours
- Week 2 (Deployments): ~1 hour
- Week 3 (RBAC/Networking): ~2 hours
- Week 4 (Advanced features): ~1-2 hours

Monitor with:
```bash
# Check status
sonobuoy status

# View logs
sonobuoy logs -f

# Or using script
./setup-sonobuoy.sh status
```

### Step 4: Analyze Results

Results are displayed automatically, showing:
- Total tests run
- Passed/failed/skipped counts
- Conformance percentage
- Top failing tests

Get detailed results:
```bash
# Windows
.\setup-sonobuoy.ps1 -Command Analyze

# Linux
./setup-sonobuoy.sh analyze
```

---

## Interpreting Results

### Passing Test
```
✓ [sig-apps] Deployment replica set has orphaning finalizer
```
→ Feature working correctly

### Failed Test
```
✗ [sig-api-machinery] List should paginate large result sets
```
→ Feature not fully implemented
→ Often indicates missing API feature
→ Low priority for MVP

### Skipped Test
```
~ [sig-storage] PersistentVolume should work
```
→ Feature not in scope for MVP
→ No impact on conformance score

---

## Common Failures & Fixes

### Failure: "Pod logs not available"
```
What: Sonobuoy can't read pod logs
Why: kubelet logs endpoint not implemented
Fix: Implement pod logs in endpoint handlers
Priority: Medium (Week 6-7)
```

### Failure: "Service endpoints not updating"
```
What: Service endpoints watch not working
Why: Watch notifications for Services not sent properly
Fix: Verify service watch implementation in handler
Priority: High (Week 5-6)
```

### Failure: "RBAC not enforced"
```
What: Any user can perform any action
Why: RBAC implementation not in MVP
Fix: Implement RBAC admission controller
Priority: Low (Post-MVP feature)
```

### Failure: "Affinity rules not respected"
```
What: Pod affinity/anti-affinity not enforced
Why: Scheduler doesn't check affinity rules
Fix: Add affinity checking to scheduler
Priority: Medium (Week 7-8)
```

---

## Performance Expectations

### Test Execution Timeline

**Quick Test (~45 min total)**:
```
Phase 1 (Pods):             15 min
Phase 2 (Services):         10 min
Phase 3 (Deployments):      10 min
Phase 4 (Other):            10 min
────────────────────────────────
Total:                       45 min
```

**Full Certification (~6 hours total)**:
```
Phase 1 (Pod tests):        90 min
Phase 2 (Service tests):    45 min
Phase 3 (Deployment tests): 45 min
Phase 4 (API tests):        60 min
Phase 5 (RBAC tests):       60 min
Phase 6 (Storage tests):    30 min (mostly skipped)
Phase 7 (Advanced tests):   60 min
────────────────────────────────
Total:                      6 hours
```

---

## Improving Conformance Scores

### Quick Wins (High ROI)
1. **Fix Pod logging** → +5% conformance
2. **Complete Service spec** → +10% conformance
3. **Fix Deployment rolling updates** → +8% conformance
4. **Implement Pod affinity** → +7% conformance
5. **Add basic RBAC** → +20% conformance

### Track Progress
```bash
# Run tests weekly and compare results
Date 1: 45% conformance (Baseline)
Date 2: 50% conformance (+5% improvement)
Date 3: 58% conformance (+8% improvement)
Date 4: 65% conformance (+7% improvement)
```

---

## Cleanup & Reset

```bash
# Windows
.\setup-sonobuoy.ps1 -Command Cleanup

# Linux
./setup-sonobuoy.sh cleanup
```

This removes:
- Sonobuoy namespace and pods
- Test artifacts
- Temporary resources

---

## Full Command Reference

### Windows PowerShell
```powershell
# Install
.\setup-sonobuoy.ps1 -Command Install

# Run tests
.\setup-sonobuoy.ps1 -Command RunLite       # ~15 min
.\setup-sonobuoy.ps1 -Command RunQuick      # ~45 min
.\setup-sonobuoy.ps1 -Command RunFull       # ~6 hours

# Manage
.\setup-sonobuoy.ps1 -Command Status        # Check progress
.\setup-sonobuoy.ps1 -Command Analyze       # View results
.\setup-sonobuoy.ps1 -Command Cleanup       # Clean up
.\setup-sonobuoy.ps1 -Command Help          # Show help
```

### Linux/Mac/WSL Bash
```bash
# Install
./setup-sonobuoy.sh install

# Run tests
./setup-sonobuoy.sh lite                    # ~15 min
./setup-sonobuoy.sh quick                   # ~45 min
./setup-sonobuoy.sh full                    # ~6 hours

# Manage
./setup-sonobuoy.sh status                  # Check progress
./setup-sonobuoy.sh analyze                 # View results
./setup-sonobuoy.sh cleanup                 # Clean up
./setup-sonobuoy.sh help                    # Show help
```

### Manual Sonobuoy Commands
```bash
# Install (if not using script)
curl -sfL https://github.com/vmware-tanzu/sonobuoy/releases/latest/download/sonobuoy_linux_amd64.tar.gz | tar xz

# Run
sonobuoy run --mode=quick --wait

# Monitor
sonobuoy status
sonobuoy logs -f

# Retrieve results
outfile=$(sonobuoy retrieve)
tar xzf $outfile

# Cleanup
sonobuoy delete
```

---

## Troubleshooting

### Tests hang or timeout
```bash
# Check cluster health
kubectl get nodes
kubectl get pods -n sonobuoy

# Check Sonobuoy logs
sonobuoy logs --plugin=e2e -n 100

# Force cleanup and restart
sonobuoy delete --wait
```

### Out of memory errors
```bash
# Reduce test parallelism
sonobuoy run --mode=quick \
    --plugin-env=e2e.LOAD_MULTIPLIER=0.5
```

### etcd connection errors
```bash
# Verify etcd is running
curl http://localhost:2379/v2/members

# Check Sirah API server logs for errors
```

### Results incomplete
```bash
# Check summary file
cat ./results/plugins/e2e/results/global/summary.txt

# Retry with longer timeout
sonobuoy run --mode=certified-conformance \
    --timeout=36000 --wait
```

---

## Next Steps

1. ✅ **Run quick test** (15 min) → Get baseline
2. ✅ **Analyze failures** (30 min) → Identify gaps
3. ✅ **Plan improvements** (1 hour) → Prioritize features
4. ✅ **Implement fixes** (varies) → Improve conformance
5. ✅ **Retest weekly** → Track progress

---

## Resources

- **Sonobuoy GitHub**: https://github.com/vmware-tanzu/sonobuoy
- **Kubernetes API Specification**: https://kubernetes.io/docs/reference/kubernetes-api/
- **Conformance Testing**: https://kubernetes.io/docs/setup/best-practices/conformance/

---

**TL;DR Command**:
```bash
./setup-sonobuoy.sh quick  # Run 45-min test and see results
```

**Expected Output**: 
```
Conformance Score: 45%
Total Tests: 4987
Passed: 2243 ✓
Failed: 1589
```

Done! Now you know what works and what needs fixing.
