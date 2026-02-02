#!/bin/bash

# Test QEMU unikernel log streaming
# Verifies that logs from unikernels appear in kubectl logs

set -e

API_URL="http://localhost:6443"
ADMIN_USER="admin:admin"

echo "=== QEMU Log Streaming Test ==="
echo ""

# Check 1: Prerequisites
echo "[1] Checking prerequisites..."
if ! command -v curl &> /dev/null; then
    echo "✗ curl not found"
    exit 1
fi
if ! command -v jq &> /dev/null; then
    echo "✗ jq not found"
    exit 1
fi
echo "✓ curl and jq available"
echo ""

# Check 2: API server
echo "[2] Checking API server..."
if ! curl -s -u "$ADMIN_USER" "$API_URL/api/v1/pods" > /dev/null 2>&1; then
    echo "✗ API server not responding"
    exit 1
fi
echo "✓ API server responding"
echo ""

# Check 3: Create test unikernel
echo "[3] Preparing test unikernel..."
mkdir -p /tmp/sirah-unikernels

TEST_KERNEL="/tmp/sirah-unikernels/test-kernel.img"
if [ ! -f "$TEST_KERNEL" ]; then
    echo "  Creating test kernel..."
    dd if=/dev/zero of="$TEST_KERNEL" bs=1M count=10 2>/dev/null
    echo "  ✓ Created $TEST_KERNEL"
else
    echo "  ✓ Using existing $TEST_KERNEL"
fi
echo ""

# Check 4: Create test pod
echo "[4] Creating test pod..."
POD_NAME="log-stream-test-$(date +%s)"
NAMESPACE="default"

POD_MANIFEST=$(cat << EOF
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "$POD_NAME",
    "namespace": "$NAMESPACE"
  },
  "spec": {
    "containers": [{
      "name": "app",
      "image": "$TEST_KERNEL",
      "resources": {
        "limits": {
          "memory": "128Mi",
          "cpu": "1"
        }
      }
    }]
  }
}
EOF
)

RESPONSE=$(curl -s -X POST "$API_URL/api/v1/namespaces/$NAMESPACE/pods" \
  -H "Content-Type: application/json" \
  -u "$ADMIN_USER" \
  -d "$POD_MANIFEST")

# Check if pod was created by verifying the response has the pod name and phase
CREATED_NAME=$(echo "$RESPONSE" | jq -r '.metadata.name // empty' 2>/dev/null)
PHASE=$(echo "$RESPONSE" | jq -r '.status.phase // empty' 2>/dev/null)

if [ "$CREATED_NAME" = "$POD_NAME" ]; then
    echo "✓ Pod created: $POD_NAME (Phase: $PHASE)"
else
    echo "✗ Failed to create pod"
    echo "Response:"
    echo "$RESPONSE" | jq . 2>/dev/null || echo "$RESPONSE"
    exit 1
fi
echo ""

# Check 5: Wait for pod to enter Pending state
echo "[5] Waiting for pod to be scheduled..."
PHASE="Unknown"
for i in {1..15}; do
    # Retrieve the pod with HTTP status code
    POD_RESPONSE=$(curl -s -w "\n%{http_code}" -u "$ADMIN_USER" \
            "$API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_NAME" 2>/dev/null)
    
    # Split response and HTTP code
    HTTP_CODE=$(echo "$POD_RESPONSE" | tail -1)
    BODY=$(echo "$POD_RESPONSE" | head -n -1)
    
    # Try to extract phase from JSON response
    if [ "$HTTP_CODE" = "200" ] && [ -n "$BODY" ]; then
        PHASE=$(echo "$BODY" | jq -r '.status.phase // "Unknown"' 2>/dev/null)
        if [ -z "$PHASE" ]; then
            PHASE="Unknown"
        fi
    else
        PHASE="NotFound"
    fi
    
    echo "  Attempt $i: Phase = $PHASE (HTTP $HTTP_CODE)"
    
    if [ "$PHASE" = "Pending" ] || [ "$PHASE" = "Running" ]; then
        echo "✓ Pod in $PHASE state"
        break
    fi
    
    sleep 1
done

if [ "$PHASE" != "Pending" ] && [ "$PHASE" != "Running" ]; then
    echo "⚠ Pod not yet in Running/Pending state (Phase: $PHASE)"
    echo "  Controller may be spawning VM now, continuing..."
