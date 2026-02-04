#!/bin/bash
# Full end-to-end test

cd /mnt/c/projects/k8s_unikernels/sirah

pkill -9 sirah-apiserver 2>/dev/null
sleep 1

echo "=== Starting API Server ==="
./bin/sirah-apiserver >/tmp/test.log 2>&1 &
SERVER_PID=$!
sleep 2

echo "Server PID: $SERVER_PID"
echo ""

# Test monitoring script first (no pods)
echo "=== Running monitoring script (no pods) ==="
bash scripts/view-qemu-pods.sh
echo ""

# Create a test pod
echo "=== Creating a test pod ==="
POD_JSON='{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "test-unikernel",
    "namespace": "default"
  },
  "spec": {
    "containers": [
      {
        "name": "app",
        "image": "mirageos/http-server:latest",
        "imagePullPolicy": "IfNotPresent",
        "ports": [{"containerPort": 8080}]
      }
    ]
  }
}'

RESPONSE=$(curl -s -X POST \
  -H "Content-Type: application/json" \
  -u admin:admin \
  -d "$POD_JSON" \
  http://localhost:6443/api/v1/namespaces/default/pods)

echo "Response: ${RESPONSE:0:200}..."
echo ""

# Run monitoring script again (with pod)
echo "=== Running monitoring script (with pod) ==="
bash scripts/view-qemu-pods.sh
echo ""

# Cleanup
echo "=== Cleanup ==="
kill -9 $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null
echo "Done!"
