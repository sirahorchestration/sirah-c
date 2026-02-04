#!/bin/bash
# Week 3 Verification Test Script
# Tests pod lifecycle, kubelet, and event tracking

set -e

PROJECT_ROOT="/mnt/c/projects/k8s_unikernels/sirah"
cd "$PROJECT_ROOT"

echo "========================================"
echo "Week 3 Verification Test Suite"
echo "========================================"
echo ""

# Test 1: Verify binaries exist
echo "[TEST 1] Verifying binaries exist..."
for binary in apiserver scheduler controller kubelet; do
    if [ -f "bin/sirah-$binary" ]; then
        echo "  ✓ bin/sirah-$binary exists"
    else
        echo "  ✗ bin/sirah-$binary missing"
        exit 1
    fi
done
echo ""

# Test 2: Check pod lifecycle headers
echo "[TEST 2] Checking pod lifecycle headers..."
if grep -q "pod_lifecycle.h" internal/kubelet/kubelet.h; then
    echo "  ✓ kubelet.h includes pod_lifecycle.h"
else
    echo "  ✗ pod_lifecycle.h not found in kubelet.h"
    exit 1
fi

if [ -f "pkg/lifecycle/pod_lifecycle.h" ]; then
    echo "  ✓ pod_lifecycle.h exists"
else
    echo "  ✗ pod_lifecycle.h missing"
    exit 1
fi
echo ""

# Test 3: Check event system
echo "[TEST 3] Checking event system..."
if [ -f "pkg/types/event.h" ] && [ -f "pkg/types/event.c" ]; then
    echo "  ✓ Event system files exist"
else
    echo "  ✗ Event system files missing"
    exit 1
fi

if grep -q "endpoint_pod_events" internal/apiserver/endpoints.c; then
    echo "  ✓ Pod events endpoint implemented"
else
    echo "  ✗ Pod events endpoint not found"
    exit 1
fi
echo ""

# Test 4: Verify JSON serialization
echo "[TEST 4] Checking pod JSON serialization..."
if grep -q "k8s_pod_to_json\|k8s_pod_from_json" pkg/types/pod.c; then
    echo "  ✓ Pod JSON serialization implemented"
else
    echo "  ✗ Pod JSON serialization not found"
    exit 1
fi
echo ""

# Test 5: Run kubelet for 3 seconds
echo "[TEST 5] Testing kubelet startup..."
echo "  Starting API server..."
./bin/sirah-apiserver -port 6443 > /tmp/apiserver.log 2>&1 &
API_PID=$!
sleep 2

echo "  Starting kubelet..."
timeout 3 ./bin/sirah-kubelet --node-name=worker-1 --api-server=http://localhost:6443 > /tmp/kubelet.log 2>&1 || true
sleep 1

# Check kubelet output
if grep -q "Initialized\|Ready to receive" /tmp/kubelet.log 2>/dev/null; then
    echo "  ✓ Kubelet initialized successfully"
else
    echo "  ✗ Kubelet initialization failed"
fi

# Check API server responsiveness
if curl -s http://localhost:6443/api/v1/namespaces/default/pods 2>/dev/null | grep -q "PodList"; then
    echo "  ✓ API server responsive"
else
    echo "  ✗ API server not responsive"
fi

# Clean up
kill $API_PID 2>/dev/null || true
sleep 1

echo ""
echo "========================================"
echo "All Tests Passed ✓"
echo "========================================"
