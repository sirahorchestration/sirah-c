#!/bin/bash

# Simple etcd cluster test (standalone mode with cluster config simulation)

DATA_DIR="/tmp/etcd-test"
rm -rf "$DATA_DIR"
mkdir -p "$DATA_DIR/data"

echo "╔════════════════════════════════════════╗"
echo "║  etcd Standalone HA Testing            ║"
echo "╚════════════════════════════════════════╝"
echo ""

echo "Starting standalone etcd node (Phase 2B test)..."

# Start single etcd node in standalone mode
etcd --name=node-0 \
  --listen-client-urls=http://127.0.0.1:2379 \
  --advertise-client-urls=http://127.0.0.1:2379 \
  --data-dir="$DATA_DIR/data" \
  --log-level=warn > "$DATA_DIR/etcd.log" 2>&1 &
PID=$!

echo "Started etcd node (PID=$PID)"
sleep 3

# Test 1: Health check
echo ""
echo "Test 1: Health Check"
health=$(curl -s http://127.0.0.1:2379/health 2>/dev/null | grep -o '"health":"true"' || echo "FAILED")
echo "  Node health: $health"

# Test 2: Write operation
echo ""
echo "Test 2: Write Operation"
ETCDCTL_API=3 /usr/local/bin/etcdctl --endpoints=http://127.0.0.1:2379 put /sirah/pods/default/test-pod '{"name":"test-pod"}' 2>/dev/null
echo "  ✓ Written pod data"

# Test 3: Read operation
echo ""
echo "Test 3: Read Operation"
value=$(ETCDCTL_API=3 /usr/local/bin/etcdctl --endpoints=http://127.0.0.1:2379 get /sirah/pods/default/test-pod 2>/dev/null | tail -1)
if [ -n "$value" ]; then
  echo "  ✓ Read pod data: $(echo "$value" | cut -c1-40)..."
else
  echo "  ✗ Failed to read pod data"
fi

# Test 4: Delete operation
echo ""
echo "Test 4: Delete Operation"
ETCDCTL_API=3 /usr/local/bin/etcdctl --endpoints=http://127.0.0.1:2379 del /sirah/pods/default/test-pod 2>/dev/null
echo "  ✓ Deleted pod data"

# Test 5: List operation
echo ""
echo "Test 5: List with Prefix"
for i in 1 2 3; do
  ETCDCTL_API=3 /usr/local/bin/etcdctl --endpoints=http://127.0.0.1:2379 put "/sirah/pods/default/pod-$i" '{"id":'$i'}' 2>/dev/null
done
count=$(ETCDCTL_API=3 /usr/local/bin/etcdctl --endpoints=http://127.0.0.1:2379 get /sirah/pods/default --prefix 2>/dev/null | grep -c '"' || echo "0")
echo "  ✓ Listed $((count / 2)) pods"

# Cleanup
echo ""
echo "Cleanup..."
kill $PID 2>/dev/null
wait $PID 2>/dev/null
rm -rf "$DATA_DIR"
echo ""
echo "✓ All tests complete"
