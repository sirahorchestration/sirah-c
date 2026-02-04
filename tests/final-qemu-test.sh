#!/bin/bash
set -e

echo "========== FULL QEMU INTEGRATION TEST =========="
echo ""

# Cleanup
pkill -9 sirah-apiserver 2>/dev/null || true
pkill -9 sirah-controller 2>/dev/null || true
pkill -9 qemu 2>/dev/null || true
sleep 1

# Setup
mkdir -p /tmp/sirah-unikernels
dd if=/dev/zero of=/tmp/sirah-unikernels/test-kernel bs=1M count=10 2>/dev/null

# Start API Server
/mnt/c/projects/k8s_unikernels/sirah/bin/sirah-apiserver > /tmp/api.log 2>&1 &
APIPID=$!
sleep 2
echo "[✓] API Server started (PID: $APIPID)"

# Start Pod Controller
/mnt/c/projects/k8s_unikernels/sirah/bin/sirah-controller -apiserver http://localhost:6443 2>&1 | tee /tmp/ctrl.log &
CTRLPID=$!
sleep 3
echo "[✓] Pod Controller started (PID: $CTRLPID)"

# Create test pod
echo "[*] Creating test pod..."
cat > /tmp/testpod.json << 'EOFPOD'
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "unikernel-test",
    "namespace": "default"
  },
  "spec": {
    "containers": [
      {
        "name": "kernel",
        "image": "/tmp/sirah-unikernels/test-kernel"
      }
    ]
  }
}
EOFPOD

curl -s -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H "Content-Type: application/json" \
  -d @/tmp/testpod.json > /dev/null

echo "[✓] Pod created"

# Wait for QEMU to spawn
echo "[*] Waiting 8 seconds for controller to discover and spawn QEMU..."
sleep 8

echo ""
echo "========== RESULTS =========="
echo ""
echo "Controller output (last 30 lines):"
tail -30 /tmp/ctrl.log | sed 's/^/  /'
echo ""
echo "QEMU processes running:"
if pgrep -f qemu-system >/dev/null 2>&1; then
    pgrep -a qemu-system | sed 's/^/  /'
    echo ""
    echo "✅ SUCCESS! QEMU process spawned!"
    QEMU_COUNT=$(pgrep -c qemu-system)
    echo "Total QEMU processes: $QEMU_COUNT"
else
    echo "  (none)"
    echo ""
    echo "❌ QEMU not running - check logs below"
fi

echo ""
echo "Controller logs:"
cat /tmp/ctrl.log | grep -E "FETCH|SYNC|Pod" | sed 's/^/  /'

# Cleanup
echo ""
echo "[*] Cleaning up..."
kill $APIPID $CTRLPID 2>/dev/null || true
pkill -9 qemu-system 2>/dev/null || true
sleep 1
echo "Done!"
