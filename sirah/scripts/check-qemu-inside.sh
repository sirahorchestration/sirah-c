#!/bin/bash

# Check what's running INSIDE a QEMU instance
# Shows: QEMU process, kernel/unikernel running, console output

set -e

NAMESPACE="${1:-default}"
POD_NAME="${2:-}"
API_URL="${API_URL:-http://localhost:6443}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
RESET='\033[0m'

if [ -z "$POD_NAME" ]; then
    echo -e "${RED}❌ Error: Pod name required${RESET}"
    echo "Usage: $0 <namespace> <pod-name>"
    echo "Example: $0 default my-pod"
    exit 1
fi

echo -e "${MAGENTA}╔════════════════════════════════════════════════════════════════╗${RESET}"
echo -e "${MAGENTA}║   QEMU INTERNAL UNIKERNEL MONITOR - Inspect Inside QEMU        ║${RESET}"
echo -e "${MAGENTA}╚════════════════════════════════════════════════════════════════╝${RESET}"
echo

# Get pod details
echo -e "${CYAN}🔍 Fetching pod information...${RESET}"
POD_JSON=$(curl -s "${API_URL}/api/v1/namespaces/${NAMESPACE}/pods/${POD_NAME}")

if echo "$POD_JSON" | grep -q '"kind":"Status"'; then
    echo -e "${RED}❌ Pod not found: ${POD_NAME}${RESET}"
    exit 1
fi

# Extract node name
NODE_NAME=$(echo "$POD_JSON" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data.get('spec', {}).get('nodeName', 'unscheduled'))" 2>/dev/null || echo "unknown")

if [ "$NODE_NAME" = "unscheduled" ] || [ "$NODE_NAME" = "unknown" ]; then
    echo -e "${YELLOW}⚠ Pod not scheduled to any node${RESET}"
    exit 1
fi

# Get runtime class
RUNTIME_CLASS=$(echo "$POD_JSON" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data.get('spec', {}).get('runtimeClassName', 'default'))" 2>/dev/null || echo "default")

# Get container image
CONTAINER_IMAGE=$(echo "$POD_JSON" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data.get('spec', {}).get('containers', [{}])[0].get('image', 'unknown'))" 2>/dev/null || echo "unknown")

# Get pod status phase
POD_PHASE=$(echo "$POD_JSON" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data.get('status', {}).get('phase', 'Unknown'))" 2>/dev/null || echo "Unknown")

echo -e "${GREEN}✓ Found pod: ${POD_NAME}${RESET}"
echo

echo -e "${BLUE}═══ POD INFORMATION ═══${RESET}"
echo "Pod Name:         $POD_NAME"
echo "Namespace:        $NAMESPACE"
echo "Node:             $NODE_NAME"
echo "Status:           $POD_PHASE"
echo "Runtime Class:    $RUNTIME_CLASS"
echo "Container Image:  $CONTAINER_IMAGE"
echo

# Find QEMU processes on the node
echo -e "${BLUE}═══ QEMU PROCESSES ═══${RESET}"
echo "🔍 Looking for QEMU processes associated with this pod..."
echo

# Get all QEMU processes
QEMU_PROCESSES=$(ps aux 2>/dev/null | grep -E 'qemu|kvm' | grep -v grep | head -20)

if [ -z "$QEMU_PROCESSES" ]; then
    echo -e "${YELLOW}⚠ No QEMU processes found on local system${RESET}"
    echo "Note: This tool checks local machine. If running on different host:"
    echo "  - SSH to node: ssh $NODE_NAME"
    echo "  - Then run: ps aux | grep qemu"
else
    echo "$QEMU_PROCESSES" | while IFS= read -r line; do
        # Extract just the important parts
        PROCESS=$(echo "$line" | awk '{print $11, $12, $13, $14, $15}')
        PID=$(echo "$line" | awk '{print $2}')
        
        echo -e "${GREEN}✓ QEMU Process (PID: $PID)${RESET}"
        echo "   Command: $PROCESS"
        
        # Check if this QEMU is related to our pod by looking at image name
        if echo "$PROCESS" | grep -q "$(basename $CONTAINER_IMAGE)"; then
            echo -e "   ${GREEN}↳ LIKELY RELATED TO THIS POD${RESET}"
        fi
        echo
    done
fi

echo -e "${BLUE}═══ UNIKERNEL STATUS ═══${RESET}"

# Check container status
CONTAINER_READY=$(echo "$POD_JSON" | python3 -c "import sys, json; data=json.load(sys.stdin); cs=data.get('status', {}).get('containerStatuses', [{}])[0]; print('true' if cs.get('ready') else 'false')" 2>/dev/null || echo "unknown")

if [ "$CONTAINER_READY" = "true" ]; then
    echo -e "${GREEN}✓ Container is READY${RESET}"
    echo "  This means the unikernel successfully started in QEMU"
else
    echo -e "${YELLOW}⚠ Container is NOT READY${RESET}"
    echo "  Unikernel may still be starting or encountered an issue"
fi

echo

