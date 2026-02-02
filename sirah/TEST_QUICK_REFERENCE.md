# Quick Test Reference

## One-Command Test Everything

```bash
cd sirah && bash start-cluster-full.sh && bash tests/run_all_tests.sh
```

## Test Commands Cheat Sheet

| Command | Purpose | Duration |
|---------|---------|----------|
| `make test` | Run all tests | ~5 min |
| `make test-unit` | Unit tests only | <1 min |
| `make test-integration` | Integration tests | ~2 min |
| `make test-e2e` | E2E stability test | ~3 min |
| `bash tests/run_all_tests.sh` | Full test suite | ~5 min |
| `bash tests/integration/test_pod_lifecycle.sh` | Pod tests only | ~1 min |
| `bash tests/e2e/test_cluster_stability.sh` | Stability test | ~3 min |

## Expected Output

### ✅ All Tests Pass
```
==========================================
Test Summary
==========================================
Total Tests: 5
Passed: 5 ✅
Failed: 0
Success Rate: 100%

All tests passed! ✓
```

### Expected Test Results by Phase

**Phase 1: Unit Tests**
- `test_deployment_management` ✅
- `test_pod_management` ✅

**Phase 2: Integration Tests**
- `test_deployment_lifecycle.sh` ✅
- `test_pod_lifecycle.sh` ✅

**Phase 3: E2E Tests**
- `test_cluster_stability.sh` ✅

## Quick Cluster Setup

```bash
cd sirah

# Terminal 1: Start cluster
pkill -f sirah; sleep 1
pkill -f etcd; sleep 1
etcd --listen-client-urls http://localhost:2379 --advertise-client-urls http://localhost:2379 --data-dir /tmp/sirah-etcd &
sleep 2
./bin/sirah-apiserver --port 6443 &
./bin/sirah-scheduler &
./bin/sirah-controller &
./bin/sirah-kubelet --node-name worker1 &
sleep 2

# Terminal 2: Run tests
bash tests/run_all_tests.sh
```

Or use shortcut:
```bash
bash start-cluster-full.sh
bash tests/run_all_tests.sh
```

## Troubleshooting

### Tests Won't Start
```bash
# Check if cluster is running
curl http://localhost:6443/healthz

# If not, start it
bash start-cluster-full.sh

# Wait 5 seconds
sleep 5

# Try tests again
bash tests/run_all_tests.sh
```

### Port Already in Use
```bash
# Kill all sirah processes
pkill -f sirah
pkill -f etcd

# Clean up
rm -rf /tmp/sirah-etcd
mkdir -p /tmp/sirah-etcd

# Wait and retry
sleep 2
bash start-cluster-full.sh
```

### Tests Fail with "Cluster is not running"
```bash
# In the same terminal session, run:
bash start-cluster-full.sh

# Then in same terminal (don't use new terminal):
bash tests/run_all_tests.sh
```

## Test Summary

### What Gets Tested

✅ **Pod Operations**
- Create pods with correct names
- List pods with persistence
- Retrieve single pods
- Delete pods
- Multi-pod stress test (10 pods, 30 operations)

✅ **Deployment Operations**
- Create deployments
- List deployments
- Validate specs and replicas
- Label selectors

✅ **Cluster Health**
- Health check endpoint
- Node availability
- API response times (<20ms)
- Namespace isolation
- Error handling

✅ **Stability**
- 30+ operations without crash
- Concurrent pod operations
- Namespace isolation
- JSON error handling
- Missing field handling

### Pass Criteria

All tests must show:
```
✓ PASS: <test description>
```

No failures (✗) allowed for production readiness.

## Performance Targets

| Metric | Target | Achieved |
|--------|--------|----------|
| Pod Create | <1s | ✅ |
| API Response | <5s | ✅ 11-17ms |
| Stress Ops | 30+ no crash | ✅ |
| Pod Persist | 100% | ✅ |

## File Locations

- Unit tests: `tests/unit/test_*.c`
- Integration tests: `tests/integration/test_*.sh`
- E2E tests: `tests/e2e/test_*.sh`
- Test runner: `tests/run_all_tests.sh`
- Full docs: `TESTING.md`
- Test results: `TEST_RESULTS.md`

## Next Test Run

```bash
# Full suite
cd sirah && bash tests/run_all_tests.sh

# If any tests fail, check:
# 1. Is cluster running? curl http://localhost:6443/healthz
# 2. Are ports free? netstat -tuln | grep 6443
# 3. Check logs: cat /tmp/apiserver.log
```

## Success Indicators

After running `bash tests/run_all_tests.sh`, you should see:

✅ Phase 1: Unit Tests - 2 passed
✅ Phase 2: Integration Tests - 2 passed  
✅ Phase 3: E2E Tests - 1 passed
✅ Test Summary - All passed!

🎉 **Cluster is stable and production-ready!**
