#!/bin/bash
# Test both POST and GET endpoints

pkill -9 sirah-apiserver 2>/dev/null
cd /mnt/c/projects/k8s_unikernels/sirah

# Start API server
./bin/sirah-apiserver > /tmp/apiserver-test.log 2>&1 &
API_PID=$!
sleep 2

# Test POST (create pod)
echo "=== TEST 1: Creating pod ===" 
curl -s -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H "Content-Type: application/json" \
  -d '{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "curl-test-pod",
    "namespace": "default"
  },
  "spec": {
    "containers": [
      {
        "name": "app",
        "image": "/tmp/test-kernel"
      }
    ]
  }
}'
echo ""
echo ""

# Test GET (list pods)
echo "=== TEST 2: Listing pods (should show 1 pod)==="
curl -s http://localhost:6443/api/v1/namespaces/default/pods
echo ""

# Cleanup
kill $API_PID 2>/dev/null
echo "Test complete!"
