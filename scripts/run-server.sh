#!/bin/bash
# Kill any existing server
pkill -f sirah-apiserver 2>/dev/null
sleep 1

# Start API server completely detached
nohup /mnt/c/projects/k8s_unikernels/sirah/bin/sirah-apiserver --port 6443 >/tmp/sirah-apiserver.log 2>&1 &
SERVER_PID=$!
echo "API Server started with PID $SERVER_PID"
sleep 2

# Verify it's running
if ps -p $SERVER_PID > /dev/null; then
    echo "✓ API Server is running on port 6443"
else
    echo "✗ API Server failed to start"
    echo "Error log:"
    cat /tmp/sirah-apiserver.log
    exit 1
fi
