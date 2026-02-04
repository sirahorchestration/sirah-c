#!/bin/bash

# Start in WSL environment
wsl << 'EOF'

echo "Starting Sirah API Server..."
/mnt/c/projects/sirah-c/sirah/bin/sirah-apiserver --etcd http://localhost:2379 > /tmp/apiserver.log 2>&1 &
API_PID=$!
echo "API Server PID: $API_PID"

sleep 3

echo ""
echo "=== Testing health endpoint ==="
curl -s http://localhost:6443/healthz | head -c 100
echo ""

echo ""
echo "=== Testing kubectl get pods ==="
kubectl get pods -A 2>&1 || echo "kubectl failed"

echo ""
echo "=== Testing kubectl apply ==="
cat > /tmp/test-pod.yaml << 'YAML'
apiVersion: v1
kind: Pod
metadata:
  name: test-pod-001
  namespace: default
spec:
  containers:
  - name: app
    image: nginx:latest
YAML

kubectl apply -f /tmp/test-pod.yaml

echo ""
echo "=== Getting pod info ==="
kubectl get pod test-pod-001 -o json 2>&1 || echo "kubectl failed"

echo ""
echo "=== Killing API Server ==="
kill $API_PID || true
wait $API_PID 2>/dev/null || true

EOF
