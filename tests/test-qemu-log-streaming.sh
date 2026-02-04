#!/bin/bash

# Test QEMU unikernel log streaming
# Verifies that logs from unikernels appear in kubectl logs
# 
# REQUIREMENTS:
#   - All 4 services running (etcd, API server, scheduler, CONTROLLER)
#   - Pod Controller MUST be running (spawns QEMU VMs)
#   - Valid unikernel image at /tmp/sirah-unikernels/test-kernel.img

# Note: Don't use set -e globally as we want to handle failures gracefully
set +e

# Setup logging
LOG_DIR="/tmp/sirah-logs"
mkdir -p "$LOG_DIR"
SCRIPT_LOG="$LOG_DIR/test-qemu-log-streaming.log"

# Redirect all script output to log file while also showing it on terminal
exec > >(tee -a "$SCRIPT_LOG")
exec 2>&1

API_URL="http://localhost:6443"
ADMIN_USER="admin:admin"
MAX_RETRIES=10
RETRY_DELAY=2

echo "=== QEMU Log Streaming Test ==="
echo ""
echo "⚠️  NOTE: Pod Controller MUST be running for logs to be generated!"
echo "    Logs are created when controller spawns QEMU VMs."
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

# Check 2: API server with retry logic
echo "[2] Checking API server..."
API_READY=0
for i in $(seq 1 $MAX_RETRIES); do
    echo "  Attempt $i/$MAX_RETRIES..."
    if curl -s --connect-timeout 2 -m 5 -u "$ADMIN_USER" "$API_URL/api/v1/pods" > /dev/null 2>&1; then
        echo "✓ API server responding"
        API_READY=1
        break
    fi
    if [ $i -lt $MAX_RETRIES ]; then
        echo "  Waiting ${RETRY_DELAY}s before retry..."
        sleep $RETRY_DELAY
    fi
done

if [ $API_READY -eq 0 ]; then
    echo "✗ API server not responding after $MAX_RETRIES attempts"
    echo "  Check if services are running: ps aux | grep sirah"
    exit 1
fi
echo ""

# Check 3: Create test unikernel
echo "[3] Preparing test unikernel..."
mkdir -p /tmp/sirah-unikernels

# Source unikernel (from repo)
SOURCE_KERNEL="/tmp/sirah-unikernels/test-kernel.img"

# Create dynamic unikernel name based on pod
TIMESTAMP=$(date +%s%N)
DYNAMIC_KERNEL="/tmp/sirah-unikernels/kernel-${TIMESTAMP}.img"

# Check if controller is running (critical for log generation)
CONTROLLER_RUNNING=$(pgrep -f "sirah-controller" 2>/dev/null || echo "")
if [ -z "$CONTROLLER_RUNNING" ]; then
    echo "⚠️  WARNING: Pod Controller is NOT running!"
    echo "    Logs will NOT be generated without the Controller."
    echo "    Start with: bash start-all.sh"
    echo ""
fi

# Check if test kernel exists
if [ ! -f "$SOURCE_KERNEL" ]; then
    echo "✗ Source kernel not found: $SOURCE_KERNEL"
    echo "  Please create a valid unikernel image at $SOURCE_KERNEL"
    echo "  (Cannot use dummy dd image - QEMU needs a real kernel)"
    echo ""
    echo "  For testing without a real kernel:"
    echo "    1. Create a minimal ELF binary for x86_64"
    echo "    2. Or use a pre-built unikernel image (MirageOS, OSv, etc.)"
    echo ""
    exit 1
else
    FILE_TYPE=$(file "$SOURCE_KERNEL" 2>/dev/null || echo "Unknown")
    echo "  ✓ Found source kernel: $SOURCE_KERNEL"
    echo "    Type: $FILE_TYPE"
    
    # Copy to dynamic name for this test run
    echo "  ✓ Copying to dynamic name: $DYNAMIC_KERNEL"
    cp "$SOURCE_KERNEL" "$DYNAMIC_KERNEL"
    
    if [ $? -eq 0 ]; then
        echo "  ✓ Dynamic kernel created successfully"
    else
        echo "✗ Failed to copy kernel to dynamic name"
        exit 1
    fi
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
      "image": "$DYNAMIC_KERNEL",
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
  -d "$POD_MANIFEST" 2>&1)

# Check if pod was created by verifying the response has the pod name and phase
CREATED_NAME=$(echo "$RESPONSE" | jq -r '.metadata.name // empty' 2>/dev/null)
PHASE=$(echo "$RESPONSE" | jq -r '.status.phase // empty' 2>/dev/null)

if [ "$CREATED_NAME" = "$POD_NAME" ]; then
    echo "✓ Pod created: $POD_NAME (Phase: $PHASE)"
else
    echo "⚠ Pod creation response:"
    echo "  Status: $(echo "$RESPONSE" | jq -r '.status // "unknown"' 2>/dev/null)"
    echo "  Response length: ${#RESPONSE}"
    if [ -z "$RESPONSE" ]; then
        echo "  (Empty response - API server may be unresponsive)"
    else
        echo "$RESPONSE" | jq . 2>/dev/null || echo "$RESPONSE"
    fi
    echo ""
    echo "Continuing with test..."
