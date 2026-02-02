# QEMU Scheduling Tests - Quick Reference

## One-Liner Commands

### Run All Tests (Including QEMU)
```bash
cd sirah && bash tests/run_all_tests.sh
```

### Run Only QEMU Unit Tests
```bash
cd sirah && gcc -std=c99 tests/unit/test_qemu_pod_scheduling.c -ljson-c -o /tmp/test_qemu && /tmp/test_qemu
```

### Run Only QEMU Integration Tests
```bash
cd sirah && bash tests/integration/test_qemu_scheduling.sh
```

### Run Only QEMU E2E Tests
```bash
cd sirah && bash tests/e2e/test_qemu_pod_scheduling.sh
```

### Run Everything with Cluster Startup
```bash
cd sirah && bash start-cluster-full.sh && sleep 3 && bash tests/run_all_tests.sh
```

---

## Using Make

```bash
make test              # All tests (unit + integration + e2e)
make test-unit        # Unit tests only
make test-integration # Integration tests only  
make test-e2e         # E2E tests only
```

---

## What Gets Tested

### Unit Tests (No cluster needed)
- Pod scheduling JSON parsing
- Scheduling status validation
- QEMU resource requirements
- Node selection logic
- Pod affinity constraints
- QEMU scheduling metadata
- Scheduler node assignment

### Integration Tests (Requires running cluster)
- Pod creation with QEMU
- Pod scheduled to node
- QEMU runtime metadata
- Concurrent pod scheduling (3 pods)
- Pod status transitions
- QEMU resource allocation
- Pod network configuration
- Node affinity constraints
- Cleanup test pods

### E2E Tests (Full cluster required)
- Cluster health verification
- QEMU node availability
- Single pod scheduling
- Pod scheduling verification
- Status monitoring
- Concurrent stress test (5 pods)
- Pod with resource limits
- Scheduling performance measurement
- Namespace isolation
- Cleanup test pods

---

## Expected Results

### All Tests Pass ✅
```
==========================================
Sirah Kubernetes E2E Test Suite
==========================================

Phase 1: Running Unit Tests
✓ PASSED: test_qemu_pod_scheduling (30+ assertions)
[other unit tests...]

Phase 2: Running Integration Tests
✓ PASSED: test_qemu_scheduling.sh (10 scenarios)
[other integration tests...]

Phase 3: Running End-to-End Tests
✓ PASSED: test_qemu_pod_scheduling.sh (10 tests)
[other e2e tests...]

==========================================
Test Summary
==========================================
Total Tests: 5+
Passed: 5+ ✅
Failed: 0

All tests passed! ✓
```

---

## Key Metrics

| Metric | Expected |
|--------|----------|
| Pod Scheduling Time | <500ms |
| API Response Time | <20ms |
| Concurrent Pods | 5+ ✅ |
| Success Rate | 100% ✅ |

---

## Troubleshooting

### "Cluster is not running"
**Fix**: Start cluster first
```bash
bash start-cluster-full.sh
```

### "Pod not scheduled"
**Check**:
```bash
ps aux | grep sirah-scheduler      # Is scheduler running?
curl http://localhost:6443/api/v1/nodes  # Are nodes available?
```

### "Node not found"
**Check**:
```bash
ps aux | grep sirah-kubelet        # Is kubelet running?
tail /tmp/sirah-logs/kubelet.log   # Check kubelet logs
```

### "Compilation failed"
**Install json-c**:
```bash
apt-get install libjson-c-dev      # Ubuntu/Debian
brew install json-c                 # macOS
```

---

## Test Files

| File | Type | Tests |
|------|------|-------|
| `tests/unit/test_qemu_pod_scheduling.c` | C | 7 |
| `tests/integration/test_qemu_scheduling.sh` | Bash | 10 |
| `tests/e2e/test_qemu_pod_scheduling.sh` | Bash | 10 |

Total: **27+ tests validating QEMU pod scheduling**

---

## Documentation

- **[QEMU_SCHEDULING_TESTS.md](QEMU_SCHEDULING_TESTS.md)** - Comprehensive guide
- **[TEST_QUICK_REFERENCE.md](TEST_QUICK_REFERENCE.md)** - General test reference
- **[TESTING.md](TESTING.md)** - Complete testing documentation

---

## Next Steps

✅ Tests are ready to run  
✅ All three test phases (unit, integration, e2e) included  
✅ QEMU-specific scenarios validated  
✅ Performance metrics included  

**Ready for production validation!**
