#!/bin/bash
pkill -9 sirah-apiserver 2>/dev/null
sleep 1

cd /mnt/c/projects/k8s_unikernels/sirah

echo "Starting server..."
./bin/sirah-apiserver >/dev/null 2>&1 &
SERVER_PID=$!
sleep 2

echo "Testing healthz endpoint..."
HEALTH=$(curl -s http://localhost:6443/healthz)
echo "Response: '$HEALTH'"

echo ""
echo "Testing pods endpoint..."
PODS=$(curl -s http://localhost:6443/api/v1/namespaces/default/pods)
echo "Response (first 100 chars): ${PODS:0:100}"

echo ""
echo "Server still running?"
if ps -p $SERVER_PID > /dev/null 2>&1; then
    echo "YES"
    kill -9 $SERVER_PID
else
    echo "NO - CRASHED"
fi



