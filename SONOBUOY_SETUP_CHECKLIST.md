# Sonobuoy Setup Checklist

Use this checklist to ensure successful Sonobuoy conformance testing for Sirah.

---

## Pre-Test Checklist

### Prerequisites
- [ ] Kubernetes/Sirah cluster is running
  - [ ] API server responding: `kubectl cluster-info`
  - [ ] Nodes available: `kubectl get nodes`
  - [ ] Can create pods: `kubectl run test-pod --image=alpine`
  
- [ ] etcd is running and accessible
  - [ ] Test connection: `curl http://localhost:2379/v2/members`
  - [ ] Check API server logs for etcd errors

- [ ] kubectl is configured correctly
  - [ ] `kubectl get svc -A` shows expected services
  - [ ] `kubectl get pods -A` shows expected pods
  - [ ] Can execute kubectl commands without errors

### System Resources
- [ ] At least **10GB free disk space** (for test artifacts and results)
- [ ] At least **4GB free RAM** (tests run in cluster)
- [ ] Network connectivity to GitHub (for downloads)
- [ ] At least **1-2 hours** available (for quick test; 6+ hours for full)

### Installation
- [ ] Sonobuoy installed and executable
  - [ ] On Windows: `C:\sonobuoy\sonobuoy.exe --version` works
  - [ ] On Linux: `./sonobuoy/sonobuoy --version` works
  - [ ] Or use script: `./setup-sonobuoy.sh install`

- [ ] Supporting tools available
  - [ ] `kubectl` in PATH
  - [ ] `tar` available (for extraction)
  - [ ] `jq` available (optional, for better analysis)
  - [ ] `curl` available (for downloads)

---

## Test Execution Checklist

### Before Running Tests

- [ ] Cluster is clean and stable
  - [ ] No stuck pods or deployments
  - [ ] No high CPU/memory usage
  - [ ] etcd is responsive
  
- [ ] Decide test mode
  - [ ] **lite** (~15 min) - Quick sanity check, use for initial testing
  - [ ] **quick** (~45 min) - Weekly validation, recommended
  - [ ] **full** (~6 hours) - Official certification, save for clean state
  
- [ ] Have terminal/shell ready
  - [ ] Windows: PowerShell ready
  - [ ] Linux/Mac: Bash shell ready
  - [ ] Can run commands in repository root

- [ ] Document test run
  - [ ] Record test start time
  - [ ] Note Sirah version being tested
  - [ ] Note any special configuration
  - [ ] Have pen/paper or notes file ready for results

### Running Tests

**Windows PowerShell**:
- [ ] Navigate to project root: `cd c:\projects\k8s_unikernels`
- [ ] Run test: `.\setup-sonobuoy.ps1 -Command RunQuick`
- [ ] Or for full: `.\setup-sonobuoy.ps1 -Command RunFull`
- [ ] Wait for results (status shown automatically)

**Linux/Mac/WSL Bash**:
- [ ] Navigate to project root: `cd ~/projects/k8s_unikernels`
- [ ] Run test: `./setup-sonobuoy.sh quick`
- [ ] Or for full: `./setup-sonobuoy.sh full`
- [ ] Wait for results (status shown automatically)

**Manual (if not using scripts)**:
- [ ] `sonobuoy run --mode=quick --wait` (or lite/full)
- [ ] Monitor with: `sonobuoy status`
- [ ] View logs with: `sonobuoy logs -f`

### During Test Execution

- [ ] Let tests run without interruption
  - [ ] Don't kill processes
  - [ ] Don't restart cluster
  - [ ] Keep network connectivity stable

- [ ] Monitor progress (optional)
  - [ ] Run: `sonobuoy status` in another terminal
  - [ ] View logs: `sonobuoy logs -f --plugin=e2e`
  - [ ] Check resource usage: `kubectl top nodes; kubectl top pods`

- [ ] Be patient
  - [ ] Lite: ~15 minutes
  - [ ] Quick: ~45 minutes
  - [ ] Full: ~6 hours (first run may take longer)

---

## Post-Test Checklist

### Test Completion

- [ ] Tests completed (no errors)
  - [ ] See "Tests completed" message in output
  - [ ] Or `sonobuoy status` shows "complete"
  
- [ ] Results were retrieved
  - [ ] `conformance-results/` directory exists
  - [ ] Contains `plugins/` subdirectory
  - [ ] Contains `e2e.json` results file

### Results Analysis

- [ ] Analyzed results (automatic if using script)
  - [ ] Saw summary output with percentages
  - [ ] Noted total tests, passed, failed, skipped
  - [ ] Saw conformance score
  
