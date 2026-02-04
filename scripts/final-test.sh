#!/bin/bash
set -e
cd /mnt/c/projects/k8s_unikernels/sirah

pkill -9 sirah-apiserver 2>/dev/null || true
sleep 1

echo "=== Starting API Server ==="
./bin/sirah-apiserver >/tmp/server.log 2>&1 &
SERVER_PID=$!
sleep 2

echo "=== Creating a test pod ==="
curl -s -X POST \
  -H "Content-Type: application/json" \
  -u admin:admin \
  -d '{
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
          "image": "mirageos/http-server:latest"
        }
      ]
    }
  }' \
  http://localhost:6443/api/v1/namespaces/default/pods >/dev/null

sleep 1

echo ""
echo "=== Running monitoring script ==="
bash scripts/view-qemu-pods.sh

pkill -9 sirah-apiserver 2>/dev/null || true
