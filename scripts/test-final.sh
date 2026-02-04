#!/bin/bash
cd /mnt/c/projects/k8s_unikernels/sirah
./bin/sirah-apiserver --port 6443 2>&1 &
SERVER_PID=$!
sleep 2

echo "✓ kubectl get pods:"
kubectl get pods 2>&1 && echo "SUCCESS" || echo "FAILED"

echo ""
echo "✗ kubectl apply (requires OpenAPI endpoint - known limitation):"
kubectl apply -f test-pod.json 2>&1 | head -1

kill $SERVER_PID 2>/dev/null || true
