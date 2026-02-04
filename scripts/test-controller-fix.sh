#!/bin/bash
set -e

echo "========== TESTING POD CONTROLLER FIX =========="
echo ""

# Kill existing processes
bash -c 'pkill -f "sirah-apiserver" || true; pkill -f "sirah-controller" || true; pkill -f etcd || true'
sleep 2

cd "$(dirname "$0")"

# Start API Server
echo "[1] Starting API Server..."
./bin/sirah-apiserver > /tmp/apiserver.log 2>&1 &
APISERVER_PID=$!
sleep 2

if ! ps -p $APISERVER_PID > /dev/null; then
    echo "ERROR: API Server failed to start"
    cat /tmp/apiserver.log
    exit 1
fi
echo "  ✓ API Server running (PID: $APISERVER_PID)"

# Start Pod Controller (with infinite loop - FIXED)
echo "[2] Starting Pod Controller (INFINITE LOOP - FIXED)..."
./bin/sirah-controller > /tmp/controller.log 2>&1 &
CONTROLLER_PID=$!
sleep 3

if ! ps -p $CONTROLLER_PID > /dev/null; then
    echo "ERROR: Pod Controller failed to start"
    cat /tmp/controller.log
    exit 1
fi
echo "  ✓ Pod Controller running (PID: $CONTROLLER_PID)"

# Wait a moment for controller to start polling
sleep 3

# Create test pod
echo ""
echo "[3] Creating test pod..."
curl -s -X POST http://localhost:6443/api/v1/namespaces/default/pods \
    -H "Content-Type: application/json" \
    -d '{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "test-myuni",
    "namespace": "default"
  },
  "spec": {
    "containers": [
      {
        "name": "app",
        "image": "/tmp/sirah-unikernels/test-kernel",
        "resources": {
          "requests": {
            "memory": "256Mi",
            "cpu": "1"
          }
        }
      }
    ]
  }
}' > /dev/null

echo "  ✓ Pod created"

# Wait for controller to discover and spawn QEMU
echo ""
echo "[4] Waiting 15 seconds for controller to discover and spawn QEMU..."
sleep 15

# Check for QEMU processes
echo ""
echo "========== RESULTS =========="
echo ""
echo "Running QEMU processes:"
QEMU_COUNT=$(pgrep -f "qemu-system" 2>/dev/null | wc -l || echo "0")
if [ "$QEMU_COUNT" -gt 0 ]; then
    pgrep -a -f "qemu-system"
    echo ""
    echo "✅ SUCCESS! $QEMU_COUNT QEMU process(es) spawned!"
else
    echo "  (none)"
    echo "❌ QEMU processes not spawned"
fi

echo ""
echo "========== CONTROLLER LOGS (Last 50 lines) =========="
echo ""
tail -50 /tmp/controller.log

echo ""
echo "[5] Controller is still running (infinite loop):"
if ps -p $CONTROLLER_PID > /dev/null; then
    echo "  ✓ Controller PID $CONTROLLER_PID still active"
else
    echo "  ✗ Controller has stopped (PID $CONTROLLER_PID not found)"
fi

echo ""
echo "Cleanup: Kill controller with: kill $CONTROLLER_PID"
echo "         Kill API server with: kill $APISERVER_PID"