fi
echo ""

# Check 5: Wait for pod to enter Pending state
echo "[5] Waiting for pod to be scheduled..."
echo "    (Controller should pick this up every 5 seconds)"
PHASE="Unknown"
for i in {1..30}; do  # Increased to 30 to wait for controller
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
    
    if [ "$PHASE" = "Running" ]; then
        echo "✓ Pod is Running!"
        break
    fi
    
    sleep 1
done

if [ "$PHASE" != "Running" ]; then
    echo "⚠ Pod not yet in Running state (Phase: $PHASE)"
    echo "  If Controller is running, it should transition pod to Running"
    echo "  and spawn a QEMU VM, which creates the log file."
fi
echo ""

# Check 6: Check for QEMU process
echo "[6] Checking for QEMU process..."
echo "    (Controller spawns QEMU for each pod)"
VM_ID="$NAMESPACE-$POD_NAME"
sleep 2  # Give controller time to spawn

# QEMU processes appear as 'qemu-system-*' with the pod name
QEMU_PIDS=$(pgrep -f "qemu" 2>/dev/null || echo "")
QEMU_COUNT=$(echo "$QEMU_PIDS" | grep -c . 2>/dev/null || echo 0)

if [ "$QEMU_COUNT" -gt 0 ]; then
    echo "✓ QEMU process(es) found:"
    pgrep -a "qemu" | head -3 | sed 's/^/  /'
else
    echo "⚠ No QEMU process found yet"
    echo "  - Controller may not be running"
    echo "  - Or QEMU binary not found on system"
    echo "  - Check: pgrep -a sirah-controller"
fi
echo ""

# Check 7: Check pod log file in final location
echo "[7] Checking pod log file..."
echo "    (Created by Controller when QEMU VM starts)"
POD_LOG_DIR="/tmp/sirah-logs/pods/$NAMESPACE/$POD_NAME"
POD_LOG_FILE="$POD_LOG_DIR/app.log"

if [ -d "$POD_LOG_DIR" ]; then
    echo "✓ Pod log directory exists: $POD_LOG_DIR"
    ls -la "$POD_LOG_DIR/" | sed 's/^/    /'
else
    echo "⚠ Pod log directory not created yet: $POD_LOG_DIR"
    echo "  This means Controller hasn't spawned the QEMU VM yet"
    echo "  Check: ps aux | grep sirah-controller"
fi

if [ -f "$POD_LOG_FILE" ]; then
    LINES=$(wc -l < "$POD_LOG_FILE" 2>/dev/null || echo 0)
    SIZE=$(du -h "$POD_LOG_FILE" | awk '{print $1}')
    echo "✓ Pod log file exists: $POD_LOG_FILE"
    echo "  Size: $SIZE, Lines: $LINES"
    
    if [ "$LINES" -gt 0 ]; then
        echo ""
        echo "  Last 10 lines from pod logs:"
        tail -10 "$POD_LOG_FILE" | sed 's/^/    /'
    else
        echo "  (File exists but empty - QEMU may still be booting)"
    fi
else
    echo "⚠ Pod log file not found: $POD_LOG_FILE"
    echo "  Ensure:"
    echo "    1. Pod Controller is running: pgrep -f sirah-controller"
    echo "    2. QEMU is available on system: which qemu-system-x86_64"
    echo "    3. Valid unikernel image provided: file $TEST_KERNEL"
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
echo "Pod Name:      $POD_NAME"
echo "Namespace:     $NAMESPACE"
echo "VM ID:         $VM_ID"
echo "Log File:      $POD_LOG_FILE"
echo "Log Directory: $POD_LOG_DIR"
echo ""

echo "Troubleshooting:"
echo "  1. Check if Pod Controller is running:"
echo "     ps aux | grep sirah-controller"
echo ""
echo "  2. Check Controller logs:"
echo "     tail -f /tmp/sirah-logs/controller.log"
echo ""
echo "  3. Monitor for QEMU processes:"
echo "     watch 'pgrep -a qemu'"
echo ""
echo "  4. Check if QEMU is installed:"
echo "     which qemu-system-x86_64"
echo ""
echo "  5. Verify log directory creation:"
echo "     ls -la /tmp/sirah-logs/pods/default/"
echo ""

echo "When working:"
echo "  - Monitor logs in real-time:"
echo "    tail -f $POD_LOG_FILE"
echo ""
echo "  - Get logs via API (requires container name in spec):"
echo "    curl -u $ADMIN_USER '$API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_NAME/log'"
echo ""
echo "  - Use kubectl if installed:"
echo "    kubectl logs $POD_NAME"
echo "    kubectl logs -f $POD_NAME (follow mode)"
echo ""

echo "Cleanup:"
echo "  curl -X DELETE -u $ADMIN_USER '$API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_NAME'"
echo ""
echo "  # Remove dynamic kernel copy:"
echo "  rm -f $DYNAMIC_KERNEL"
echo ""

echo "=== Test Log ===" 
echo "This test output was also saved to:"
echo "  tail -f $SCRIPT_LOG"
echo ""
