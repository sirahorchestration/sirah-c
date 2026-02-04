#!/bin/bash
set -e

cd /mnt/c/projects/sirah-c/sirah

echo "=== Killing old processes ==="
pkill -9 -f 'sirah-apiserver|sirah-controller|qemu' || true
sleep 1

echo "=== Checking etcd ==="
pkill -9 etcd || true
sleep 1
rm -rf /tmp/etcd-data*
echo "Starting etcd..."
/mnt/c/projects/sirah-c/etcd --data-dir=/tmp/etcd-data > /tmp/etcd.log 2>&1 &
sleep 3

echo "=== Testing API server startup ==="
timeout 10 ./bin/sirah-apiserver 2>&1 || echo "API server exited"

echo "=== Done ==="
