#!/bin/bash
set -e

echo "========== QEMU SPAWNING TEST =========="
echo ""

cd "$(dirname "$0")/.."
truncate -s 0 /tmp/controller.log
truncate -s 0 /tmp/qemu-*.log
# Start API Server
echo "[*] Starting API Server..."
./bin/sirah-apiserver > /tmp/apiserver.log 2>&1 &
APISERVER_PID=$!
sleep 2

# Start Pod Controller with longer loop
echo "[*] Starting Pod Controller (30 iterations, 5-second intervals)..."
#timeout 160 
./bin/sirah-controller > /tmp/controller.log 2>&1 &
CONTROLLER_PID=$!
sleep 3

# Create test pods
echo "[*] Creating test pods..."
curl -s -X POST http://localhost:6443/api/v1/namespaces/default/pods \
    -H "Content-Type: application/json" \
    -d '{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "test-1",
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

# curl -s -X POST http://localhost:6443/api/v1/namespaces/default/pods \
#     -H "Content-Type: application/json" \
#     -d '{
#   "apiVersion": "v1",
#   "kind": "Pod",
#   "metadata": {
#     "name": "test-unikernel-2",
#     "namespace": "default"
#   },
#   "spec": {
#     "containers": [
#       {
#         "name": "app",
#         "image": "/opt/unikernels/mirage-app.img",
#         "resources": {
#           "requests": {
#             "memory": "128Mi",
#             "cpu": "1"
#           }
#         }
#       }
#     ]
#   }
# }' > /dev/null

echo "[✓] Pods created"
echo ""

# Wait for spawning
echo "[*] Waiting 20 seconds for controller to discover and spawn VMs..."
sleep 20

# Check for QEMU processes
echo ""
echo "========== RESULTS =========="
echo ""
echo "Running QEMU processes:"
QEMU_PIDS=$(pgrep -f "qemu-system" 2>/dev/null || echo "")
if [ ! -z "$QEMU_PIDS" ]; then
    pgrep -a -f "qemu-system"
    echo ""
    COUNT=$(echo "$QEMU_PIDS" | wc -l)
    echo "✅ SUCCESS! $COUNT QEMU processes spawned!"
    
    echo ""
    echo "QEMU log files:"
    ls -lh /tmp/qemu-*.log 2>/dev/null | head -5
else
    echo "  (none)"
    echo "❌ QEMU processes not spawned"
fi

echo ""
echo "========== CONTROLLER LOGS =========="
echo ""
grep -E "\[POD|qemu\]" /tmp/controller.log | head -40

# Cleanup
#echo ""
#echo "[*] Cleaning up..."
#kill $APISERVER_PID 2>/dev/null || true
#kill $CONTROLLER_PID 2>/dev/null || true
#pkill -f qemu-system-x86_64 2>/dev/null || true
#wait 2>/dev/null || true

echo "Done!"
