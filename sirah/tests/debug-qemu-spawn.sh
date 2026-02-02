#!/bin/bash

# Debug script to diagnose why QEMU unikernels are not spawning

set -e

API_URL="http://localhost:6443"
ADMIN_USER="admin:admin"

echo "=== QEMU Spawn Debugging ==="
echo ""

# Check 1: Is API server running?
echo "[1] Checking if API server is running..."
if curl -s -u "$ADMIN_USER" "$API_URL/api/v1/pods" > /dev/null 2>&1; then
    echo "✓ API server is responding"
else
    echo "✗ API server is NOT responding. Start with: sirah-apiserver"
    exit 1
fi
echo ""

# Check 2: Are there any pods?
echo "[2] Checking if pods exist..."
PODS=$(curl -s -u "$ADMIN_USER" "$API_URL/api/v1/pods" | jq '.items | length')
echo "✓ Total pods found: $PODS"

if [ "$PODS" -eq 0 ]; then
    echo "✗ No pods exist. Create one first with:"
    echo "  curl -X POST http://localhost:6443/api/v1/namespaces/default/pods \\"
    echo "    -H 'Content-Type: application/json' \\"
    echo "    -d '{...}'"
    exit 1
fi
echo ""

# Check 3: Check each pod's status and image
echo "[3] Analyzing pods..."
curl -s -u "$ADMIN_USER" "$API_URL/api/v1/pods" | jq -r '.items[] | "\(.metadata.namespace)/\(.metadata.name) | image: \(.spec.containers[0].image) | status: \(.status.phase)"' | while read -r line; do
    echo "  • $line"
done
echo ""

# Check 4: Check image detection
echo "[4] Checking image names for unikernel keywords..."
curl -s -u "$ADMIN_USER" "$API_URL/api/v1/pods" | jq -r '.items[].spec.containers[0].image' | while read -r image; do
    echo "  Image: $image"
    
    # Check for unikernel keywords
    if echo "$image" | grep -iE "unikernel|kernel|\.img|\.bin|vmlinuz|osv|mirage|rumprun|IncludeOS" > /dev/null; then
        echo "    ✓ Matches unikernel keywords"
    elif [[ "$image" == /* ]]; then
        echo "    ✓ Absolute path (treated as unikernel)"
    else
        echo "    ✗ NOT detected as unikernel (controller will skip this)"
    fi
done
echo ""

# Check 5: Check pod phase status
echo "[5] Checking pod phases..."
PHASES=$(curl -s -u "$ADMIN_USER" "$API_URL/api/v1/pods" | jq -r '.items[].status.phase' | sort | uniq -c)
echo "  Phase counts:"
echo "$PHASES" | while read -r count phase; do
    if [ "$phase" = "Pending" ]; then
        echo "    ✓ $count pods in Pending state (these should spawn QEMU)"
    else
        echo "    ✗ $count pods in $phase state (these will NOT spawn)"
    fi
done
echo ""

# Check 6: Check if controller is running
echo "[6] Checking if pod controller is running..."
if pgrep -f "sirah-controller" > /dev/null; then
    CONTROLLER_PID=$(pgrep -f "sirah-controller" | head -1)
    echo "✓ Controller running (PID: $CONTROLLER_PID)"
    
    echo "  Checking for SYNC messages in logs..."
    if [ -f /tmp/sirah-logs/controller.log ]; then
        SYNC_COUNT=$(grep -c "SYNC:" /tmp/sirah-logs/controller.log || echo 0)
        echo "    • SYNC messages: $SYNC_COUNT"
        
        SPAWN_COUNT=$(grep -c "SYNC: Spawning" /tmp/sirah-logs/controller.log || echo 0)
        echo "    • Spawn attempts: $SPAWN_COUNT"
        
        if [ "$SPAWN_COUNT" -gt 0 ]; then
            echo "    ✓ Controller is attempting to spawn VMs"
            echo "    Recent spawn attempts:"
            grep "SYNC: Spawning" /tmp/sirah-logs/controller.log | tail -3 | sed 's/^/      /'
        else
            echo "    ✗ No spawn attempts found"
        fi
    else
        echo "    ✗ Controller log not found at /tmp/sirah-logs/controller.log"
    fi
else
    echo "✗ Controller is NOT running. Start with: sirah-controller"
fi
echo ""

# Check 7: Check for QEMU processes
echo "[7] Checking QEMU processes..."
QEMU_COUNT=$(pgrep -c "qemu-system" 2>/dev/null || echo 0)
echo "  QEMU processes running: $QEMU_COUNT"

if [ "$QEMU_COUNT" -gt 0 ]; then
    echo "  ✓ QEMU VMs are running!"
    pgrep -a "qemu-system" | sed 's/^/    /'
else
    echo "  ✗ No QEMU processes found"
fi
echo ""

echo "=== Summary ==="
echo ""
if [ "$QEMU_COUNT" -gt 0 ]; then
    echo "✓ SUCCESS: QEMU unikernels are spawning!"
else
    echo "✗ PROBLEM: QEMU unikernels are NOT spawning"
    echo ""
    echo "Possible causes:"
    echo "  1. Pod images don't contain unikernel keywords (add '.img' or 'kernel' to name)"
    echo "  2. Pod status is not 'Pending' (already Running or other state)"
    echo "  3. Controller is not running (start sirah-controller)"
    echo "  4. Controller logs have errors (check /tmp/sirah-logs/controller.log)"
    echo ""
    echo "Quick fixes:"
    echo "  • Create pod with image name ending in .img:"
    echo "    image: /tmp/sirah-unikernels/test-kernel.img"
    echo ""
    echo "  • Or use a keyword:"
    echo "    image: my-mirage-kernel"
    echo ""
    echo "  • Or use absolute path:"
    echo "    image: /path/to/unikernel"
fi
echo ""
