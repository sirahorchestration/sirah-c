#!/bin/bash
set -e

# Cleanup
pkill -9 qemu-system-x86_64 2>/dev/null || true
pkill -9 sirah-apiserver 2>/dev/null || true
pkill -9 sirah-controller 2>/dev/null || true
sleep 1

mkdir -p /tmp/sirah-unikernels
dd if=/dev/zero of=/tmp/sirah-unikernels/test-kernel bs=1M count=10 2>/dev/null

echo "Starting API Server..."
/mnt/c/projects/k8s_unikernels/sirah/bin/sirah-apiserver > /tmp/apiserver.log 2>&1 &
APIPID=$!
sleep 2

echo "Starting Controller..."
/mnt/c/projects/k8s_unikernels/sirah/bin/sirah-controller -apiserver http://localhost:6443 2>&1 | tee /tmp/controller_live.log &
CTRLPID=$!
sleep 3

echo "Creating test pod..."
cat > /tmp/pod.json << 'EOF'
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "testpod",
    "namespace": "default"
  },
  "spec": {
    "containers": [
      {
        "name": "test",
        "image": "/tmp/sirah-unikernels/test-kernel"
      }
    ]
  }
}
EOF

curl -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H "Content-Type: application/json" \
  -d @/tmp/pod.json > /tmp/pod_resp.json 2>&1

echo "Pod created! Waiting 10 seconds for controller to process..."

# Wait 10 seconds for the sync loop to run (it's every 5 seconds)
for i in {1..10}; do
    echo "  Second $i..."
    sleep 1
done

echo ""
echo "========== FULL CONTROLLER OUTPUT =========="
cat /tmp/controller_live.log || cat /tmp/controller.log || echo "No controller log"
echo ""
echo "========== API SERVER LOGS (last 20 lines) =========="
tail -20 /tmp/apiserver.log || echo "No API logs"
echo ""
echo "========== QEMU PROCESSES =========="
pgrep qemu || echo "No QEMU processes running"

# Cleanup
kill $APIPID $CTRLPID 2>/dev/null || true
pkill -9 qemu-system-x86_64 2>/dev/null || true