- [ ] Identified failed tests
  - [ ] Listed top 10 failures (or seen from output)
  - [ ] Understood what each failure means
  - [ ] Noted patterns (e.g., RBAC all fail, pod logs partial)

- [ ] Reviewed skipped tests
  - [ ] Understood why tests were skipped
  - [ ] Noted if skips are expected (e.g., storage not implemented)
  - [ ] Recorded for future reference

### Document Results

- [ ] Recorded in SONOBUOY_TRACKING.md
  - [ ] Date and time of test
  - [ ] Test mode used (lite/quick/full)
  - [ ] Total tests, passed, failed, skipped counts
  - [ ] Conformance percentage
  - [ ] Top 10 failed tests
  - [ ] Any notes or observations

- [ ] Created summary report
  - [ ] Compared to previous run (if any)
  - [ ] Noted improvements/regressions
  - [ ] Identified top priority fixes
  - [ ] Estimated effort for each fix

- [ ] Saved results for audit trail
  - [ ] Renamed `conformance-results/` with date: `conformance-results-20260131/`
  - [ ] Archived results file: `conformance-results-20260131.tar.gz`
  - [ ] Stored in safe location

### Cleanup

- [ ] Removed Sonobuoy test artifacts
  - [ ] Run: `./setup-sonobuoy.sh cleanup` or `sonobuoy delete`
  - [ ] Verify: `kubectl get pods -n sonobuoy` shows nothing
  - [ ] Verify: `kubectl get ns sonobuoy` is gone

- [ ] Cluster returned to clean state
  - [ ] No stuck pods: `kubectl get pods -A | grep -i pending`
  - [ ] No errors: `kubectl get events -A | grep -i error`
  - [ ] Ready for next test: `kubectl get nodes` all Ready

---

## Results Interpretation Checklist

### Conformance Score

- [ ] Understood baseline
  - [ ] MVP (Week 8): 40-60% is normal
  - [ ] Production: 85%+ is target
  - [ ] Certified: 90%+ is required

- [ ] Compared to expectations
  - [ ] Pod tests: ~85-95% pass expected ✓
  - [ ] Service tests: ~70-85% pass expected ✓
  - [ ] Deployment tests: ~75-85% pass expected ✓
  - [ ] RBAC tests: ~0-20% pass expected (not implemented)
  - [ ] Storage tests: 0% pass expected (not in scope)

### Failed Tests Analysis

- [ ] Categorized failures
  - [ ] Count by feature area (Pod, Service, Deployment, etc.)
  - [ ] Count by severity (critical, high, medium, low)
  - [ ] Count by type (missing feature, implementation bug, edge case)

- [ ] Identified quick wins
  - [ ] Tests that failed for simple reasons
  - [ ] Would take 1-2 days to fix
  - [ ] Would improve conformance by 2-5%

- [ ] Identified blockers
  - [ ] Tests that require major implementation work
  - [ ] Would take 1+ week to fix
  - [ ] But high impact on conformance

### Skipped Tests Analysis

- [ ] Understood why tests skipped
  - [ ] Feature not implemented? (acceptable for MVP)
  - [ ] Test incompatibility? (may need investigation)
  - [ ] Resource constraints? (cluster resource issue)
  - [ ] Configuration issue? (check cluster config)

---

## Action Items Checklist

### Immediate (After Results)

- [ ] Created GitHub issue for each major failure
  - [ ] Title: "CONFORMANCE: [Feature] should [expected behavior]"
  - [ ] Description: Test name, current behavior, expected behavior
  - [ ] Label: `priority:high`, `area:api`, `type:bug`
  - [ ] Assigned to developer

- [ ] Prioritized failures by impact
  - [ ] P1 (Critical): Blocks core functionality
  - [ ] P2 (High): Reduces conformance by >5%
  - [ ] P3 (Medium): Improves compatibility
  - [ ] P4 (Low): Nice to have

### Short Term (Next Week)

- [ ] Began fixing high-priority failures
  - [ ] Started with quick wins
  - [ ] Assigned to team members
  - [ ] Set completion target

- [ ] Planned next test run
  - [ ] Scheduled for end of week
  - [ ] Expected conformance improvement
  - [ ] Will run lite/quick test again

### Medium Term (Weeks 2-8)

- [ ] Regular testing schedule
  - [ ] Lite test: After each change
  - [ ] Quick test: Daily/weekly
  - [ ] Full test: Weekly on clean state
  
- [ ] Tracking progress
  - [ ] Updated SONOBUOY_TRACKING.md weekly
  - [ ] Graphed conformance score over time
  - [ ] Noted which fixes had highest impact