# Container state
CONTAINER_STATE=$(echo "$POD_JSON" | python3 -c "
import sys, json
data = json.load(sys.stdin)
cs = data.get('status', {}).get('containerStatuses', [{}])[0]
state = cs.get('state', {})
if 'running' in state:
    print('RUNNING - Unikernel is executing')
elif 'waiting' in state:
    reason = state['waiting'].get('reason', 'unknown')
    msg = state['waiting'].get('message', '')
    print(f'WAITING - {reason}: {msg}')
elif 'terminated' in state:
    reason = state['terminated'].get('reason', 'unknown')
    print(f'TERMINATED - {reason}')
else:
    print('UNKNOWN state')
" 2>/dev/null || echo "UNKNOWN"
)

if echo "$CONTAINER_STATE" | grep -q "RUNNING"; then
    echo -e "${GREEN}✓ $CONTAINER_STATE${RESET}"
elif echo "$CONTAINER_STATE" | grep -q "WAITING"; then
    echo -e "${YELLOW}⟳ $CONTAINER_STATE${RESET}"
elif echo "$CONTAINER_STATE" | grep -q "TERMINATED"; then
    echo -e "${RED}✗ $CONTAINER_STATE${RESET}"
else
    echo -e "${YELLOW}? $CONTAINER_STATE${RESET}"
fi

echo

echo -e "${BLUE}═══ HOW TO VERIFY UNIKERNEL INSIDE QEMU ═══${RESET}"
echo
echo "1️⃣  Check QEMU Serial Console Output:"
echo "   If QEMU has a serial port connected, unikernel output appears there"
echo "   Look for kernel boot messages"
echo
echo "2️⃣  Use QEMU Monitor Commands:"
echo "   connect to QEMU monitor to check running processes"
echo "   (requires QEMU monitor enabled)"
echo
echo "3️⃣  Check Pod Logs:"
echo "   kubectl logs -n $NAMESPACE $POD_NAME"
echo "   May show unikernel startup messages"
echo
echo "4️⃣  Inspect QEMU Process Details:"
if [ ! -z "$QEMU_PROCESSES" ]; then
    FIRST_PID=$(echo "$QEMU_PROCESSES" | head -1 | awk '{print $2}')
    echo "   ps -eaf | grep qemu      # See all QEMU processes"
    echo "   ps aux | grep $FIRST_PID   # See full command line"
    echo "   cat /proc/$FIRST_PID/cmdline | tr '\0' ' '  # See QEMU boot args"
fi
echo
echo "5️⃣  Check QEMU Runtime Features:"
echo "   - KVM enabled: Check QEMU process for '-enable-kvm'"
echo "   - CPU flags: Check '-m' (memory) and '-smp' (CPUs) arguments"
echo "   - Disk: Look for '-drive' or '-hda' for rootfs"
echo

echo -e "${BLUE}═══ COMMON UNIKERNEL INDICATORS ═══${RESET}"
echo
echo "Signs the unikernel is running:"
echo "  ✓ Pod status is Running"
echo "  ✓ Container status shows 'running' state"
echo "  ✓ Container ready flag is true"
echo "  ✓ QEMU process is present in 'ps' output"
echo "  ✓ No restart count (or expected restart count)"
echo
echo "Signs the unikernel is NOT running:"
echo "  ✗ Pod status is Pending or Failed"
echo "  ✗ Container state is 'waiting' or 'terminated'"
echo "  ✗ QEMU process is not in 'ps' output"
echo "  ✗ Restart count is increasing"
echo "  ✗ Error messages in container status"
echo

# Get restart count
RESTART_COUNT=$(echo "$POD_JSON" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data.get('status', {}).get('containerStatuses', [{}])[0].get('restartCount', 0))" 2>/dev/null || echo "0")

echo -e "${BLUE}═══ RESTART STATUS ═══${RESET}"
echo "Restart Count: $RESTART_COUNT"
if [ "$RESTART_COUNT" = "0" ]; then
    echo -e "${GREEN}✓ No restarts (healthy)${RESET}"
else
    echo -e "${YELLOW}⚠ Container restarted $RESTART_COUNT time(s)${RESET}"
    echo "   Check logs for startup issues: kubectl logs -n $NAMESPACE $POD_NAME"
fi
echo

echo -e "${BLUE}═══ SUMMARY ═══${RESET}"
if [ "$CONTAINER_READY" = "true" ] && echo "$CONTAINER_STATE" | grep -q "RUNNING"; then
    echo -e "${GREEN}✅ UNIKERNEL IS RUNNING INSIDE QEMU${RESET}"
    echo "    Pod: $POD_NAME"
    echo "    Image: $CONTAINER_IMAGE"
    echo "    Node: $NODE_NAME"
    echo "    Status: $POD_PHASE (Ready)"
else
    echo -e "${YELLOW}⚠ UNIKERNEL STATUS UNCLEAR${RESET}"
    echo "    Pod Status: $POD_PHASE"
    echo "    Container Ready: $CONTAINER_READY"
    echo "    Container State: $(echo "$CONTAINER_STATE" | head -1)"
    echo
    echo "    Next steps:"
    echo "    1. Check pod logs: kubectl logs -n $NAMESPACE $POD_NAME"
    echo "    2. Describe pod: kubectl describe pod -n $NAMESPACE $POD_NAME"
    echo "    3. Check node: kubectl describe node $NODE_NAME"
fi
echo
