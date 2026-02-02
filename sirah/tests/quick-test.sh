#!/bin/bash
set -e

# Cleanup
pkill -9 qemu-system-x86_64 2>/dev/null || true
pkill -9 sirah-apiserver 2>/dev/null || true
pkill -9 sirah-controller 2>/dev/null || true
sleep 1

echo "Starting API Server..."
/mnt/c/projects/k8s_unikernels/sirah/bin/sirah-apiserver > /tmp/apiserver.log 2>&1 &
APIPID=$!
sleep 2

echo "Starting Controller..."
/mnt/c/projects/k8s_unikernels/sirah/bin/sirah-controller -apiserver http://localhost:6443 > /tmp/controller.log 2>&1 &
CTRLPID=$!
sleep 3

# Create test image
mkdir -p /tmp/sirah-unikernels
dd if=/dev/zero of=/tmp/sirah-unikernels/test-kernel bs=1M count=10 2>/dev/null

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

echo "Pod created!"
sleep 3

echo ""
echo "========== CONTROLLER LOGS =========="
cat /tmp/controller.log
echo ""
echo "========== API SERVER LOGS =========="
tail -20 /tmp/apiserver.log
echo ""
echo "========== QEMU PROCESSES =========="
pgrep -a qemu-system-x86_64 || echo "No QEMU processes running"

# Cleanup
kill $APIPID $CTRLPID 2>/dev/null || true
pkill -9 qemu-system-x86_64 2>/dev/null || true