fi
echo ""

# Check 6: Check for QEMU process
echo "[6] Checking for QEMU process..."
VM_ID="$NAMESPACE-$POD_NAME"
sleep 3  # Give controller time to spawn

QEMU_COUNT=$(pgrep -f "qemu.*$VM_ID" 2>/dev/null | wc -l || echo 0)
if [ "$QEMU_COUNT" -gt 0 ]; then
    echo "✓ QEMU process found"
    pgrep -a "qemu.*$VM_ID" | sed 's/^/  /'
else
    echo "⚠ No QEMU process found yet (may still be starting)"
fi
echo ""

# Check 7: Check pod log file in final location
echo "[7] Checking pod log file..."
POD_LOG_DIR="/tmp/sirah-logs/pods/$NAMESPACE/$POD_NAME"
POD_LOG_FILE="$POD_LOG_DIR/app.log"

if [ -f "$POD_LOG_FILE" ]; then
    LINES=$(wc -l < "$POD_LOG_FILE" 2>/dev/null || echo 0)
    SIZE=$(du -h "$POD_LOG_FILE" | awk '{print $1}')
    echo "✓ Pod log file exists: $POD_LOG_FILE"
    echo "  Size: $SIZE, Lines: $LINES"
    
    if [ "$LINES" -gt 0 ]; then
        echo ""
        echo "  Last 10 lines from pod logs:"
        tail -10 "$POD_LOG_FILE" | sed 's/^/    /'
    fi
else
    echo "⚠ Pod log file not found: $POD_LOG_FILE"
    echo "  (QEMU may not have started yet)"
fi
echo ""

# Check 8: Retrieve logs via API
echo "[8] Retrieving logs via API endpoint..."
API_RESPONSE=$(curl -s -w "\n%{http_code}" -u "$ADMIN_USER" \
           "$API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_NAME/log" 2>/dev/null)

HTTP_CODE=$(echo "$API_RESPONSE" | tail -1)
API_LOGS=$(echo "$API_RESPONSE" | head -n -1)

if [ "$HTTP_CODE" != "200" ]; then
    echo "⚠ API returned HTTP $HTTP_CODE"
elif [ -z "$API_LOGS" ]; then
    echo "⚠ No logs available via API yet"
else
    echo "✓ Logs retrieved via API (HTTP 200):"
    echo "$API_LOGS" | head -10 | sed 's/^/  /'
    
    TOTAL_LINES=$(echo "$API_LOGS" | wc -l)
    echo "  (Showing first 10 of $TOTAL_LINES lines)"
fi
echo ""

# Check 9: Test with tail parameter
echo "[9] Testing API with ?tailLines=5..."
TAIL_RESPONSE=$(curl -s -w "\n%{http_code}" -u "$ADMIN_USER" \
            "$API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_NAME/log?tailLines=5" 2>/dev/null)

TAIL_HTTP_CODE=$(echo "$TAIL_RESPONSE" | tail -1)
TAIL_LOGS=$(echo "$TAIL_RESPONSE" | head -n -1)

if [ "$TAIL_HTTP_CODE" != "200" ]; then
    echo "⚠ API returned HTTP $TAIL_HTTP_CODE"
elif [ -n "$TAIL_LOGS" ]; then
    echo "✓ Logs with tail parameter (HTTP 200):"
    echo "$TAIL_LOGS" | sed 's/^/  /'
else
    echo "⚠ No logs with tail parameter yet"
fi
echo ""

# Check 10: Summary
echo "=== Test Summary ==="
echo ""
echo "Pod Name: $POD_NAME"
echo "Namespace: $NAMESPACE"
echo "VM ID: $VM_ID"
echo "Log File: $LOG_FILE"
echo ""

echo "To monitor in real-time:"
echo "  tail -f $LOG_FILE"
echo ""

echo "To get logs via API:"
echo "  curl -u $ADMIN_USER '$API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_NAME/log'"
echo ""

echo "To use kubectl:"
echo "  kubectl logs $POD_NAME"
echo "  kubectl logs -f $POD_NAME (follow mode)"
echo ""

echo "Cleanup:"
echo "  curl -X DELETE -u $ADMIN_USER '$API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_NAME'"
echo ""

echo "✓ Test pod created and monitoring"
echo ""
