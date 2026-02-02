# Sonobuoy Kubernetes Conformance Testing for Sirah

**Date**: January 31, 2026  
**Purpose**: Establish baseline Kubernetes API conformance for Sirah implementation  
**Tools**: Sonobuoy (vmware-tanzu), kubectl, jq  
**Expected Duration**: 4-8 hours for full certified-conformance test suite

---

## 1. Quick Start

### Prerequisites

Before running Sonobuoy, ensure:

1. **Sirah Control Plane Running**
   ```bash
   # Start all Sirah components
   ./bin/sirah-apiserver
   ./bin/sirah-scheduler
   ./bin/sirah-controller
   ./bin/sirah-kubelet (optional, for pod execution)
   ```

2. **kubectl Configured**
   ```bash
   # Point kubectl to Sirah API server
   kubectl config set-cluster sirah --server=http://localhost:8080
   kubectl config set-context sirah --cluster=sirah --user=admin
   kubectl config use-context sirah
   
   # Verify connectivity
   kubectl get nodes
   kubectl get pods --all-namespaces
   ```

3. **etcd Running**
   ```bash
   # If not bundled with Sirah
   etcd --listen-client-urls=http://localhost:2379 \
        --advertise-client-urls=http://localhost:2379
   ```

4. **Sonobuoy Installed**
   - See installation section below

---

## 2. Installation

### On Linux/Mac (WSL recommended on Windows)

```bash
# Download latest Sonobuoy release
SONOBUOY_VERSION=0.57.1  # Check for latest at github.com/vmware-tanzu/sonobuoy/releases
SONOBUOY_OS=$(uname -s | tr '[:upper:]' '[:lower:]')
SONOBUOY_ARCH=$(uname -m)

curl -sfL -O "https://github.com/vmware-tanzu/sonobuoy/releases/download/v${SONOBUOY_VERSION}/sonobuoy_${SONOBUOY_OS}_${SONOBUOY_ARCH}.tar.gz"

# Extract
tar -xzf sonobuoy_*.tar.gz

# Verify
./sonobuoy version

# Make globally available (optional)
chmod +x sonobuoy
sudo mv sonobuoy /usr/local/bin/
```

### On Windows (PowerShell)

```powershell
# Download latest Sonobuoy release
$SONOBUOY_VERSION = "0.57.1"  # Update as needed
$SONOBUOY_URL = "https://github.com/vmware-tanzu/sonobuoy/releases/download/v${SONOBUOY_VERSION}/sonobuoy_windows_amd64.tar.gz"

# Create sonobuoy directory
New-Item -ItemType Directory -Path "C:\sonobuoy" -Force | Out-Null

# Download
Invoke-WebRequest -Uri $SONOBUOY_URL -OutFile "C:\sonobuoy\sonobuoy.tar.gz"

# Extract (using tar command available in Windows 10+)
tar -xzf "C:\sonobuoy\sonobuoy.tar.gz" -C "C:\sonobuoy"

# Verify
C:\sonobuoy\sonobuoy.exe version

# Add to PATH (optional)
[Environment]::SetEnvironmentVariable(
    "Path",
    [Environment]::GetEnvironmentVariable("Path", "User") + ";C:\sonobuoy",
    "User"
)
```

---

## 3. Running Conformance Tests

### Mode Options

| Mode | Tests | Duration | Scope |
|------|-------|----------|-------|
| **lite** | ~50 essential tests | 10-15 min | Pod, Service, basic APIs |
| **quick** | ~100 quick tests | 30-45 min | Broader coverage |
| **certified-conformance** | ~5000 full tests | 4-8 hours | Complete v1 API specification |
| **custom** | User-defined | Variable | Specific test plugins |

### Recommended Test Progression for Sirah

**Phase 1: Quick Sanity Check (10 minutes)**
```bash
sonobuoy run --mode=lite --wait
```

**Phase 2: Broader Coverage (45 minutes)**
```bash
sonobuoy run --mode=quick --wait
```

**Phase 3: Full Certification (4-8 hours)**
```bash
sonobuoy run --mode=certified-conformance --wait
```

---

## 4. Running Tests Step-by-Step

### Step 1: Start Sonobuoy Test Suite

```bash
# Basic certified-conformance run (minimal output)
sonobuoy run --mode=certified-conformance --wait

# Or with verbose output
sonobuoy run --mode=certified-conformance --wait --verbose

# Or to run without waiting (for long-running tests)
sonobuoy run --mode=certified-conformance
```

