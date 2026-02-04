#!/bin/bash
set -e

cd /mnt/c/projects/sirah-c/sirah

# Kill any old processes
pkill -f sirah-apiserver 2>/dev/null || true
pkill -f sirah-controller 2>/dev/null || true
pkill -f qemu 2>/dev/null || true
sleep 1

# Clean old logs and state
rm -rf /tmp/sirah-logs /tmp/api.log /tmp/controller.log

# Start API server
echo "[*] Starting API server..."
./bin/sirah-apiserver > /tmp/api.log 2>&1 &
sleep 3

# Start controller
echo "[*] Starting controller..."
./bin/sirah-controller > /tmp/controller.log 2>&1 &
sleep 3

# Create test pod
echo "[*] Creating test pod..."
POD_NAME="test-kubectl-logs-$(date +%s)"
curl -s -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H "Content-Type: application/json" \
  -u admin:admin \
  -d "{
    \"apiVersion\": \"v1\",
    \"kind\": \"Pod\",
    \"metadata\": {
      \"name\": \"$POD_NAME\",
      \"namespace\": \"default\"
    },
    \"spec\": {
      \"containers\": [{
        \"name\": \"app\",
        \"image\": \"/tmp/sirah-unikernels/test-kernel.img\",
        \"resources\": {
          \"limits\": {
            \"memory\": \"128Mi\",
            \"cpu\": \"1\"
          }
        }
      }]
    }
  }" > /dev/null

echo "[*] Pod created: $POD_NAME"
sleep 4

# Wait for logs to be written
echo "[*] Waiting for logs..."
for i in {1..10}; do
    if [ -f "/tmp/sirah-logs/pods/default/$POD_NAME/app.log" ]; then
        LOG_SIZE=$(wc -c < "/tmp/sirah-logs/pods/default/$POD_NAME/app.log")
        if [ "$LOG_SIZE" -gt 100 ]; then
            echo "✓ Logs found ($LOG_SIZE bytes)"
            break
        fi
    fi
    echo "  Attempt $i: waiting for logs..."
    sleep 1
done

# Test 1: curl with correct Content-Type
echo ""
echo "[TEST 1] curl endpoint (text/plain):"
curl -s -u admin:admin "http://localhost:6443/api/v1/namespaces/default/pods/$POD_NAME/log" | head -5

# Test 2: kubectl logs
echo ""
echo "[TEST 2] kubectl logs:"
kubectl logs "$POD_NAME" -n default 2>&1 | head -10 || echo "kubectl logs failed (expected if kubectl not configured)"

# Show log file directly
echo ""
echo "[TEST 3] Log file content:"
head -10 "/tmp/sirah-logs/pods/default/$POD_NAME/app.log"

# Cleanup
pkill -f sirah 2>/dev/null || true
pkill -f qemu 2>/dev/null || true

echo ""
echo "[*] Test complete!"
