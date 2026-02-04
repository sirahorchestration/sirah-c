#!/bin/bash
# Quick test: Check if Pod Controller is the missing piece

echo "=== Quick Pod Logs Diagnostic ==="
echo ""

# Check 1: Is API Server running?
echo "[1] Is API Server running?"
if pgrep -f "sirah-apiserver" > /dev/null; then
    echo "    ✓ YES - API Server is running"
    APISERVER=1
else
    echo "    ✗ NO - API Server is NOT running"
    APISERVER=0
fi
echo ""

# Check 2: Is Pod Controller running?
echo "[2] Is Pod Controller running?"
if pgrep -f "sirah-controller" > /dev/null; then
    echo "    ✓ YES - Pod Controller is running"
    CONTROLLER=1
else
    echo "    ✗ NO - Pod Controller is NOT running (THIS IS THE PROBLEM!)"
    CONTROLLER=0
fi
echo ""

# Check 3: Are there any QEMU VMs running?
echo "[3] Are there any QEMU VMs running?"
QEMU_COUNT=$(pgrep -f "qemu-system" | wc -l)
if [ "$QEMU_COUNT" -gt 0 ]; then
    echo "    ✓ YES - $QEMU_COUNT QEMU VM(s) running"
else
    echo "    ✗ NO - No QEMU VMs (expected if Controller not running)"
fi
echo ""

# Check 4: Are there any logs?
echo "[4] Are there any pod logs in /tmp/sirah-logs/pods/?"
if [ -d "/tmp/sirah-logs/pods" ]; then
    LOG_COUNT=$(find /tmp/sirah-logs/pods -name "*.log" 2>/dev/null | wc -l)
    if [ "$LOG_COUNT" -gt 0 ]; then
        echo "    ✓ YES - $LOG_COUNT log file(s) found"
        find /tmp/sirah-logs/pods -name "*.log" | head -5 | sed 's/^/      /'
    else
        echo "    ✗ NO - Directory exists but contains no log files"
    fi
else
    echo "    ✗ NO - Log directory doesn't exist yet"
fi
echo ""

# Summary
echo "=== DIAGNOSIS ==="
echo ""

if [ "$APISERVER" -eq 1 ] && [ "$CONTROLLER" -eq 0 ]; then
    echo "✅ FOUND THE ISSUE:"
    echo ""
    echo "  • API Server is running ✓"
    echo "  • Pod Controller is NOT running ✗"
    echo ""
    echo "  This is why pod logs are empty!"
    echo ""
    echo "  The Pod Controller is the component that:"
    echo "    1. Fetches pods from the API"
    echo "    2. Spawns QEMU VMs for each pod"
    echo "    3. Captures VM output to log files"
    echo ""
    echo "  Without it, pods are created in etcd but never executed."
    echo ""
    echo "FIX: Start the Pod Controller"
    echo "  cd /mnt/c/projects/sirah-c/sirah"
    echo "  ./bin/sirah-controller 2>&1 | tee /tmp/sirah-logs/controller.log &"
    echo ""
elif [ "$APISERVER" -eq 0 ]; then
    echo "❌ API Server is not running"
    echo ""
    echo "Start it with:"
    echo "  cd /mnt/c/projects/sirah-c/sirah"
    echo "  ./bin/sirah-apiserver --etcd http://localhost:2379 &"
    echo ""
elif [ "$CONTROLLER" -eq 1 ] && [ "$QEMU_COUNT" -eq 0 ]; then
    echo "⚠️ Pod Controller is running but no QEMU VMs"
    echo ""
    echo "This could mean:"
    echo "  1. No pods have been created yet"
    echo "  2. QEMU is not installed (run: sudo apt install qemu-system-x86)"
    echo "  3. Pod images are invalid unikernel paths"
    echo ""
    echo "Check the controller logs:"
    echo "  tail -f /tmp/sirah-logs/controller.log"
    echo ""
else
    echo "✓ Services appear to be configured"
    echo ""
    echo "If logs are still missing, check:"
    echo "  1. Unikernel image path is valid:"
    echo "     ls -l /tmp/sirah-unikernels/test-kernel.img"
    echo "  2. Controller logs for errors:"
    echo "     tail -50 /tmp/sirah-logs/controller.log"
    echo ""
fi
