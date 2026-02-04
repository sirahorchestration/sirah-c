#!/bin/bash
set -e
cd /mnt/c/projects/k8s_unikernels/sirah

pkill -9 sirah-apiserver 2>/dev/null || true
sleep 1

./bin/sirah-apiserver >/tmp/server.log 2>&1 &
SERVER_PID=$!
sleep 2

# Create pod
curl -s -X POST \
  -H "Content-Type: application/json" \
  -u admin:admin \
  -d '{
    "apiVersion": "v1",
    "kind": "Pod",
    "metadata": {
      "name": "test-pod",
      "namespace": "default"
    },
    "spec": {
      "containers": [
        {
          "name": "app",
          "image": "test:latest"
        }
      ]
    }
  }' \
  http://localhost:6443/api/v1/namespaces/default/pods > /tmp/create-response.json

echo "Pod creation response:"
cat /tmp/create-response.json | python3 -m json.tool

echo ""
echo "Getting pods..."
curl -s http://localhost:6443/api/v1/namespaces/default/pods > /tmp/pods-response.json

echo "Pods response:"
cat /tmp/pods-response.json | python3 -m json.tool

pkill -9 sirah-apiserver 2>/dev/null || true
