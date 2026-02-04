#!/bin/bash
cd /mnt/c/projects/sirah-c/sirah
./bin/sirah-apiserver --etcd localhost:2379 &
SERVER_PID=$!
sleep 3

echo "Testing /openapi/v2:"
curl -s http://localhost:6443/openapi/v2 | head -c 300
echo ""

echo ""
echo "Testing kubectl apply:"
cd /mnt/c/projects && kubectl apply -f sirah/tests/integration/fixtures/test-pod.yaml 2>&1

echo ""
echo "Getting pod:"
kubectl get pods -n default 2>&1 | head -10

kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null