**What happens**:
1. Sonobuoy creates `sonobuoy` namespace
2. Deploys e2e test pods and systemd-logs daemonset
3. Runs ~5000 conformance tests against your cluster
4. Collects results, logs, and metrics

**Monitor progress**:
```bash
# Check status
sonobuoy status

# Watch logs in real-time
sonobuoy logs -f

# See specific plugin logs
sonobuoy logs -f --plugin e2e
```

### Step 2: Wait for Completion

**Timeout Considerations**:
```bash
# Default timeout: 24 hours
# Modify if needed:
sonobuoy run --mode=certified-conformance --timeout=7200 --wait
```

**On Windows** (if running under WSL):
```powershell
# Alternative: Run asynchronously and check later
C:\sonobuoy\sonobuoy.exe run --mode=certified-conformance

# Check status in PowerShell
while ((C:\sonobuoy\sonobuoy.exe status).Contains("running")) {
    Start-Sleep -Seconds 60
}

Write-Host "Tests complete!"
```

---

## 5. Retrieving Results

### Automatic Retrieval After `--wait`

```bash
# After sonobuoy run --wait completes, retrieve results
outfile=$(sonobuoy retrieve)
echo "Results saved to: $outfile"

# Create results directory
mkdir -p ./results
tar xzf "$outfile" -C ./results
```

### Manual Retrieval

```bash
# If run without --wait, retrieve later
outfile=$(sonobuoy retrieve)
tar xzf "$outfile" -C ./results

# Or specify custom path
sonobuoy retrieve -p ~/my-sonobuoy-results

# Clean up Sonobuoy resources
sonobuoy delete
```

---

## 6. Analyzing Results

### View Summary Results

```bash
# Extract and view e2e test results
cat ./results/plugins/e2e/results/global/summary.txt

# View in JSON format (full details)
cat ./results/plugins/e2e/results/global/e2e.json | jq .

# Count passed/failed
cat ./results/plugins/e2e/results/global/e2e.json | jq '.[] | select(.result == "passed") | .name' | wc -l
cat ./results/plugins/e2e/results/global/e2e.json | jq '.[] | select(.result == "failed") | .name' | wc -l
```

### Detailed Test Results Script

```bash
#!/bin/bash
# save as: analyze_sonobuoy_results.sh

RESULTS_DIR="${1:-.}"

if [ ! -d "$RESULTS_DIR/plugins/e2e/results/global" ]; then
    echo "Error: Results directory not found"
    echo "Usage: $0 <path-to-extracted-results>"
    exit 1
fi

echo "=== SONOBUOY CONFORMANCE TEST RESULTS ==="
echo

# Summary stats
TOTAL=$(cat $RESULTS_DIR/plugins/e2e/results/global/e2e.json | jq '.[] | .result' | wc -l)
PASSED=$(cat $RESULTS_DIR/plugins/e2e/results/global/e2e.json | jq '.[] | select(.result == "passed")' | wc -l)
FAILED=$(cat $RESULTS_DIR/plugins/e2e/results/global/e2e.json | jq '.[] | select(.result == "failed")' | wc -l)
SKIPPED=$(cat $RESULTS_DIR/plugins/e2e/results/global/e2e.json | jq '.[] | select(.result == "skipped")' | wc -l)

echo "Test Statistics:"
echo "  Total Tests:   $TOTAL"
echo "  Passed:        $PASSED ($(( PASSED * 100 / TOTAL ))%)"
echo "  Failed:        $FAILED ($(( FAILED * 100 / TOTAL ))%)"
echo "  Skipped:       $SKIPPED"
echo

# Show failed tests
if [ $FAILED -gt 0 ]; then
    echo "=== FAILED TESTS ($FAILED) ==="
    cat $RESULTS_DIR/plugins/e2e/results/global/e2e.json | \
        jq '.[] | select(.result == "failed") | "\(.name)\n  Error: \(.err)"' -r
    echo
fi

# Show skipped tests
if [ $SKIPPED -gt 0 ]; then
    echo "=== SKIPPED TESTS ($SKIPPED) (Sample) ==="
    cat $RESULTS_DIR/plugins/e2e/results/global/e2e.json | \
        jq '.[] | select(.result == "skipped") | .name' -r | head -20
    echo
fi

# Conformance percentage
CONFORMANCE=$(echo "scale=2; $PASSED * 100 / $TOTAL" | bc)
echo "=== CONFORMANCE SCORE ==="
echo "  $CONFORMANCE% ($PASSED/$TOTAL tests passed)"
```

