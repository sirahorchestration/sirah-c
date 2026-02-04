#!/bin/bash
cd /mnt/c/projects/k8s_unikernels/sirah

# Kill any existing server
pkill -f "sirah-apiserver" || true
sleep 1

# Start server in background writing to log
nohup ./bin/sirah-apiserver --port 6443 > /tmp/apiserver.log 2>&1 &
sleep 3

# Send pod creation request
echo "Sending pod creation request..."
curl -s -X POST http://localhost:6443/api/v1/namespaces/default/pods -H 'Content-Type: application/json' -d @test_pod.json
echo ""

# Show relevant output
echo "API Server output (last 30 lines):"
tail -30 /tmp/apiserver.log
