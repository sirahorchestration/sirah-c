#!/bin/bash
# scripts/view-qemu-pods.sh
# Visual display of QEMU-scheduled unikernels and their status

set -e

API_URL="http://localhost:6443"
NAMESPACE="${1:-default}"

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo ""
echo "╔════════════════════════════════════════════════════════════════╗"
echo "║        QEMU UNIKERNEL SCHEDULING STATUS MONITOR               ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""

# Check if cluster is running
if ! curl -s "$API_URL/healthz" | grep -q "ok"; then
    echo -e "${RED}✗${NC} Cluster is not responding"
    echo "  Start cluster: bash start-cluster-full.sh"
    exit 1
fi

echo -e "${GREEN}✓${NC} Cluster is healthy"
echo ""

# Get available nodes
echo "📊 QEMU Nodes Available:"
NODES=$(curl -s -X GET "$API_URL/api/v1/nodes")
NODE_COUNT=$(echo "$NODES" | python3 -c "import sys, json; print(len(json.load(sys.stdin).get('items', [])))" 2>/dev/null)

if [ "$NODE_COUNT" -gt 0 ]; then
    python3 << PYTHON_SCRIPT
import sys, json
nodes_json = """$NODES"""
try:
    nodes = json.loads(nodes_json).get('items', [])
    for node in nodes:
        name = node.get('metadata', {}).get('name', 'unknown')
        status = node.get('status', {})
        node_status = status.get('status', 'Unknown')
        ready_symbol = '✓' if node_status == 'Ready' else '✗'
        print(f"  {ready_symbol} {name:30} (Status: {node_status})")
except:
    pass
PYTHON_SCRIPT
else
    echo -e "  ${RED}No nodes available${NC}"
fi

echo ""
echo "📦 QEMU-Scheduled Unikernels in '$NAMESPACE' Namespace:"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Get pods
PODS=$(curl -s -X GET "$API_URL/api/v1/namespaces/$NAMESPACE/pods")

POD_COUNT=$(echo "$PODS" | python3 -c "import sys, json; print(len(json.load(sys.stdin).get('items', [])))" 2>/dev/null)

if [ "$POD_COUNT" -eq 0 ]; then
    echo -e "  ${YELLOW}No pods in namespace '$NAMESPACE'${NC}"
    echo ""
    exit 0
fi

# Display pods with QEMU scheduling info
python3 << PYTHON_SCRIPT
import sys, json
pods_json = """$PODS"""
try:
    pods = json.loads(pods_json).get('items', [])
    
    if not pods:
        print("  No pods found")
        sys.exit(0)
    
    # Sort by name for consistent display
    pods.sort(key=lambda p: p.get('metadata', {}).get('name', ''))
    
    # Column headers
    print(f"{'Pod Name':<30} {'Status':<12} {'Node':<15} {'QEMU':<8}")
    print("─" * 75)
    
    for pod in pods:
        name = pod.get('metadata', {}).get('name', 'unknown')
        
        # Get status phase
        phase = pod.get('status', {}).get('phase', 'Unknown')
        phase_symbol = '●'
        if phase == 'Running':
            phase_color = '\033[0;32m'  # Green
            phase_symbol = '●'
        elif phase == 'Pending':
            phase_color = '\033[1;33m'  # Yellow
            phase_symbol = '◐'
        elif phase == 'Succeeded':
            phase_color = '\033[0;32m'  # Green
            phase_symbol = '✓'
        elif phase == 'Failed':
            phase_color = '\033[0;31m'  # Red
            phase_symbol = '✗'
        else:
            phase_color = '\033[0;34m'  # Blue
            phase_symbol = '?'
        
        phase_display = f"{phase_color}{phase_symbol}{'\033[0m'} {phase:<10}"
        
        # Get node name (indicates scheduling)
        node = pod.get('spec', {}).get('nodeName', 'unscheduled')
        if not node:
            node = 'unscheduled'
        
        # Check for QEMU indicators
        labels = pod.get('metadata', {}).get('labels', {})
        runtime = labels.get('runtime', '')
        
        status = pod.get('status', {})
        container_runtime = status.get('containerRuntime', '')
        
        spec = pod.get('spec', {})
        runtime_class = spec.get('runtimeClassName', '')
        
        is_qemu = False
        qemu_indicator = ''
        
        if runtime == 'qemu' or 'qemu' in str(container_runtime).lower() or runtime_class == 'qemu':
            is_qemu = True
            qemu_indicator = '✓ QEMU'
        elif node != 'unscheduled':
            # If scheduled to a node, assume QEMU (as that's our only runtime)
            is_qemu = True
            qemu_indicator = '✓ QEMU'
        else:
            qemu_indicator = '○'
        
        # Format node name
        if node == 'unscheduled':
            node_display = f"\033[1;33m{node:<15}\033[0m"
        else:
            node_display = f"\033[0;32m{node:<15}\033[0m"
        
        print(f"{name:<30} {phase_display} {node_display} {qemu_indicator:<8}")
    
    print("─" * 75)
    print("")
    print("Legend:")
    print("  ● Running  •  ◐ Pending  •  ✓ Succeeded  •  ✗ Failed")
    print("  ○ Not QEMU  •  ✓ QEMU Scheduled")
    
except Exception as e:
    print(f"Error: {e}")
    import traceback
    traceback.print_exc()
PYTHON_SCRIPT

echo ""

# Show QEMU process count
echo "🚀 Running QEMU Processes:"
QEMU_PROCS=$(pgrep -f "qemu" 2>/dev/null | wc -l)
if [ "$QEMU_PROCS" -gt 0 ]; then
    echo -e "  ${GREEN}✓${NC} $QEMU_PROCS QEMU process(es) running"
else
    echo -e "  ${YELLOW}○${NC} No QEMU processes currently running"
fi

echo ""

# Show summary statistics
echo "📈 Summary:"
SCHEDULED_COUNT=$(echo "$PODS" | python3 -c "
import sys, json
try:
    pods = json.load(sys.stdin).get('items', [])
    scheduled = sum(1 for p in pods if p.get('spec', {}).get('nodeName'))
    print(scheduled)
except:
    print(0)
" 2>/dev/null)

RUNNING_COUNT=$(echo "$PODS" | python3 -c "
import sys, json
try:
    pods = json.load(sys.stdin).get('items', [])
    running = sum(1 for p in pods if p.get('status', {}).get('phase') == 'Running')
    print(running)
except:
    print(0)
" 2>/dev/null)

PENDING_COUNT=$(echo "$PODS" | python3 -c "
import sys, json
try:
    pods = json.load(sys.stdin).get('items', [])
    pending = sum(1 for p in pods if p.get('status', {}).get('phase') == 'Pending')
    print(pending)
except:
    print(0)
" 2>/dev/null)

echo "  Total Pods:        $POD_COUNT"
echo "  Scheduled:         $SCHEDULED_COUNT (to QEMU nodes)"
echo "  Running:           $RUNNING_COUNT"
echo "  Pending:           $PENDING_COUNT"

echo ""
echo "💡 Tips:"
echo "  • View specific pod:  curl http://localhost:6443/api/v1/namespaces/$NAMESPACE/pods/{pod-name}"
echo "  • Watch live:         watch -n 1 'bash $0 $NAMESPACE'"
echo "  • Refresh:            Just run this script again"

echo ""
