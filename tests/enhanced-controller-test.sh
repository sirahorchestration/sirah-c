#!/bin/bash
set -e

echo "========== ENHANCED CONTROLLER TEST =========="
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

# Test 1: Pod with resource limits
echo ""
echo "[TEST 1] Pod with resource limits (256Mi memory, 2 CPUs)"
cat > /tmp/pod-resources.json << 'EOFPOD'
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "unikernel-with-resources",
    "namespace": "default"
  },
  "spec": {
    "containers": [
      {
        "name": "kernel",
        "image": "/tmp/sirah-unikernels/test-kernel",
        "resources": {
          "limits": {
            "memory": "256Mi",
            "cpu": "2"
          }
        }
      }
    ]
  }
}
EOFPOD

curl -s -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H "Content-Type: application/json" \
  -d @/tmp/pod-resources.json > /dev/null

echo "[*] Created pod with resource limits"

# Test 2: Pod with different image name
echo ""
echo "[TEST 2] Pod with unikernel image"
cat > /tmp/pod-unikernel.json << 'EOFPOD2'
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "mirage-unikernel",
    "namespace": "default"
  },
  "spec": {
    "containers": [
      {
        "name": "app",
        "image": "/opt/unikernels/mirage-app.img",
        "resources": {
          "limits": {
            "memory": "128Mi",
            "cpu": "1"
          }
        }
      }
    ]
  }
}
EOFPOD2

curl -s -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H "Content-Type: application/json" \
  -d @/tmp/pod-unikernel.json > /dev/null

echo "[*] Created pod with unikernel image"

# Wait for discovery and spawning
echo ""
echo "[*] Waiting 12 seconds for controller to discover and spawn VMs..."
sleep 12

echo ""
echo "========== RESULTS =========="
echo ""
echo "Running QEMU processes:"
QEMU_COUNT=$(pgrep -c qemu-system-x86_64 2>/dev/null || echo 0)
if [ $QEMU_COUNT -gt 0 ]; then
    pgrep -a qemu-system-x86_64 | while read line; do
        echo "  $line"
    done
    echo ""
    echo "✅ SUCCESS! $QEMU_COUNT QEMU processes spawned!"
else
    echo "  (none)"
    echo "❌ QEMU processes not spawned"
fi

echo ""
echo "========== CONTROLLER LOG HIGHLIGHTS =========="
echo ""
grep -E "FETCH.*resources|cpu=|memory=|SYNC.*spawning|is_unikernel|Update.*status" /tmp/ctrl.log | tail -20 || echo "No highlights found"

echo ""
echo "[*] Cleaning up..."
kill $APIPID $CTRLPID 2>/dev/null || true
pkill -9 qemu-system 2>/dev/null || true
sleep 1
echo "Done!"