### PowerShell Analysis Script

```powershell
# save as: Analyze-SonobuoyResults.ps1

param(
    [string]$ResultsPath = ".\results"
)

if (-not (Test-Path "$ResultsPath\plugins\e2e\results\global\e2e.json")) {
    Write-Error "Results file not found: $ResultsPath\plugins\e2e\results\global\e2e.json"
    exit 1
}

# Read JSON results
$results = Get-Content "$ResultsPath\plugins\e2e\results\global\e2e.json" | ConvertFrom-Json

# Count by result type
$passed = @($results | Where-Object { $_.result -eq "passed" }).Count
$failed = @($results | Where-Object { $_.result -eq "failed" }).Count
$skipped = @($results | Where-Object { $_.result -eq "skipped" }).Count
$total = $results.Count

Write-Host "`n=== SONOBUOY CONFORMANCE TEST RESULTS ===`n"

Write-Host "Test Statistics:"
Write-Host "  Total Tests:   $total"
Write-Host "  Passed:        $passed ($([math]::Round($passed * 100 / $total, 1))%)"
Write-Host "  Failed:        $failed ($([math]::Round($failed * 100 / $total, 1))%)"
Write-Host "  Skipped:       $skipped"
Write-Host ""

# Show failed tests
if ($failed -gt 0) {
    Write-Host "=== FAILED TESTS ($failed) ==="
    $results | Where-Object { $_.result -eq "failed" } | ForEach-Object {
        Write-Host "  $($_.name)"
        Write-Host "    Error: $($_.err)"
    }
    Write-Host ""
}

# Show skipped tests (sample)
if ($skipped -gt 0) {
    Write-Host "=== SKIPPED TESTS ($skipped) (First 20) ==="
    $results | Where-Object { $_.result -eq "skipped" } | Select-Object -First 20 | ForEach-Object {
        Write-Host "  $($_.name)"
    }
    Write-Host ""
}

# Conformance percentage
$conformance = [math]::Round($passed * 100 / $total, 2)
Write-Host "=== CONFORMANCE SCORE ==="
Write-Host "  $conformance% ($passed/$total tests passed)"
```

---

## 7. Expected Results for Sirah MVP

### Realistic Expectations (Week 8 of 8-week plan)

| Feature | Expected Pass Rate | Notes |
|---------|-------------------|-------|
| **Pod Operations** | 85-95% | Core CRUD should work, advanced features may fail |
| **Service Operations** | 70-85% | Basic services work, network policies/ingress may not |
| **Deployment Operations** | 75-85% | Rolling updates, basic strategies supported |
| **ConfigMaps/Secrets** | 70-80% | CRUD works, immutable/signing features may not |
| **Events** | 60-70% | Basic event recording, some edge cases fail |
| **Namespaces** | 80-90% | Basic namespace isolation |
| **Nodes** | 40-60% | Read-only implementation, limited status |
| **RBAC** | 0-20% | Not implemented in MVP |
| **Networking** | 30-50% | Basic service DNS, no network policies |
| **Storage** | 0-10% | PVs/PVCs not in scope for MVP |
| **Overall Conformance** | **40-60%** | Reasonable for MVP; focus on core pod/service APIs |

### High-Value Targets (Post-MVP Improvement)

To increase conformance from 50% to 75%+:
1. ✅ Complete pod spec support (all fields)
2. ✅ Complete service spec support
3. ✅ Implement missing watch features
4. ✅ Add RBAC basic support
5. ✅ Improve node status reporting

---

## 8. Interpreting Failed Tests

### Common Failures (and What They Mean)

```
FAIL: [sig-apps] Deployment, should create new replicasets and scale down old ones
  → Missing: Rolling update strategy, replicas field handling
  → Fix: Implement deployment controller lifecycle

FAIL: [sig-network] Service, should be able to connect to MySQL from different namespace
  → Missing: Service DNS across namespaces
  → Fix: Implement DNS service in Sirah

FAIL: [sig-api-machinery] List, should return partial results if list is large
  → Missing: Pagination support (limit/continue parameters)
  → Fix: Implement pagination in API handler

FAIL: [sig-storage] PersistentVolume, should support block volume
  → Missing: PV/PVC implementation
  → Fix: Not in MVP scope; acceptable to skip

SKIP: [sig-node] Kubelet, should provide logs for containers
  → Missing: Pod logs capture
  → Fix: Implement kubelet log stream endpoint
```

