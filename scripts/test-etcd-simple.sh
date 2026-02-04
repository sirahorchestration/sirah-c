#!/bin/bash

# Simple etcd integration test - no trap, minimal complexity

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== Starting etcd (if not running) ==="
pgrep -f 'etcd ' > /dev/null && echo "✓ etcd already running" || (etcd > /tmp/etcd.log 2>&1 &)
sleep 2

echo "=== Killing any existing API server ==="
pkill -f 'sirah-apiserver' || true
sleep 1

echo "=== Starting API server ==="
cd sirah
./bin/sirah-apiserver --etcd http://localhost:2379 > /tmp/apiserver.log 2>&1 &
API_PID=$!
echo "API Server PID: $API_PID"

# Wait for initialization
sleep 5

# Check if it's still running
if ! ps -p $API_PID > /dev/null; then
    echo "✗ API server crashed!"
    echo "=== API Server Log ==="
    cat /tmp/apiserver.log
    exit 1
fi

echo "✓ API server is running"

# Test 1: GET /healthz
echo ""
echo "=== Test 1: GET /healthz ==="
STATUS=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:6443/healthz)
echo "Status: $STATUS"
if [ "$STATUS" = "200" ]; then
    echo "✓ PASSED"
else
    echo "✗ FAILED (expected 200)"
fi

# Test 2: POST /api/v1/namespaces/default/pods (create pod)
echo ""
echo "=== Test 2: POST /api/v1/namespaces/default/pods ==="
POD_JSON='{"apiVersion":"v1","kind":"Pod","metadata":{"name":"test-pod"},"spec":{"containers":[{"name":"main","image":"busybox"}]}}'
RESPONSE=$(curl -s -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H 'Content-Type: application/json' \
  -d "$POD_JSON" \
  -w "\n%{http_code}")

STATUS=$(echo "$RESPONSE" | tail -n 1)
BODY=$(echo "$RESPONSE" | sed '$d')

echo "Status: $STATUS"
echo "Body: $(echo "$BODY" | head -c 100)"

if [ "$STATUS" = "201" ]; then
    echo "✓ PASSED"
else
    echo "✗ FAILED (expected 201, got $STATUS)"
fi

# Test 3: GET /api/v1/namespaces/default/pods/{name}
echo ""
echo "=== Test 3: GET /api/v1/namespaces/default/pods/test-pod ==="
STATUS=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:6443/api/v1/namespaces/default/pods/test-pod)
echo "Status: $STATUS"
if [ "$STATUS" = "200" ]; then
    echo "✓ PASSED"
else
    echo "✗ FAILED (expected 200)"
fi

# Cleanup
echo ""
echo "=== Cleanup ==="
kill $API_PID 2>/dev/null || true
sleep 1

if ps -p $API_PID > /dev/null; then
    echo "Killing API server forcefully..."
    kill -9 $API_PID
fi

echo "Done"
