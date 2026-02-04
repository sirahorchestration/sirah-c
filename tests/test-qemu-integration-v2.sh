#!/bin/bash
set -e

echo "=================================================="
echo "QEMU Integration Test - Version 2 (Clean)"
echo "=================================================="

SIRAH_DIR="/mnt/c/projects/k8s_unikernels/sirah"
API_PORT=6443
QEMU_DIR="/tmp/sirah-qemu"
UNIKERNEL_DIR="/tmp/sirah-unikernels"
TEST_IMAGE="$UNIKERNEL_DIR/test-kernel"

# Kill any existing QEMU processes from previous runs
echo "[1/7] Cleaning up old QEMU processes..."
pkill -9 qemu-system-x86_64 2>/dev/null || true
sleep 1
echo "  ✓ Cleaned up"

# Create directories
echo "[2/7] Creating directories..."
mkdir -p "$QEMU_DIR"
mkdir -p "$UNIKERNEL_DIR"
rm -f "$TEST_IMAGE"
echo "  ✓ Directories created"

# Create dummy unikernel image
echo "[3/7] Creating dummy unikernel image..."
dd if=/dev/zero of="$TEST_IMAGE" bs=1M count=10 2>/dev/null
echo "  ✓ Test kernel created: $TEST_IMAGE"

# Start API Server
echo "[4/7] Starting API Server..."
$SIRAH_DIR/bin/sirah-apiserver -port $API_PORT > /tmp/apiserver.log 2>&1 &
API_PID=$!
sleep 2
if ps -p $API_PID > /dev/null; then
    echo "  ✓ API Server running (PID: $API_PID)"
else
    echo "  ✗ API Server failed to start"
    cat /tmp/apiserver.log
    exit 1
fi

# Start Controller
echo "[5/7] Starting Controller..."
$SIRAH_DIR/bin/sirah-controller -apiserver "http://localhost:$API_PORT" > /tmp/controller.log 2>&1 &
CONTROLLER_PID=$!
sleep 3
if ps -p $CONTROLLER_PID > /dev/null; then
    echo "  ✓ Controller running (PID: $CONTROLLER_PID)"
    echo "  Controller logs:"
    head -20 /tmp/controller.log | sed 's/^/    /'
else
    echo "  ✗ Controller failed to start"
    cat /tmp/controller.log
    kill $API_PID 2>/dev/null || true
    exit 1
fi

# Count QEMU before
echo "[6/7] Checking initial QEMU processes..."
INITIAL_COUNT=$(pgrep -c qemu-system-x86_64 2>/dev/null || echo 0)
echo "  Initial QEMU processes: $INITIAL_COUNT"

# Create test pod
echo "[7/7] Creating test pod..."
POD_NAME="test-unikernel"
NAMESPACE="default"

curl -s -X POST "http://localhost:$API_PORT/api/v1/namespaces/$NAMESPACE/pods" \
  -H "Content-Type: application/json" \
  -d '{
    "apiVersion": "v1",
    "kind": "Pod",
    "metadata": {
      "name": "'$POD_NAME'",
      "namespace": "'$NAMESPACE'"
    },
    "spec": {
      "containers": [{
        "name": "unikernel",
        "image": "'$TEST_IMAGE'",
        "resources": {
          "limits": {
            "memory": "256Mi",
            "cpu": "1"
          }
        }
      }]
    }
  }' > /tmp/pod_response.json 2>&1

echo "  Pod created: $POD_NAME"
echo "  Response:"
cat /tmp/pod_response.json | sed 's/^/    /'

# Wait for QEMU to spawn
echo ""
echo "Waiting for QEMU processes to spawn (15 seconds)..."
for i in {1..15}; do
    CURRENT=$(pgrep -c qemu-system-x86_64 2>/dev/null || echo 0)
    NEW_COUNT=$((CURRENT - INITIAL_COUNT))
    echo "  [$i/15] Current QEMU processes: $CURRENT (new: +$NEW_COUNT)"
    sleep 1
done

# Final check
echo ""
echo "=========================================="
FINAL_COUNT=$(pgrep -c qemu-system-x86_64 2>/dev/null || echo 0)
NEW_PROCESSES=$((FINAL_COUNT - INITIAL_COUNT))

echo "Initial QEMU processes:   $INITIAL_COUNT"
echo "Final QEMU processes:     $FINAL_COUNT"
echo "New processes spawned:    +$NEW_PROCESSES"
echo "=========================================="

if [ $NEW_PROCESSES -gt 0 ]; then
    echo "✓ SUCCESS: QEMU processes spawned!"
    echo ""
    echo "Running QEMU processes:"
    pgrep -a qemu-system-x86_64 | sed 's/^/  /'
else
    echo "✗ FAILURE: No new QEMU processes spawned"
    echo ""
    echo "API Server logs:"
    tail -10 /tmp/apiserver.log | sed 's/^/  /'
    echo ""
    echo "Controller logs:"
    cat /tmp/controller.log | sed 's/^/  /'
fi

# Cleanup
echo ""
echo "Cleaning up..."
kill $API_PID $CONTROLLER_PID 2>/dev/null || true
pkill -9 qemu-system-x86_64 2>/dev/null || true
sleep 1

if [ $NEW_PROCESSES -gt 0 ]; then
    exit 0
else
    exit 1
fi
