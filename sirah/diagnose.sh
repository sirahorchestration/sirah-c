#!/bin/bash

echo "=== SIRAH CLUSTER DIAGNOSTIC ==="
echo ""

echo "[1] Checking running processes..."
ps aux | grep -E "sirah-|etcd" | grep -v grep || echo "No sirah processes running"
echo ""

echo "[2] Listing pods in cluster..."
curl -s http://localhost:6443/api/v1/namespaces/default/pods | jq -r '.items[] | "Pod: \(.metadata.name) | Status: \(.status.phase) | Image: \(.spec.containers[0].image)"' 2>/dev/null || echo "Failed to query pods"
echo ""

echo "[3] Checking QEMU processes..."
pgrep -a qemu-system || echo "No QEMU processes running"
echo ""

echo "[4] Last 30 controller log lines..."
tail -30 /tmp/sirah-logs/controller.log 2>/dev/null || echo "No controller log found"
echo ""

echo "[5] Check controller is running..."
ps aux | grep "sirah-controller" | grep -v grep || echo "Controller not running"
