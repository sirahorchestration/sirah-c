#!/bin/bash
# Test sequential requests like view-qemu-pods.sh does

cd /mnt/c/projects/k8s_unikernels/sirah

pkill -9 sirah-apiserver 2>/dev/null
sleep 1

echo "=== Starting server ==="
./bin/sirah-apiserver >/tmp/test.log 2>&1 &
SERVER_PID=$!
sleep 2

echo "=== Test 1: First healthz call ==="
RESULT1=$(curl -s http://localhost:6443/healthz)
echo "Result: $RESULT1"
echo "Server running? $(ps -p $SERVER_PID >/dev/null 2>&1 && echo YES || echo NO)"
echo ""

echo "=== Test 2: Second healthz call ==="
RESULT2=$(curl -s http://localhost:6443/healthz)
echo "Result: $RESULT2"
echo "Server running? $(ps -p $SERVER_PID >/dev/null 2>&1 && echo YES || echo NO)"
echo ""

echo "=== Test 3: Third healthz call ==="
RESULT3=$(curl -s http://localhost:6443/healthz)
echo "Result: $RESULT3"
echo "Server running? $(ps -p $SERVER_PID >/dev/null 2>&1 && echo YES || echo NO)"
echo ""

echo "=== Test 4: Pods list call ==="
RESULT4=$(curl -s http://localhost:6443/api/v1/namespaces/default/pods)
echo "Result (first 100 chars): ${RESULT4:0:100}"
echo "Server running? $(ps -p $SERVER_PID >/dev/null 2>&1 && echo YES || echo NO)"
echo ""

echo "=== Test 5: Grep on healthz result ==="
if echo "$RESULT1" | grep -q "ok"; then
    echo "✓ Grep pattern found"
else
    echo "✗ Grep pattern NOT found"
    echo "Raw result: '$RESULT1'"
    echo "Hex dump:"
    echo "$RESULT1" | od -c
fi

kill -9 $SERVER_PID 2>/dev/null
