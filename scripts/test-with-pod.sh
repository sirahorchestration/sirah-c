#!/bin/bash
# Test script with pod creation

API_URL="http://localhost:6443"
NAMESPACE="${1:-default}"

cd /mnt/c/projects/k8s_unikernels/sirah

pkill -9 sirah-apiserver 2>/dev/null
sleep 1

./bin/sirah-apiserver >/tmp/test.log 2>&1 &
SERVER_PID=$!
sleep 2

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
        "image": "mirageos/http-server:latest"
      }
    ]
  }
}'

RESPONSE=$(curl -s -X POST \
  -H "Content-Type: application/json" \
  -u admin:admin \
  -d "$POD_JSON" \
  "$API_URL/api/v1/namespaces/default/pods")

echo "Pod created: $(echo $RESPONSE | python3 -c 'import sys, json; d=json.load(sys.stdin); print(d.get(\"metadata\", {}).get(\"name\", \"unknown\"))')"
echo ""

echo "=== Getting pods ==="
PODS=$(curl -s -X GET "$API_URL/api/v1/namespaces/$NAMESPACE/pods")
echo "pods response length: ${#PODS}"
echo "pods (first 100): ${PODS:0:100}..."
echo ""

echo "=== Counting pods ==="
POD_COUNT=$(echo "$PODS" | python3 -c "import sys, json; print(len(json.load(sys.stdin).get('items', [])))" 2>&1)
echo "POD_COUNT: $POD_COUNT"
echo ""

echo "=== Is server still running? ==="
ps -p $SERVER_PID > /dev/null && echo "YES" || echo "NO"

kill -9 $SERVER_PID 2>/dev/null
