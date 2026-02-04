#!/bin/bash
set -e

cd /mnt/c/projects/sirah-c/sirah

# Kill any old processes
pkill -f sirah-apiserver 2>/dev/null || true
pkill -f sirah-controller 2>/dev/null || true
pkill -f qemu 2>/dev/null || true
sleep 1

# Clean old logs and state
rm -rf /tmp/sirah-logs /tmp/qemu-*.log /tmp/api.log /tmp/controller.log

# Start API server
echo "[*] Starting API server..."
./bin/sirah-apiserver > /tmp/api.log 2>&1 &
API_PID=$!
sleep 2

# Start controller
echo "[*] Starting controller..."
./bin/sirah-controller > /tmp/controller.log 2>&1 &
CTRL_PID=$!
sleep 2

# Run the test
echo "[*] Running test..."
bash tests/test-qemu-log-streaming.sh

# Cleanup
kill $API_PID $CTRL_PID 2>/dev/null || true
