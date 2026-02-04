#!/bin/bash
# Debug test to find the crash

cd /mnt/c/projects/k8s_unikernels/sirah

pkill -9 sirah-apiserver 2>/dev/null
sleep 1

echo "=== Starting server with output redirection ==="
./bin/sirah-apiserver >/tmp/test.log 2>&1 &
SERVER_PID=$!
sleep 2

echo "=== Server PID: $SERVER_PID ==="
echo ""

echo "=== Test 1: healthz endpoint ==="
echo "Making request..."
curl -s http://localhost:6443/healthz 2>&1
echo ""

echo "=== Test 2: pods endpoint ==="
echo "Making request..."
curl -s http://localhost:6443/api/v1/namespaces/default/pods 2>&1 | head -c 100
echo ""

echo "=== Checking if server still running ==="
if ps -p $SERVER_PID > /dev/null 2>&1; then
    echo "YES - Server alive"
    kill -9 $SERVER_PID
else
    echo "NO - Server crashed!"
fi

echo ""
echo "=== Server log ==="
cat /tmp/test.log

