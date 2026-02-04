#!/bin/bash
# Debug version of view-qemu-pods.sh

API_URL="http://localhost:6443"
NAMESPACE="${1:-default}"

cd /mnt/c/projects/k8s_unikernels/sirah

pkill -9 sirah-apiserver 2>/dev/null
sleep 1

./bin/sirah-apiserver >/tmp/test.log 2>&1 &
SERVER_PID=$!
sleep 2

echo "=== Step 1: healthz check ==="
HEALTHZ=$(curl -s "$API_URL/healthz")
echo "healthz: $HEALTHZ"
if echo "$HEALTHZ" | grep -q "ok"; then
    echo "✓ Cluster is healthy"
else
    echo "✗ Cluster check failed"
fi
echo ""

echo "=== Step 2: get nodes ==="
NODES=$(curl -s -X GET "$API_URL/api/v1/nodes")
echo "nodes response length: ${#NODES}"
echo "nodes: ${NODES:0:100}..."
echo ""

echo "=== Step 3: count nodes ==="
NODE_COUNT=$(echo "$NODES" | python3 -c "import sys, json; print(len(json.load(sys.stdin).get('items', [])))" 2>&1)
echo "NODE_COUNT: $NODE_COUNT"
echo ""

echo "=== Step 4: get pods ==="
PODS=$(curl -s -X GET "$API_URL/api/v1/namespaces/$NAMESPACE/pods")
echo "pods response length: ${#PODS}"
echo "pods (first 100): ${PODS:0:100}..."
echo ""

echo "=== Step 5: count pods ==="
echo "About to call python3 with PODS..."
echo "PODS value: '$PODS'"
POD_COUNT=$(echo "$PODS" | python3 -c "import sys, json; print(len(json.load(sys.stdin).get('items', [])))" 2>&1)
EXITCODE=$?
echo "POD_COUNT result: $POD_COUNT"
echo "Python exit code: $EXITCODE"
echo ""

echo "=== Step 6: server still running? ==="
ps -p $SERVER_PID > /dev/null && echo "YES" || echo "NO"

kill -9 $SERVER_PID 2>/dev/null
