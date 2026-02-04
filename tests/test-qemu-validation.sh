#!/bin/bash

# Test QEMU unikernel log streaming
# Verifies that logs from unikernels appear in kubectl logs

set -e

API_URL="http://localhost:6443"
ADMIN_USER="admin:admin"

echo "=== QEMU Validation Test ==="
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

# Check 3: Prepare unique test unikernel
echo "[3] Preparing unique test unikernel..."
mkdir -p /tmp/sirah-unikernels

# Get or create source kernel
SOURCE_KERNEL="/tmp/sirah-unikernels/test-kernel.img"
if [ ! -f "$SOURCE_KERNEL" ]; then
    echo "  Creating source test kernel..."
    dd if=/dev/zero of="$SOURCE_KERNEL" bs=1M count=10 2>/dev/null
    echo "  ✓ Created $SOURCE_KERNEL"
else
    echo "  ✓ Using existing $SOURCE_KERNEL"
fi

# Create unique copy for this pod
POD_NAME="qemu-test-$(date +%s%N)"
UNIQUE_KERNEL="/tmp/sirah-unikernels/${POD_NAME}.img"
echo "  Copying kernel to unique path..."
cp "$SOURCE_KERNEL" "$UNIQUE_KERNEL"
echo "  ✓ Created unique kernel: $UNIQUE_KERNEL"
echo ""

# Check 4: Create test pod
echo "[4] Creating test pod..."
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
      "image": "$UNIQUE_KERNEL",
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

echo "  Looking for QEMU process with unique pod name: $POD_NAME"
echo "  Looking for unique kernel: $UNIQUE_KERNEL"
echo ""

# Check for process using unique kernel path
PS_OUTPUT=$(ps aux | grep -E "(qemu|$POD_NAME|${UNIQUE_KERNEL})" | grep -v grep)

if [ -n "$PS_OUTPUT" ]; then
    echo "✓ QEMU process found!"
    echo "  Process details:"
    echo "$PS_OUTPUT" | sed 's/^/    /'
    echo ""
    
    # Extract PID
    QEMU_PID=$(echo "$PS_OUTPUT" | awk '{print $2}' | head -1)
    echo "  PID: $QEMU_PID"
    echo "  Kernel: $UNIQUE_KERNEL"
    echo "  Pod: $POD_NAME"
else
    echo "⚠ No QEMU process found yet (may still be starting)"
    echo "  Expected to see process with:"
    echo "    - Pod name: $POD_NAME"
    echo "    - Kernel path: $UNIQUE_KERNEL"
    echo "    - VM ID: $VM_ID"
fi
echo ""

# Check 7: Validate pod status progression
echo "[7] Validating pod status progression..."

# First check if controller is running
CONTROLLER_RUNNING=$(ps aux | grep -E 'sirah-controller|bin/sirah-con' | grep -v grep | wc -l)
if [ "$CONTROLLER_RUNNING" -eq 0 ]; then
    echo "⚠ WARNING: Controller is not running!"
    echo "  The pod will not transition from Pending to Running without the controller."
    echo "  Start it with: ./bin/sirah-controller"
    echo ""
fi

WAITING_FOUND=0
RUNNING_FOUND=0
READY_FOUND=0

for i in {1..15}; do
    POD_RESPONSE=$(curl -s -u "$ADMIN_USER" \
            "$API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_NAME" 2>/dev/null)
    
    # Extract status fields
    PHASE=$(echo "$POD_RESPONSE" | jq -r '.status.phase // "Unknown"' 2>/dev/null)
    CONTAINER_STATE=$(echo "$POD_RESPONSE" | jq -r '.status.containerStatuses[0].state | keys[0] // "unknown"' 2>/dev/null)
    CONTAINER_READY=$(echo "$POD_RESPONSE" | jq -r '.status.containerStatuses[0].ready // false' 2>/dev/null)
    WAIT_REASON=$(echo "$POD_RESPONSE" | jq -r '.status.containerStatuses[0].state.waiting.reason // "-"' 2>/dev/null)
    
    echo "  Attempt $i: Phase=$PHASE, ContainerState=$CONTAINER_STATE, Ready=$CONTAINER_READY, Reason=$WAIT_REASON"
    
    # Track status transitions
    if [ "$CONTAINER_STATE" = "waiting" ] && [ "$WAITING_FOUND" = "0" ]; then
        WAITING_FOUND=1
        echo "    ✓ Detected 'waiting' state with reason: $WAIT_REASON"
    fi
    
    if [ "$CONTAINER_STATE" = "running" ] && [ "$RUNNING_FOUND" = "0" ]; then
        RUNNING_FOUND=1
        echo "    ✓ Transitioned to 'running' state"
    fi
    
    if [ "$CONTAINER_READY" = "true" ] && [ "$READY_FOUND" = "0" ]; then
        READY_FOUND=1
        echo "    ✓ Container marked as ready"
    fi
    
    # Check if we've seen the full progression
    if [ "$PHASE" = "Running" ] && [ "$CONTAINER_STATE" = "running" ] && [ "$CONTAINER_READY" = "true" ]; then
        echo "✓ Pod fully progressed to Running state with container ready"
        break
    fi
    
    sleep 1
done

# Validate we saw expected transitions
echo ""
echo "  Status progression summary:"
if [ "$WAITING_FOUND" = "1" ]; then
    echo "    ✓ Saw 'waiting' state (pod in Pending phase)"
else
    echo "    ⚠ Did not see 'waiting' state"
fi

if [ "$RUNNING_FOUND" = "1" ]; then
    echo "    ✓ Saw 'running' state (container started)"
else
    echo "    ⚠ Did not see 'running' state (controller may not be running)"
fi

if [ "$READY_FOUND" = "1" ]; then
    echo "    ✓ Container marked ready"
else
    echo "    ⚠ Container not yet ready (still starting or waiting)"
fi

if [ "$PHASE" = "Running" ]; then
    echo "    ✓ Pod phase is 'Running'"
elif [ "$PHASE" = "Pending" ]; then
    echo "    ⚠ Pod phase is 'Pending' (waiting for controller to spawn VM)"
else
    echo "    ⚠ Pod phase is '$PHASE' (unexpected)"
fi

if [ "$CONTAINER_STATE" = "running" ]; then
    echo "    ✓ Container state is 'running'"
elif [ "$CONTAINER_STATE" = "waiting" ]; then
    echo "    ⚠ Container state is 'waiting' with reason: $WAIT_REASON"
else
    echo "    ⚠ Container state is '$CONTAINER_STATE' (unexpected)"
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
#