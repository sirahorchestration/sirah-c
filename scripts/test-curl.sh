#!/bin/bash
cd /mnt/c/projects/k8s_unikernels/sirah
./bin/sirah-apiserver --port 6443 2>&1 &
SERVER_PID=$!
sleep 2

# Test the exact path kubectl would use
echo "Testing POST to /api/v1/namespaces/default/pods..."
curl -i -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H 'Content-Type: application/json' \
  -d @test-pod.json 2>&1 | head -20

echo ""
echo "Testing GET to /api/v1/namespaces/default/pods..."
curl -i http://localhost:6443/api/v1/namespaces/default/pods 2>&1 | head -5

kill $SERVER_PID 2>/dev/null
