#!/bin/bash
# Test the nodes endpoint directly

cd /mnt/c/projects/k8s_unikernels/sirah

pkill -9 sirah-apiserver 2>/dev/null
sleep 1

./bin/sirah-apiserver >/tmp/test.log 2>&1 &
SERVER_PID=$!
sleep 2

echo "=== Test nodes endpoint ==="
NODES=$(curl -s http://localhost:6443/api/v1/nodes)
echo "NODES: $NODES"
echo ""

echo "=== Count nodes with python ==="
NODE_COUNT=$(echo "$NODES" | python3 -c "import sys, json; print(len(json.load(sys.stdin).get('items', [])))" 2>&1)
echo "NODE_COUNT: $NODE_COUNT"
echo "NODE_COUNT type: $(echo $NODE_COUNT | head -c 20)"
echo ""

echo "=== Test with simple python ==="
echo "$NODES" | python3 -c "import sys, json; print('Valid JSON'); data = json.load(sys.stdin); print('Items:', len(data.get('items', [])))"

kill -9 $SERVER_PID 2>/dev/null
