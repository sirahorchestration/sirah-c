#!/bin/bash
set -e
cd /mnt/c/projects/k8s_unikernels/sirah

pkill -9 sirah-apiserver 2>/dev/null || true
sleep 1

./bin/sirah-apiserver >/tmp/server.log 2>&1 &
sleep 2

# Create pod
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

API_URL="http://localhost:6443"
NAMESPACE="default"

echo "=== Get pods ==="
PODS=$(curl -s -X GET "$API_URL/api/v1/namespaces/$NAMESPACE/pods")
echo "PODS length: ${#PODS}"
echo "PODS: $PODS"
echo ""

echo "=== Count pods WITHOUT suppressing errors ==="
POD_COUNT=$(echo "$PODS" | python3 -c "import sys, json; print(len(json.load(sys.stdin).get('items', [])))" 2>&1)
echo "POD_COUNT: $POD_COUNT"
echo ""

echo "=== Test if POD_COUNT is 0 ==="
if [ "$POD_COUNT" -eq 0 ]; then
    echo "Pod count is 0"
else
    echo "Pod count is NOT 0, value: $POD_COUNT"
fi

pkill -9 sirah-apiserver 2>/dev/null || true
