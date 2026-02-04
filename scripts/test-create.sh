#!/bin/bash
cd /mnt/c/projects/k8s_unikernels/sirah
make 2>&1 | grep '✓' || true
./bin/sirah-apiserver --port 6443 2>&1 &
SERVER_PID=$!
sleep 2

echo "=== Test 1: Using create instead of apply ==="
kubectl create -f test-pod.json 2>&1 || echo "Create failed"

echo ""
echo "=== Test 2: Using dry-run ==="
kubectl apply -f test-pod.json --dry-run=client -o yaml 2>&1 | head -20 || echo "Dry-run failed"

echo ""
echo "=== Test 3: Get pods ==="
kubectl get pods 2>&1 || echo "Get pods failed"

kill $SERVER_PID 2>/dev/null || true
