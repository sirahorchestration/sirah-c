#!/bin/bash
cd /mnt/c/projects/k8s_unikernels/sirah
./bin/sirah-apiserver --port 6443 2>/dev/null &
SERVER_PID=$!
sleep 1

echo "Testing /openapi/v2:"
curl -s http://localhost:6443/openapi/v2 | head -c 100
echo ""

echo ""
echo "Testing /api/v1:"
curl -s http://localhost:6443/api/v1 | python3 -m json.tool | head -20

kill $SERVER_PID 2>/dev/null