- [ ] Planning for certification
  - [ ] Targeting 85%+ by week 8
  - [ ] Full certification run planned for week 8
  - [ ] Documentation updated

---

## Troubleshooting Checklist

### If Tests Hang/Timeout

- [ ] Checked cluster health
  - [ ] `kubectl get nodes` - all Ready?
  - [ ] `kubectl get pods -n sonobuoy` - any pending?
  - [ ] etcd responding: `curl http://localhost:2379/v2/members`

- [ ] Viewed logs
  - [ ] `sonobuoy logs -f` - any errors?
  - [ ] `kubectl logs -n sonobuoy -l component=sonobuoy` - any errors?
  - [ ] API server logs - any errors?

- [ ] Forced cleanup and restart
  - [ ] `sonobuoy delete --wait`
  - [ ] `kubectl get ns sonobuoy` - should be gone
  - [ ] Waited 30 seconds
  - [ ] Restarted test

### If Results are Incomplete

- [ ] Checked summary file
  - [ ] `cat results/plugins/e2e/results/global/summary.txt`
  - [ ] Shows what completed?
  - [ ] Shows any errors?

- [ ] Retried with longer timeout
  - [ ] `sonobuoy run --mode=quick --timeout=7200 --wait`
  - [ ] Or (longer): `--timeout=36000` for full test

- [ ] Checked resource availability
  - [ ] `kubectl top nodes` - any out of memory?
  - [ ] `kubectl top pods -n sonobuoy` - tests using too much?
  - [ ] `df -h` - disk space available?

### If etcd Connection Fails

- [ ] Verified etcd running
  - [ ] `curl http://localhost:2379/v2/members` returns data?
  - [ ] Or: `etcdctl member list`
  - [ ] Check etcd service status

- [ ] Checked API server logs
  - [ ] Look for "connection refused"
  - [ ] Look for "etcd: endpoint not found"
  - [ ] Check network connectivity

- [ ] Restarted Sirah components
  - [ ] Stop API server, scheduler, controller
  - [ ] Restart in order
  - [ ] Verify `kubectl` commands work
  - [ ] Retry Sonobuoy test

---

## Documentation Checklist

### Recording Results

- [ ] Filled in SONOBUOY_TRACKING.md template
  - [ ] Test date and duration
  - [ ] Sirah version tested
  - [ ] Total/passed/failed/skipped counts
  - [ ] Conformance percentage
  - [ ] Top 10 failed tests list
  - [ ] Issues identified section

- [ ] Created comparison table
  - [ ] If previous run exists, compared results
  - [ ] Showed improvement/regression
  - [ ] Explained changes made between runs

- [ ] Added notes section
  - [ ] Any issues encountered
  - [ ] Workarounds applied
  - [ ] Blockers identified
  - [ ] Next steps for improvement

### Sharing Results

- [ ] Created report for team
  - [ ] Summary of conformance score
  - [ ] Top 5 failures and priority
  - [ ] Proposed fixes and effort estimates
  - [ ] Next testing timeline

- [ ] Updated architecture documentation
  - [ ] Linked to latest conformance results
  - [ ] Updated API completeness status
  - [ ] Noted any architectural implications

- [ ] Shared with stakeholders
  - [ ] Results summary
  - [ ] Progress toward target (85%+)
  - [ ] Timeline for improvements

---

## Final Sign-Off

- [ ] All tests completed successfully
- [ ] Results analyzed and understood
- [ ] Action items created for failures
- [ ] Progress tracked in SONOBUOY_TRACKING.md
- [ ] Team briefed on results
- [ ] Next test run scheduled
- [ ] Documentation updated

**Test Date**: _______________  
**Test Mode**: _______________  
**Conformance Score**: _______________  
**Tested By**: _______________  
**Date Completed**: _______________  

---

## Quick Reference

### Commands
```bash
# Install
./setup-sonobuoy.sh install

# Run quick test
./setup-sonobuoy.sh quick

# View results
./setup-sonobuoy.sh analyze

# Cleanup
./setup-sonobuoy.sh cleanup
```

### Files
- 📖 [SONOBUOY_QUICK_START.md](SONOBUOY_QUICK_START.md) - 5-minute overview
- 📋 [SONOBUOY_TRACKING.md](SONOBUOY_TRACKING.md) - Results tracking template
- 📚 [SONOBUOY_CONFORMANCE_TESTING.md](sirah/SONOBUOY_CONFORMANCE_TESTING.md) - Full guide

### Contacts
- **Questions**: See troubleshooting guide
- **Bugs**: Create GitHub issue
- **Feedback**: Contact [Team]

---

**Document Version**: 1.0  
**Status**: Ready to Use  
**Created**: January 31, 2026
