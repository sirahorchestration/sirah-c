#!/bin/bash

# Monitor QEMU unikernel execution
# Shows if unikernel is actually running inside QEMU VMs

set -e

echo "=== QEMU Unikernel Monitoring ==="
echo ""

# Check 1: List QEMU processes
echo "[1] QEMU Processes"
echo "---"
QEMU_COUNT=$(pgrep -c "qemu-system" 2>/dev/null || echo 0)
echo "Total QEMU processes: $QEMU_COUNT"
echo ""

if [ "$QEMU_COUNT" -gt 0 ]; then
    echo "Running VMs:"
    pgrep -a "qemu-system" | while read -r pid cmd; do
        echo "  PID: $pid"
        echo "    CMD: $cmd"
        
        # Extract VM name from command
        VM_NAME=$(echo "$cmd" | grep -oP '(?<=-name\s)\S+' || echo "unknown")
        echo "    Name: $VM_NAME"
        
        # Check process memory usage
        MEMORY=$(ps -p "$pid" -o rss= | awk '{print $1/1024 "MB"}')
        echo "    Memory: $MEMORY"
        echo ""
    done
fi
echo ""

# Check 2: Check QEMU log files
echo "[2] QEMU Log Files"
echo "---"
LOG_DIR="/tmp"
QEMU_LOGS=$(find "$LOG_DIR" -name "qemu-*.log" -mmin -5 2>/dev/null | head -10)

if [ -z "$QEMU_LOGS" ]; then
    echo "No recent QEMU log files found"
else
    while read -r log_file; do
        echo "Log: $log_file"
        LINES=$(wc -l < "$log_file" 2>/dev/null || echo 0)
        echo "  Lines: $LINES"
        echo "  Last output:"
        tail -5 "$log_file" | sed 's/^/    /'
        echo ""
    done <<< "$QEMU_LOGS"
fi
echo ""

# Check 3: Check pod status in API
echo "[3] Pod Status in Kubernetes API"
echo "---"
API_URL="http://localhost:6443"
ADMIN_USER="admin:admin"

PODS=$(curl -s -u "$ADMIN_USER" "$API_URL/api/v1/pods" 2>/dev/null | jq -r '.items[] | "\(.metadata.namespace)/\(.metadata.name)"' 2>/dev/null || echo "")

if [ -z "$PODS" ]; then
    echo "No pods found"
else
    while read -r pod_ref; do
        echo "Pod: $pod_ref"
        NS=$(echo "$pod_ref" | cut -d'/' -f1)
        POD=$(echo "$pod_ref" | cut -d'/' -f2)
        
        # Get pod details
        POD_DATA=$(curl -s -u "$ADMIN_USER" "$API_URL/api/v1/namespaces/$NS/pods/$POD" 2>/dev/null)
        
        PHASE=$(echo "$POD_DATA" | jq -r '.status.phase' 2>/dev/null || echo "Unknown")
        IMAGE=$(echo "$POD_DATA" | jq -r '.spec.containers[0].image' 2>/dev/null || echo "Unknown")
        
        echo "  Phase: $PHASE"
        echo "  Image: $IMAGE"
        
        # Check container status
        CONTAINER_READY=$(echo "$POD_DATA" | jq -r '.status.containerStatuses[0].ready' 2>/dev/null || echo "unknown")
        echo "  Container Ready: $CONTAINER_READY"
        
        # Get memory and CPU requests
        MEMORY=$(echo "$POD_DATA" | jq -r '.spec.containers[0].resources.limits.memory' 2>/dev/null || echo "Not set")
        CPU=$(echo "$POD_DATA" | jq -r '.spec.containers[0].resources.limits.cpu' 2>/dev/null || echo "Not set")
        echo "  Requested Memory: $MEMORY"
        echo "  Requested CPU: $CPU"
        echo ""
    done <<< "$PODS"
fi
echo ""

# Check 4: Controller activity
echo "[4] Controller Activity (Last 20 lines)"
echo "---"
if [ -f /tmp/sirah-logs/controller.log ]; then
    tail -20 /tmp/sirah-logs/controller.log | sed 's/^/  /'
else
    echo "Controller log not found"
fi
echo ""

# Check 5: QEMU process details
echo "[5] Detailed QEMU Process Information"
echo "---"
ps aux | grep "qemu-system" | grep -v grep | while read -r line; do
    echo "$line" | awk '{
        print "  PID: " $2
        print "  USER: " $1
        print "  CPU%: " $3
        print "  MEM%: " $4
        print "  RSS: " $6 " KB"
        print "  Full Command:"
        for(i=11;i<=NF;i++) printf "    %s ", $i; print ""
    }'
    echo ""
done
echo ""

# Check 6: Network connections
echo "[6] QEMU Network Connections"
echo "---"
QEMU_PIDS=$(pgrep -f "qemu-system" 2>/dev/null || echo "")
if [ -z "$QEMU_PIDS" ]; then
    echo "No QEMU processes found"
else
    while read -r pid; do
        echo "QEMU PID $pid:"
        ss -tlpn 2>/dev/null | grep "$pid" || echo "  No network connections"
        echo ""
    done <<< "$QEMU_PIDS"
fi
echo ""

# Check 7: System resources
echo "[7] System Resources"
echo "---"
echo "CPU Usage:"
top -bn1 | grep "Cpu(s)" | sed 's/^/  /'
echo ""
echo "Memory Usage:"
free -h | sed 's/^/  /'
echo ""
echo "Disk Space:"
df -h /tmp | sed 's/^/  /'
echo ""

# Summary
echo "=== Summary ==="
echo ""
if [ "$QEMU_COUNT" -gt 0 ]; then
    echo "✓ QEMU VMs are running"
    echo ""
    echo "To see unikernel output:"
    echo "  1. Find QEMU log files in /tmp/qemu-*.log"
    echo "  2. Monitor with: tail -f /tmp/qemu-<vm-name>.log"
    echo ""
    echo "To check if unikernel is responsive:"
    echo "  1. Inside QEMU, look for kernel boot messages"
    echo "  2. Check for application startup logs"
    echo ""
    echo "To verify pod status:"
    echo "  curl -s -u admin:admin http://localhost:6443/api/v1/namespaces/default/pods | jq '.items[] | {name: .metadata.name, phase: .status.phase, ready: .status.containerStatuses[0].ready}'"
else
    echo "✗ No QEMU processes found"
    echo ""
    echo "Possible issues:"
    echo "  1. Pods not in Pending status"
    echo "  2. Controller not running or not detecting pods"
    echo "  3. QEMU spawn failed - check controller logs"
fi
echo ""
