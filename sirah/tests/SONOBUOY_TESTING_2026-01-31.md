# Sonobuoy Conformance Testing - January 31, 2026

## Overview

Sonobuoy is a tool for running Kubernetes conformance tests to verify that your cluster meets the official Kubernetes standards. For the Sirah unikernel-based Kubernetes implementation, we've installed and configured Sonobuoy v0.57.1.

## Installation Status

✅ **Sonobuoy v0.57.1 installed** at `./sonobuoy/sonobuoy`

The binary has been downloaded and extracted from:
```
https://github.com/vmware-tanzu/sonobuoy/releases/download/v0.57.1/sonobuoy_0.57.1_linux_amd64.tar.gz
```

## Available Test Modes

Sonobuoy supports the following conformance test modes:

| Mode | Duration | Purpose |
|------|----------|---------|
| `conformance-lite` | ~10-15 min | Quick lightweight conformance tests |
| `quick` | ~30-45 min | Faster conformance test suite |
| `non-disruptive-conformance` | ~1-2 hours | Non-disruptive test mode (default) |
| `certified-conformance` | ~4-8 hours | Full official Kubernetes certification tests |

## Basic Commands

### Installation

```bash
cd /mnt/c/projects/k8s_unikernels/sirah
./setup-sonobuoy.sh install
```

### Run Lite Tests (Recommended First Test)

```bash
./sonobuoy/sonobuoy run --mode=conformance-lite --wait
```

This runs the quick lightweight conformance suite and waits for completion.

### Run Quick Tests

```bash
./sonobuoy/sonobuoy run --mode=quick --wait
```

Faster test suite, takes 30-45 minutes.

### Run Full Certification Tests

```bash
./sonobuoy/sonobuoy run --mode=certified-conformance --wait
```

**Warning:** This takes 4-8 hours to complete. Run in a detached terminal or screen session.

### Check Test Status

```bash
./sonobuoy/sonobuoy status
```

Shows current status of running Sonobuoy tests, including:
- Current progress
- Running plugins
- Number of pods
- Completion percentage

### Retrieve Test Results

```bash
./sonobuoy/sonobuoy retrieve
```

Downloads the latest test results tarball to the current directory.

### Analyze Results

```bash
./sonobuoy/sonobuoy results <tarball-name>.tar.gz
```

Displays a summary of test results including:
- Passed tests
- Failed tests
- Skipped tests
- Overall conformance status

### Clean Up Resources

```bash
./sonobuoy/sonobuoy delete --wait
```

Removes all Sonobuoy resources from the cluster including:
- Test pods
- Sonobuoy namespace
- Results and logs

## Current Issues with Sirah

### Version Endpoint Issue

**Status:** Partially Resolved

Sonobuoy requires the `/api/v1/version` endpoint to retrieve cluster version information. This endpoint has been implemented in [internal/apiserver/handler.c](../internal/apiserver/handler.c) to return:

```json
{
  "major": "1",
  "minor": "28",
  "gitVersion": "v1.28.0-sirah",
  "gitCommit": "sirah-custom-build",
  "gitTreeState": "clean",
  "buildDate": "2026-01-31T00:00:00Z",
  "goVersion": "go1.21",
  "compiler": "gc",
  "platform": "linux/amd64"
}
```

**Implementation Details:**
- Added to `internal/apiserver/handler.c` after the `/api` discovery endpoints
- Returns v1.28-compatible version information
- Uses JSON-C library for response formatting

**Rebuild Required:**
```bash
cd /mnt/c/projects/k8s_unikernels/sirah
make clean
make
```

## Running Tests with Custom Scripts

### Via setup-sonobuoy.sh

The `setup-sonobuoy.sh` wrapper script provides convenience commands:

```bash
# Install Sonobuoy
./setup-sonobuoy.sh install

# Run tests with automatic result analysis
./setup-sonobuoy.sh lite
./setup-sonobuoy.sh quick
./setup-sonobuoy.sh full

# Analyze existing results
./setup-sonobuoy.sh analyze

# Check cluster and Sonobuoy status
./setup-sonobuoy.sh status

# Clean up
./setup-sonobuoy.sh cleanup
```

## Test Results Location

Test results are saved in:
```
./conformance-results/
```

Results are stored as compressed tarballs:
```
sonobuoy_<timestamp>.tar.gz
```

Extract with:
```bash
tar -xzf sonobuoy_<timestamp>.tar.gz
```

## Troubleshooting

### Error: "the server could not find the requested resource"

**Cause:** Cluster is not fully responsive or version endpoint not implemented.

**Solution:**
1. Verify cluster is running: `kubectl cluster-info`
2. Check node status: `kubectl get nodes`
3. Ensure `/api/v1/version` endpoint is implemented
4. Rebuild API server if changes were made

### Error: "unknown mode lite"

**Cause:** Using incorrect mode name.

**Solution:** Use full mode names:
- ✅ `--mode=conformance-lite`
- ❌ `--mode=lite`

### Sonobuoy pods not starting

**Solution:**
1. Check pod status: `kubectl get pods -A`
2. View pod logs: `kubectl logs -n sonobuoy <pod-name>`
3. Verify RBAC permissions if enabled
4. Ensure sufficient cluster resources

## Expected Behavior

### Successful Run Output

```
status: complete
progress: {
  "name": "Sonobuoy",
  "items": [...],
  "completionTime": "2026-01-31T..."
}
results: {
  "passed": 46,
  "failed": 0,
  "skipped": 5
}
```

### Result Files in Tarball

```
sonobuoy/
├── plugins/
│   ├── e2e/
│   │   └── results.txt
│   └── systemd-logs/
│       └── results.txt
├── podlogs/
│   └── [pod logs]
└── hosts/
    └── [host information]
```

## Performance Notes

- **Lite tests** are suitable for quick validation
- **Quick tests** provide good coverage in reasonable time
- **Full certification** should be run on production-quality infrastructure
- Results may vary based on Sirah implementation completeness

## Documentation References

- [Official Sonobuoy Documentation](https://sonobuoy.io)
- [Kubernetes Conformance Testing](https://github.com/kubernetes/kubernetes/blob/master/hack/conformance-testing.md)
- [Sirah API Implementation](../internal/apiserver/handler.c)
- [Sirah Setup Script](../setup-sonobuoy.sh)

## Next Steps

1. **Verify API server rebuild** with version endpoint implemented
2. **Start simple test run** with `--mode=conformance-lite`
3. **Monitor progress** with `sonobuoy status`
4. **Retrieve results** when complete
5. **Analyze output** to identify any failing tests

---

**Last Updated:** January 31, 2026
**Sonobuoy Version:** 0.57.1
**Sirah Version:** Custom implementation
**Test Location:** `/mnt/c/projects/k8s_unikernels/sirah`
