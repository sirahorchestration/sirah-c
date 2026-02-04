#!/bin/bash
ETCD_ADDR="${ETCD_ADDR:-localhost:2379}"

# NOTE: etcd should be running as a service
# API server will connect to it via the --etcd argument

# Start API server with etcd support
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
"$SCRIPT_DIR/bin/sirah-apiserver" --etcd "$ETCD_ADDR" --port 6443 &
SERVER_PID=$!
sleep 2

# Test with curl
echo "Testing /healthz..."
curl -s http://localhost:6443/healthz || echo "Failed"

echo ""
echo "Testing /api/v1/nodes..."
curl -s http://localhost:6443/api/v1/nodes || echo "Failed"

echo ""
echo "Server PID: $SERVER_PID"
echo "Press Ctrl+C to stop"

# Keep running
wait $SERVER_PID
