#!/bin/bash
cd /mnt/c/projects/k8s_unikernels/sirah

# Kill any existing server
pkill -f "sirah-apiserver" || true
sleep 1

# Start server in background writing to log
nohup ./bin/sirah-apiserver --port 6443 > /tmp/apiserver.log 2>&1 &
sleep 3

# Send test POST request
echo "Sending test POST request..."
curl -X POST http://localhost:6443/test -H 'Content-Type: application/json' -d '{"test":"data"}' -s
echo ""

# Show last 40 lines of log
echo "API Server output:"
tail -40 /tmp/apiserver.log
