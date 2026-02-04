#!/bin/bash
# scripts/qemu-scheduling-dashboard.sh
# Real-time QEMU scheduling dashboard with detailed pod information

API_URL="http://localhost:6443"
NAMESPACE="${1:-default}"
REFRESH_INTERVAL="${2:-5}"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m'

clear_screen() {
    clear
}

show_dashboard() {
    clear_screen
    
    echo -e "${CYAN}"
    cat << 'EOF'
╔══════════════════════════════════════════════════════════════════════════════╗
║                  QEMU UNIKERNEL SCHEDULING DASHBOARD                         ║
║                      Real-Time Monitoring                                    ║
╚══════════════════════════════════════════════════════════════════════════════╝
EOF
    echo -e "${NC}"
    
    # Timestamp
    echo "Last Updated: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "Namespace: $NAMESPACE"
    echo "Refresh Rate: Every ${REFRESH_INTERVAL}s (Ctrl+C to exit)"
    echo ""
    
    # Cluster Health
    if curl -s "$API_URL/healthz" | grep -q "ok"; then
        echo -e "${GREEN}✓ Cluster Status: HEALTHY${NC}"
    else
        echo -e "${RED}✗ Cluster Status: UNHEALTHY${NC}"
        return 1
    fi
    
    # Node Status
    echo ""
    echo -e "${BLUE}═══ QEMU NODES ═══${NC}"
    NODES=$(curl -s -X GET "$API_URL/api/v1/nodes" 2>/dev/null)
    
    echo "$NODES" | python3 << 'PYTHON_NODES'
import sys, json
try:
    nodes = json.load(sys.stdin).get('items', [])
    if not nodes:
        print("  No nodes available")
    else:
        for node in nodes:
            name = node.get('name', 'unknown')
            ready = 'Unknown'
            conditions = node.get('status', {}).get('conditions', [])
            for cond in conditions:
                if cond.get('type') == 'Ready':
                    ready = cond.get('status', 'Unknown')
            
            status_symbol = '\033[0;32m✓\033[0m' if ready == 'True' else '\033[0;31m✗\033[0m'
            print(f"  {status_symbol} {name:35} ({ready})")
except:
    pass
PYTHON_NODES
    
    # Pod Statistics
    echo ""
    echo -e "${BLUE}═══ POD DISTRIBUTION ═══${NC}"
    
    PODS=$(curl -s -X GET "$API_URL/api/v1/namespaces/$NAMESPACE/pods" 2>/dev/null)
    POD_COUNT=$(echo "$PODS" | python3 -c "import sys, json; print(len(json.load(sys.stdin).get('items', [])))" 2>/dev/null)
    
    RUNNING=$(echo "$PODS" | python3 -c "import sys, json; print(sum(1 for p in json.load(sys.stdin).get('items', []) if p.get('status', {}).get('phase') == 'Running'))" 2>/dev/null)
    PENDING=$(echo "$PODS" | python3 -c "import sys, json; print(sum(1 for p in json.load(sys.stdin).get('items', []) if p.get('status', {}).get('phase') == 'Pending'))" 2>/dev/null)
    SCHEDULED=$(echo "$PODS" | python3 -c "import sys, json; print(sum(1 for p in json.load(sys.stdin).get('items', []) if p.get('spec', {}).get('nodeName')))" 2>/dev/null)
    
    echo "  Total:      $POD_COUNT"
    echo -e "  ${GREEN}Running:    $RUNNING${NC}"
    echo -e "  ${YELLOW}Pending:    $PENDING${NC}"
    echo -e "  ${CYAN}Scheduled:  $SCHEDULED${NC}"
    
    # Detailed Pod Table
    echo ""
    echo -e "${BLUE}═══ UNIKERNEL PODS ═══${NC}"
    
    if [ "$POD_COUNT" -eq 0 ]; then
        echo "  No pods in namespace '$NAMESPACE'"
    else
        echo ""
        echo -e "${MAGENTA}│ POD NAME                    │ STATUS    │ NODE          │ PHASE        │${NC}"
        echo -e "${MAGENTA}├─────────────────────────────┼───────────┼───────────────┼──────────────┤${NC}"
        
        echo "$PODS" | python3 << 'PYTHON_PODS'
import sys, json
import time

try:
    pods = json.load(sys.stdin).get('items', [])
    pods.sort(key=lambda p: p.get('name', ''))
    
    for pod in pods:
        name = pod.get('name', 'unknown')[:25].ljust(25)
        
        # Status indicator
        phase = pod.get('status', {}).get('phase', 'Unknown')
        if phase == 'Running':
            status = '\033[0;32m✓ Running\033[0m'
        elif phase == 'Pending':
            status = '\033[1;33m⟳ Pending\033[0m'
        elif phase == 'Succeeded':
            status = '\033[0;32m✓ Succe\033[0m'
        else:
            status = f'? {phase[:6]}'
        
        node = pod.get('spec', {}).get('nodeName', 'unscheduled')[:13].ljust(13)
        
        # Ready status
        ready = pod.get('status', {}).get('conditions', [])
        ready_count = sum(1 for c in ready if c.get('type') == 'Ready' and c.get('status') == 'True')
        container_count = len(pod.get('spec', {}).get('containers', []))
        ready_status = f"{ready_count}/{container_count}".ljust(12)
        
        print(f"\033[0;35m│\033[0m {name} \033[0;35m│\033[0m {status:20} \033[0;35m│\033[0m {node} \033[0;35m│\033[0m {ready_status} \033[0;35m│\033[0m")
except:
    pass
PYTHON_PODS
        
        echo -e "${MAGENTA}└─────────────────────────────┴───────────┴───────────────┴──────────────┘${NC}"
    fi
    
    # Resource Usage
    echo ""
    echo -e "${BLUE}═══ QEMU PROCESS STATUS ═══${NC}"
    QEMU_COUNT=$(pgrep -f "qemu" 2>/dev/null | wc -l)
    
    if [ "$QEMU_COUNT" -gt 0 ]; then
        echo -e "  ${GREEN}✓ Active QEMU Instances: $QEMU_COUNT${NC}"
        echo ""
        echo "  Process Details:"
        pgrep -f "qemu" 2>/dev/null | while read pid; do
            PROC_NAME=$(ps -p "$pid" -o comm= 2>/dev/null || echo "unknown")
            PROC_CMD=$(ps -p "$pid" -o args= 2>/dev/null | cut -c1-60)
            echo "    PID: $pid ($PROC_NAME)"
        done | head -5
    else
        echo -e "  ${YELLOW}○ No QEMU processes running${NC}"
    fi
    
    # Quick Links
    echo ""
    echo -e "${BLUE}═══ QUICK COMMANDS ═══${NC}"
    echo "  View Pod Details:    curl http://localhost:6443/api/v1/namespaces/$NAMESPACE/pods/{pod-name}"
    echo "  Get Pod Logs:        kubectl logs -n $NAMESPACE {pod-name}"
    echo "  Exec into Pod:       kubectl exec -n $NAMESPACE -it {pod-name} -- /bin/sh"
    echo "  Delete Pod:          kubectl delete pod -n $NAMESPACE {pod-name}"
    echo "  Describe Pod:        curl http://localhost:6443/api/v1/namespaces/$NAMESPACE/pods/{pod-name}"
    
    echo ""
    echo -e "${CYAN}Next refresh in ${REFRESH_INTERVAL}s... (Ctrl+C to exit)${NC}"
}

# Main loop
while true; do
    show_dashboard
    sleep "$REFRESH_INTERVAL"
done
