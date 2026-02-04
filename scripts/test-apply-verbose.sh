#!/bin/bash
cd /mnt/c/projects/k8s_unikernels/sirah
make 2>&1 | grep -E '(error|✓)' || true
./bin/sirah-apiserver --port 6443 2>&1 &
SERVER_PID=$!
sleep 2

echo "=== Testing kubectl apply ==="
kubectl apply -f test-pod.json 2>&1 || true

echo ""
echo "=== Testing with dry-run ==="
kubectl apply -f test-pod.json --dry-run=client 2>&1 || true

echo ""
echo "Killing server..."
kill $SERVER_PID 2>/dev/null || true
