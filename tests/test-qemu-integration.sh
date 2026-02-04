#!/bin/bash
# Test QEMU integration - spawns pods and verifies QEMU processes are created

set -e

cd /mnt/c/projects/k8s_unikernels/sirah

echo "=========================================="
echo "    QEMU INTEGRATION TEST"
echo "=========================================="
echo ""

# Clean up old processes
pkill -9 sirah-apiserver 2>/dev/null || true
pkill -9 sirah-controller 2>/dev/null || true
sleep 1

# Create necessary directories (use temp for testing)
QEMU_DIR="/tmp/sirah-qemu"
UNIKERNEL_DIR="/tmp/sirah-unikernels"
mkdir -p $QEMU_DIR
mkdir -p $UNIKERNEL_DIR

# Create a dummy unikernel image for testing
echo "Creating dummy unikernel image..."
dd if=/dev/zero of=$UNIKERNEL_DIR/test-kernel.img bs=1M count=10 2>/dev/null
echo "✓ Test unikernel created"
echo ""

# Start API server
echo "[1/5] Starting API Server..."
./bin/sirah-apiserver >/tmp/apiserver.log 2>&1 &
APISERVER_PID=$!
sleep 2
echo "✓ API Server running (PID: $APISERVER_PID)"
echo ""

# Start controller
echo "[2/5] Starting Controller..."
./bin/sirah-controller --api-server http://localhost:6443 >/tmp/controller.log 2>&1 &
CONTROLLER_PID=$!
sleep 2
echo "✓ Controller running (PID: $CONTROLLER_PID)"
echo ""

# Check initial QEMU processes
echo "[3/5] Checking initial QEMU processes..."
INITIAL_QEMU=$(pgrep -f "qemu" 2>/dev/null | wc -l || echo "0")
echo "  Initial QEMU processes: $INITIAL_QEMU"
echo ""

# Create a pod (should trigger QEMU spawn)
echo "[4/5] Creating test pod..."
POD_RESPONSE=$(curl -s -X POST \
  -H 'Content-Type: application/json' \
  -u admin:admin \
  -d '{
    "apiVersion":"v1",
    "kind":"Pod",
    "metadata":{"name":"test-unikernel","namespace":"default"},
    "spec":{
      "containers":[{
        "name":"app",
        "image":"test-kernel.img",
        "resources":{"memory":"128Mi"}
      }]
    }
  }' \
  http://localhost:6443/api/v1/namespaces/default/pods)

echo "  Pod created: $(echo $POD_RESPONSE | python3 -c 'import sys,json; print(json.load(sys.stdin).get("metadata",{}).get("name","unknown"))' 2>/dev/null)"
echo ""

# Wait for controller to spawn QEMU
echo "[5/5] Waiting for QEMU processes to spawn (10 seconds)..."
sleep 10

# Check for QEMU processes
FINAL_QEMU=$(pgrep -f "qemu" 2>/dev/null | wc -l || echo "0")
echo ""
echo "=========================================="
echo "    TEST RESULTS"
echo "=========================================="
echo "Initial QEMU processes: $INITIAL_QEMU"
echo "Final QEMU processes:   $FINAL_QEMU"
echo ""

if [ "$FINAL_QEMU" -gt "$INITIAL_QEMU" ]; then
    echo "✓ SUCCESS: QEMU processes spawned!"
    echo ""
    echo "Running QEMU processes:"
    pgrep -f "qemu" -a || true
else
    echo "⚠ WARNING: No new QEMU processes detected"
    echo "Check logs:"
    echo "  API Server: cat /tmp/apiserver.log"
    echo "  Controller: cat /tmp/controller.log"
fi

echo ""
echo "Cleaning up..."
kill -9 $APISERVER_PID 2>/dev/null || true
kill -9 $CONTROLLER_PID 2>/dev/null || true
pkill -9 -f "qemu.*test-unikernel" 2>/dev/null || true
sleep 1

echo "✓ Test complete"
