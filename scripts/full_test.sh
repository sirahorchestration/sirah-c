#!/bin/bash

echo "=== Full Sirah System Test ==="
echo ""

# Kill any existing processes
echo "Cleaning up existing processes..."
pkill -9 -f 'sirah-|qemu-' 2>/dev/null || true
sleep 1

# Clean and restart etcd
echo "Setting up etcd..."
pkill -9 etcd 2>/dev/null || true
sleep 1
rm -rf /tmp/etcd-data*
/mnt/c/projects/sirah-c/etcd --data-dir=/tmp/etcd-data > /tmp/etcd.log 2>&1 &
sleep 3

# Start API server
echo "Starting API server..."
cd /mnt/c/projects/sirah-c/sirah
./bin/sirah-apiserver > /tmp/apiserver.log 2>&1 &
APISERVER_PID=$!
sleep 3

# Start controller manager
echo "Starting controller manager..."
./bin/sirah-controller > /tmp/controller.log 2>&1 &
CONTROLLER_PID=$!
sleep 2

# Start kubelet for control-plane node
echo "Starting kubelet (control-plane node)..."
./bin/sirah-kubelet --node-name control-plane > /tmp/kubelet.log 2>&1 &
KUBELET_PID=$!
sleep 2

# Run the test
echo ""
echo "=== Running test ==="
/mnt/c/projects/sirah-c/test_logging.sh

echo ""
echo "=== Test complete ==="
echo "API Server PID: $APISERVER_PID"
echo "Controller PID: $CONTROLLER_PID"
echo "Kubelet PID: $KUBELET_PID"
echo ""
echo "Logs:"
echo "  API Server: /tmp/apiserver.log"
echo "  Controller: /tmp/controller.log"
echo "  Kubelet: /tmp/kubelet.log"