### Categorizing Results

```bash
# By test category
cat results/plugins/e2e/results/global/e2e.json | \
    jq '.[] | .name | split(" ")[0]' | sort | uniq -c

# By result type
echo "Passed by category:"
cat results/plugins/e2e/results/global/e2e.json | \
    jq '.[] | select(.result == "passed") | .name | split(" ")[0]' | sort | uniq -c
```

---

## 9. Continuous Testing Workflow

### Automated Testing Pipeline

```bash
#!/bin/bash
# save as: run-conformance-suite.sh

set -e

TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULTS_DIR="conformance_results_$TIMESTAMP"

echo "[$(date)] Starting Sirah conformance tests..."

# Verify Sirah is running
if ! kubectl cluster-info &>/dev/null; then
    echo "ERROR: Sirah cluster not accessible"
    exit 1
fi

# Run tests
echo "[$(date)] Running certified-conformance suite (this may take 4-8 hours)..."
sonobuoy run --mode=certified-conformance --wait --timeout=28800

# Retrieve results
echo "[$(date)] Retrieving results..."
outfile=$(sonobuoy retrieve)
mkdir -p "$RESULTS_DIR"
tar xzf "$outfile" -C "$RESULTS_DIR"

# Analyze
echo "[$(date)] Analyzing results..."
./analyze_sonobuoy_results.sh "$RESULTS_DIR"

# Archive
echo "[$(date)] Archiving results..."
tar czf "${RESULTS_DIR}.tar.gz" "$RESULTS_DIR"

# Cleanup Sonobuoy
sonobuoy delete

echo "[$(date)] Complete! Results: $RESULTS_DIR"
```

### Post-Test Checklist

```markdown
- [ ] Tests completed successfully
- [ ] Results retrieved and extracted
- [ ] JSON results analyzed
- [ ] Pass/fail counts recorded
- [ ] Failed tests categorized
- [ ] Conformance score calculated
- [ ] Comparison with previous runs done
- [ ] Issues logged in GitHub/tracking system
- [ ] Results archived for audit trail
```

---

## 10. Improving Conformance Scores

### Priority 1: Core Pod Features (Biggest Impact)

```
Current: 85% → Target: 95%

Checklist:
- [ ] Implement missing Pod spec fields:
  - [ ] securityContext
  - [ ] affinity
  - [ ] tolerations
  - [ ] hostNetwork, hostPID, hostIPC
  
- [ ] Implement Pod status fields:
  - [ ] containerStatuses (all states)
  - [ ] hostIP
  - [ ] startTime
  - [ ] conditions
  
- [ ] Test with real workloads:
  - [ ] Database containers (MySQL, Redis)
  - [ ] Multi-container pods
  - [ ] Init containers
```

### Priority 2: Service & Networking (High Value)

```
Current: 70% → Target: 85%

Checklist:
- [ ] Complete Service spec support:
  - [ ] sessionAffinity
  - [ ] externalTrafficPolicy
  - [ ] healthCheckNodePort
  
- [ ] Implement DNS service discovery:
  - [ ] service.namespace.svc.cluster.local
  - [ ] service.svc.cluster.local
  - [ ] headless services
  
- [ ] Add network policies:
  - [ ] Ingress/egress rules
  - [ ] Label selectors
  - [ ] IP blocks
```

### Priority 3: Deployment Controller (Medium Value)

```
Current: 75% → Target: 85%

Checklist:
- [ ] Rolling update strategy:
  - [ ] maxSurge
  - [ ] maxUnavailable
  - [ ] progressDeadlineSeconds
  
- [ ] Deployment status:
  - [ ] observedGeneration
  - [ ] replicas
  - [ ] conditions
  
- [ ] Rollback support:
  - [ ] revision history
  - [ ] rollout undo
```

---

## 11. Troubleshooting

### Test Hangs or Times Out

```bash
# Check cluster health
kubectl get nodes
kubectl get pods -n sonobuoy

# Check Sonobuoy status
sonobuoy status

# View recent logs
sonobuoy logs --plugin=e2e -n 100

# If stuck, force cleanup
sonobuoy delete --wait
```

