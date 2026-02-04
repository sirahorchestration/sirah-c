#!/bin/bash
# scripts/inspect-qemu-pod.sh
# Detailed inspection of a specific pod's QEMU scheduling information

API_URL="http://localhost:6443"
NAMESPACE="${1:-default}"
POD_NAME="$2"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m'

if [ -z "$POD_NAME" ]; then
    echo "Usage: $0 [namespace] [pod-name]"
    echo ""
    echo "Example: $0 default test-pod"
    exit 1
fi

echo ""
echo "╔════════════════════════════════════════════════════════════════╗"
echo "║        POD QEMU SCHEDULING DETAILS - $POD_NAME"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""

# Fetch pod data
POD=$(curl -s -X GET "$API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_NAME" 2>/dev/null)

if [ -z "$POD" ]; then
    echo -e "${RED}✗ Pod '$POD_NAME' not found in namespace '$NAMESPACE'${NC}"
    exit 1
fi

echo "$POD" | python3 << 'PYTHON_INSPECT'
import sys, json

try:
    pod = json.load(sys.stdin)
    
    print("\033[0;34m═══ METADATA ═══\033[0m")
    metadata = pod.get('metadata', {})
    print(f"  Name:        {metadata.get('name', 'N/A')}")
    print(f"  Namespace:   {metadata.get('namespace', 'N/A')}")
    print(f"  UID:         {metadata.get('uid', 'N/A')[:8]}...")
    print(f"  Created:     {metadata.get('creationTimestamp', 'N/A')}")
    
    # Labels
    labels = metadata.get('labels', {})
    if labels:
        print(f"\n  Labels:")
        for key, value in labels.items():
            print(f"    {key}: {value}")
    
    # Spec section
    print(f"\n\033[0;34m═══ SCHEDULING INFORMATION ═══\033[0m")
    spec = pod.get('spec', {})
    
    # Node assignment (key indicator of QEMU scheduling)
    node_name = spec.get('nodeName', None)
    if node_name:
        print(f"  ${'\033[0;32m'} Node Assigned:  {node_name}\033[0m (✓ SCHEDULED)")
    else:
        print(f"  ${'\033[1;33m'} Node Assigned:  Unscheduled\033[0m (⟳ PENDING)")
    
    # Runtime class
    runtime_class = spec.get('runtimeClassName', None)
    if runtime_class:
        print(f"  Runtime Class:  {runtime_class}")
    
    # Affinity
    affinity = spec.get('affinity', {})
    if affinity:
        print(f"\n  Affinity Rules:")
        node_aff = affinity.get('nodeAffinity', {})
        if node_aff:
            print(f"    Node Affinity: Configured")
            req = node_aff.get('requiredDuringSchedulingIgnoredDuringExecution')
            pref = node_aff.get('preferredDuringSchedulingIgnoredDuringExecution')
            if req:
                print(f"      - Required: Yes")
            if pref:
                print(f"      - Preferred: Yes")
    
    # Container specs
    print(f"\n\033[0;34m═══ CONTAINER SPECIFICATIONS ═══\033[0m")
    containers = spec.get('containers', [])
    for i, container in enumerate(containers):
        print(f"  Container {i+1}: {container.get('name', 'N/A')}")
        print(f"    Image:        {container.get('image', 'N/A')}")
        
        # Resources
        resources = container.get('resources', {})
        if resources:
            requests = resources.get('requests', {})
            limits = resources.get('limits', {})
            
            if requests or limits:
                print(f"    Resources:")
                if requests:
                    print(f"      Requests: CPU={requests.get('cpu', 'N/A')}, Memory={requests.get('memory', 'N/A')}")
                if limits:
                    print(f"      Limits:   CPU={limits.get('cpu', 'N/A')}, Memory={limits.get('memory', 'N/A')}")
    
    # Status section
    print(f"\n\033[0;34m═══ POD STATUS ═══\033[0m")
    status = pod.get('status', {})
    
    phase = status.get('phase', 'Unknown')
    phase_symbol = {
        'Running': '\033[0;32m●\033[0m',
        'Pending': '\033[1;33m◐\033[0m',
        'Succeeded': '\033[0;32m✓\033[0m',
        'Failed': '\033[0;31m✗\033[0m'
    }.get(phase, '?')
    
    print(f"  {phase_symbol} Phase:        {phase}")
    print(f"  Host IP:       {status.get('hostIP', 'N/A')}")
    print(f"  Pod IP:        {status.get('podIP', 'N/A')}")
    print(f"  Start Time:    {status.get('startTime', 'N/A')}")
    
    # Conditions
    conditions = status.get('conditions', [])
    if conditions:
        print(f"\n  Conditions:")
        for cond in conditions:
            cond_type = cond.get('type', 'Unknown')
            cond_status = cond.get('status', 'Unknown')
            reason = cond.get('reason', '')
            
            status_symbol = '\033[0;32m✓\033[0m' if cond_status == 'True' else '\033[0;31m✗\033[0m'
            print(f"    {status_symbol} {cond_type}: {cond_status}")
            if reason:
                print(f"       Reason: {reason}")
    
    # Container statuses
    print(f"\n\033[0;34m═══ CONTAINER RUNTIME STATUS ═══\033[0m")
    container_statuses = status.get('containerStatuses', [])
    
    if container_statuses:
        for cs in container_statuses:
            cname = cs.get('name', 'unknown')
            print(f"  {cname}:")
            print(f"    Ready:        {cs.get('ready', False)}")
            print(f"    Restart Count: {cs.get('restartCount', 0)}")
            
            # Container state
            state = cs.get('state', {})
            if 'running' in state:
                running = state.get('running', {})
                print(f"    State:        Running")
                print(f"    Started:      {running.get('startedAt', 'N/A')}")
            elif 'waiting' in state:
                waiting = state.get('waiting', {})
                print(f"    State:        Waiting ({waiting.get('reason', 'N/A')})")
            elif 'terminated' in state:
                terminated = state.get('terminated', {})
                print(f"    State:        Terminated")
                print(f"    Exit Code:    {terminated.get('exitCode', 'N/A')}")
                print(f"    Reason:       {terminated.get('reason', 'N/A')}")
    
    # QEMU specific info
    print(f"\n\033[0;34m═══ QEMU SCHEDULING DETAILS ═══\033[0m")
    
    if node_name:
        print(f"  ${'\033[0;32m'} ✓ SCHEDULED TO QEMU\033[0m")
        print(f"    Node:          {node_name}")
        print(f"    Assignment:    Complete")
    else:
        print(f"  ${'\033[1;33m'} ⟳ NOT YET SCHEDULED\033[0m")
        print(f"    Waiting for:   Scheduler to assign node")
    
    # Check for QEMU runtime indicators
    qemu_indicators = []
    
    if labels.get('runtime') == 'qemu':
        qemu_indicators.append("Runtime label: qemu")
    
    if runtime_class == 'qemu':
        qemu_indicators.append("RuntimeClassName: qemu")
    
    container_runtime = status.get('containerRuntime', '')
    if 'qemu' in str(container_runtime).lower():
        qemu_indicators.append(f"Container runtime: {container_runtime}")
    
    if qemu_indicators:
        print(f"\n  QEMU Indicators:")
        for indicator in qemu_indicators:
            print(f"    ✓ {indicator}")
    
    # Summary
    print(f"\n\033[0;34m═══ SUMMARY ═══\033[0m")
    
    is_scheduled = node_name is not None and node_name != ''
    is_running = phase == 'Running'
    
    if is_scheduled and is_running:
        print(f"  ${'\033[0;32m'} ✓ UNIKERNEL IS ACTIVELY RUNNING ON QEMU\033[0m")
    elif is_scheduled and not is_running:
        print(f"  ${'\033[1;33m'} ⟳ UNIKERNEL SCHEDULED BUT NOT YET RUNNING ({phase})\033[0m")
    else:
        print(f"  ${'\033[1;33m'} ⟳ UNIKERNEL NOT YET SCHEDULED\033[0m")
    
    # Quick debug commands
    print(f"\n\033[0;34m═══ USEFUL COMMANDS ═══\033[0m")
    print(f"  Check pod logs:      curl http://localhost:6443/api/v1/namespaces/{metadata.get('namespace')}/pods/{metadata.get('name')}/log")
    print(f"  Delete this pod:     curl -X DELETE http://localhost:6443/api/v1/namespaces/{metadata.get('namespace')}/pods/{metadata.get('name')}")
    print(f"  Refresh:             bash scripts/inspect-qemu-pod.sh {metadata.get('namespace')} {metadata.get('name')}")
    
except Exception as e:
    print(f"\033[0;31mError parsing pod data: {e}\033[0m")
    import traceback
    traceback.print_exc()

PYTHON_INSPECT

echo ""
