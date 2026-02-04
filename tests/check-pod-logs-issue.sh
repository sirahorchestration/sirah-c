#!/bin/bash
# Diagnostic: Check why pod logs are missing

echo "=== Sirah Pod Logs Diagnostic ==="
echo ""

# Check 1: What processes are running?
echo "[1] Checking running Sirah processes..."
APISERVER_PID=$(pgrep -f "sirah-apiserver" | head -1)
CONTROLLER_PID=$(pgrep -f "sirah-controller" | head -1)
SCHEDULER_PID=$(pgrep -f "sirah-scheduler" | head -1)

echo "  API Server:    $([ -n "$APISERVER_PID" ] && echo "✓ PID $APISERVER_PID" || echo "✗ NOT RUNNING")"
echo "  Pod Controller: $([ -n "$CONTROLLER_PID" ] && echo "✓ PID $CONTROLLER_PID" || echo "✗ NOT RUNNING")"
echo "  Scheduler:     $([ -n "$SCHEDULER_PID" ] && echo "✓ PID $SCHEDULER_PID" || echo "✗ NOT RUNNING")"
echo ""

# Check 2: Is etcd running?
echo "[2] Checking etcd..."
if nc -zv localhost 2379 2>&1 | grep -q "succeeded"; then
    echo "  ✓ etcd is running on localhost:2379"
else
    echo "  ✗ etcd is NOT running"
fi
echo ""

# Check 3: Are there any pods in the system?
echo "[3] Checking pods in API server..."
POD_LIST=$(curl -s -u admin:admin 'http://localhost:6443/api/v1/namespaces/default/pods' 2>/dev/null)
POD_COUNT=$(echo "$POD_LIST" | grep -o '"name"' | wc -l)
if [ "$POD_COUNT" -gt 0 ]; then
    echo "  ✓ Found $POD_COUNT pods"
    echo "$POD_LIST" | grep '"name"' | head -3 | sed 's/^/    /'
else
    echo "  ✗ No pods found (or API not responding)"
fi
echo ""

# Check 4: Are there QEMU processes running?
echo "[4] Checking QEMU processes..."
QEMU_COUNT=$(pgrep -f "qemu-system" | wc -l)
if [ "$QEMU_COUNT" -gt 0 ]; then
    echo "  ✓ Found $QEMU_COUNT QEMU processes"
    pgrep -a "qemu-system" | sed 's/^/    /'
else
    echo "  ✗ No QEMU processes running (VMs not spawned)"
fi
echo ""

# Check 5: Check log directories
echo "[5] Checking log storage..."
if [ -d "/tmp/sirah-logs/pods" ]; then
    LOGS=$(find /tmp/sirah-logs/pods -name "*.log" 2>/dev/null | wc -l)
    echo "  ✓ Log directory exists: /tmp/sirah-logs/pods/"
    if [ "$LOGS" -gt 0 ]; then
        echo "  ✓ Found $LOGS log files"
        find /tmp/sirah-logs/pods -name "*.log" | sed 's/^/    /'
    else
        echo "  ⚠ Log directory exists but contains no logs yet"
    fi
else
    echo "  ✗ Log directory does not exist: /tmp/sirah-logs/pods/"
fi
echo ""

# Check 6: Try to get logs via API
echo "[6] Testing log API endpoint..."
if [ "$POD_COUNT" -gt 0 ]; then
    FIRST_POD=$(echo "$POD_LIST" | grep '"name"' -o | head -1)
    if [ -n "$FIRST_POD" ]; then
        POD_NAME=$(echo "$POD_LIST" | jq -r '.items[0].metadata.name' 2>/dev/null)
        if [ -n "$POD_NAME" ] && [ "$POD_NAME" != "null" ]; then
            echo "  Testing with pod: $POD_NAME"
            LOG_RESPONSE=$(curl -s -w "\n%{http_code}" -u admin:admin \
                "http://localhost:6443/api/v1/namespaces/default/pods/$POD_NAME/log" 2>/dev/null)
            HTTP_CODE=$(echo "$LOG_RESPONSE" | tail -1)
            LOG_DATA=$(echo "$LOG_RESPONSE" | head -n -1)
            
            if [ "$HTTP_CODE" = "200" ]; then
                if [ -z "$LOG_DATA" ]; then
                    echo "  ✓ API endpoint works (HTTP 200) but returned no logs"
                else
                    echo "  ✓ API endpoint works (HTTP 200) with logs"
                    echo "$LOG_DATA" | head -3 | sed 's/^/    /'
                fi
            else
                echo "  ✗ API returned HTTP $HTTP_CODE"
            fi
        fi
    fi
fi
echo ""

# Summary and recommendations
echo "=== Diagnosis ==="
echo ""

if [ -z "$CONTROLLER_PID" ]; then
    echo "❌ PRIMARY ISSUE: Pod Controller is NOT running"
    echo ""
    echo "The Pod Controller is the component that:"
    echo "  1. Watches the API server for new pods"
    echo "  2. Spawns QEMU VMs for unikernel pods"
    echo "  3. Creates logs in /tmp/sirah-logs/pods/"
    echo ""
    echo "To fix: Start the pod controller:"
    echo "  cd /mnt/c/projects/sirah-c/sirah"
    echo "  ./bin/sirah-controller 2>&1 | tee /tmp/sirah-logs/controller.log &"
    echo ""
elif [ "$QEMU_COUNT" -eq 0 ]; then
    echo "⚠ Pod Controller is running but no QEMU VMs are spawned"
    echo ""
    echo "Check controller logs:"
    echo "  tail -f /tmp/sirah-logs/controller.log | grep -E 'SYNC|SPAWN|ERROR'"
    echo ""
    echo "Common issues:"
    echo "  1. QEMU not installed: sudo apt install qemu-system-x86"
    echo "  2. Pod images not valid unikernel paths"
    echo "  3. Controller unable to start VMs (permissions?)"
    echo ""
elif [ "$POD_COUNT" -eq 0 ]; then
    echo "✓ All services running but no pods created yet"
    echo ""
    echo "Create a test pod:"
    echo "  curl -X POST -u admin:admin -H 'Content-Type: application/json' \\"
    echo "    'http://localhost:6443/api/v1/namespaces/default/pods' \\"
    echo "    -d '{\"apiVersion\":\"v1\",\"kind\":\"Pod\",\"metadata\":{\"name\":\"test-pod\"},\"spec\":{\"containers\":[{\"name\":\"app\",\"image\":\"/tmp/sirah-unikernels/test-kernel.img\"}]}}'"
    echo ""
else
    echo "✓ Services are running and pods exist"
    echo "⚠ But no logs found in /tmp/sirah-logs/pods/"
    echo ""
    echo "Possible issues:"
    echo "  1. QEMU VMs have not started yet - wait and retry"
    echo "  2. Unikernel images are invalid (not booting)"
    echo "  3. Log capture in QEMU failed (check /tmp/qemu-* files)"
    echo ""
    echo "Check for QEMU output directly:"
    echo "  ls -lh /tmp/qemu-*.log 2>/dev/null || echo 'No QEMU logs found'"
    echo ""
fi
echo ""