### Out of Memory / Resource Issues

```bash
# Sonobuoy runs test pods; ensure cluster has resources
kubectl top nodes
kubectl top pods -n sonobuoy

# Reduce test parallelism if needed
sonobuoy run --mode=certified-conformance \
    --plugin-env=e2e.LOAD_MULTIPLIER=0.5
```

### etcd Connection Issues

```bash
# Verify etcd is accessible
curl http://localhost:2379/v2/members

# Check Sirah API server logs for etcd errors
# (logs should indicate connection status)
```

### Incomplete Results

```bash
# If results are incomplete, check:
cat results/plugins/e2e/results/global/summary.txt

# Retry with longer timeout
sonobuoy run --mode=certified-conformance --timeout=36000
```

---

## 12. Documentation & Reporting

### Creating Conformance Report

```markdown
# Sirah Kubernetes Conformance Report

**Test Date**: January 31, 2026  
**Sirah Version**: 1.0-MVP  
**Test Tool**: Sonobuoy v0.57.1  
**Test Mode**: certified-conformance  
**Duration**: 6 hours 42 minutes  

## Results Summary

- **Total Tests**: 4,987
- **Passed**: 2,243 (45%)
- **Failed**: 1,589 (32%)
- **Skipped**: 1,155 (23%)
- **Conformance Score**: 45.0%

## Breakdown by Feature Area

| Feature | Pass Rate | Status | Priority |
|---------|-----------|--------|----------|
| Pods | 85% | Good | Maintain |
| Services | 72% | Fair | Improve |
| Deployments | 78% | Fair | Improve |
| ConfigMaps/Secrets | 70% | Fair | Improve |
| RBAC | 5% | Poor | Not MVP |
| Storage | 0% | Not Implemented | Post-MVP |

## Top Failures

1. Pod affinity rules not supported (45 failures)
2. Service endpoints not updating correctly (38 failures)
3. Deployment rolling update issues (32 failures)
4. Missing RBAC implementation (500+ failures)
5. Storage API not implemented (300+ failures)

## Recommendations

1. Implement Pod affinity/anti-affinity (high ROI)
2. Fix Service endpoint watch notifications
3. Improve Deployment controller reliability
4. Consider basic RBAC for compliance

## Next Steps

- [ ] Fix top 5 failure categories
- [ ] Retest in 2 weeks
- [ ] Target: 60% conformance for next release
```

---

## 13. Quick Reference Commands

```bash
# Installation
curl -sfL https://github.com/vmware-tanzu/sonobuoy/releases/latest/download/sonobuoy_linux_amd64.tar.gz | tar xz

# Run tests
sonobuoy run --mode=certified-conformance --wait

# Monitor
sonobuoy status
sonobuoy logs -f

# Retrieve results
outfile=$(sonobuoy retrieve)
tar xzf $outfile -C ./results

# Analyze
cat ./results/plugins/e2e/results/global/e2e.json | jq .

# Cleanup
sonobuoy delete

# Full pipeline
sonobuoy run --mode=certified-conformance --wait && \
    outfile=$(sonobuoy retrieve) && \
    tar xzf $outfile -C ./results && \
    cat ./results/plugins/e2e/results/global/e2e.json | jq '.[].result' | sort | uniq -c
```

---

## 14. Success Criteria

### For Sirah MVP (Week 8)

✅ **Minimum Acceptable**: 35-40% conformance  
- Core pod operations working
- Services functional
- Basic deployments working
- Critical bugs fixed

✅ **Good Progress**: 50-60% conformance  
- Most pod features implemented
- Services mostly working
- Deployments reliable
- Event system functional

✅ **Strong Implementation**: 70%+ conformance  
- Nearly all pod features
- Complete service support
- Reliable deployments
- Advanced features (affinity, taints)

### For Production (Post-MVP)

✅ **Production Ready**: 85%+ conformance  
- All core Kubernetes APIs working
- RBAC implemented
- Advanced scheduling features
- Excellent test coverage

---

**Next Steps**:
1. Install Sonobuoy
2. Ensure Sirah cluster is running
3. Run `sonobuoy run --mode=lite --wait` (quick test)
4. Analyze results
5. Plan improvements based on failures

**Expected Timeline**: 
- Quick test: 15 minutes
- Full certified-conformance: 4-8 hours

---

**Document Version**: 1.0  
**Status**: Ready for Testing  
**Created**: January 31, 2026
