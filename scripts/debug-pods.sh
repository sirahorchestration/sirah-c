#!/bin/bash
# Debug pods endpoint

cd /mnt/c/projects/k8s_unikernels/sirah

pkill -9 sirah-apiserver 2>/dev/null
sleep 1

./bin/sirah-apiserver >/tmp/test.log 2>&1 &
SERVER_PID=$!
sleep 2

echo "=== Create pod ==="
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
  http://localhost:6443/api/v1/namespaces/default/pods | python3 -m json.tool
echo ""

echo "=== List pods (with verbose curl) ==="
curl -v http://localhost:6443/api/v1/namespaces/default/pods 2>&1 | head -50
echo ""

echo "=== List pods (simple) ==="
RESULT=$(curl -s http://localhost:6443/api/v1/namespaces/default/pods)
echo "Raw result length: ${#RESULT}"
echo "Raw result: '$RESULT'"
echo ""

echo "=== Is server still running? ==="
ps -p $SERVER_PID > /dev/null && echo "YES" || echo "NO"

echo ""
echo "=== Server log ==="
cat /tmp/test.log

kill -9 $SERVER_PID 2>/dev/null
