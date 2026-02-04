#!/bin/bash

# Kill any existing processes
pkill -f etcd
pkill -f sirah
sleep 2

# Clean and start etcd
rm -rf /tmp/sirah-etcd
mkdir -p /tmp/sirah-logs

echo "Starting etcd..."
nohup etcd \
    --listen-client-urls http://localhost:2379 \
    --advertise-client-urls http://localhost:2379 \
    --data-dir /tmp/sirah-etcd \
    > /tmp/sirah-logs/etcd.log 2>&1 &

sleep 3

BIN_DIR="/mnt/c/projects/k8s_unikernels/sirah/bin"

echo "Starting API Server..."
nohup "$BIN_DIR/sirah-apiserver" --port 6443 > /tmp/sirah-logs/apiserver.log 2>&1 &
sleep 3

echo "Starting Scheduler..."
nohup "$BIN_DIR/sirah-scheduler" --api-server http://localhost:6443 > /tmp/sirah-logs/scheduler.log 2>&1 &
sleep 2

echo "Starting Controller..."
nohup "$BIN_DIR/sirah-controller" --api-server http://localhost:6443 > /tmp/sirah-logs/controller.log 2>&1 &
sleep 2

echo "Starting Kubelet..."
nohup "$BIN_DIR/sirah-kubelet" --node worker1 --api-server http://localhost:6443 > /tmp/sirah-logs/kubelet.log 2>&1 &
sleep 2

echo ""
echo "Testing API..."
curl -s http://localhost:6443/api/v1/nodes | head -c 100
echo ""
echo ""
echo "=== Running Components ==="
ps aux | grep -E 'sirah-|[e]tcd' | grep -v grep
