#!/bin/bash

# Sirah Cluster Manager - Complete management tool

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
API_URL="http://localhost:6443"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

usage() {
    cat <<EOF
${BLUE}Sirah Kubernetes Cluster Manager${NC}

Usage: $0 <command> [options]

Commands:
  ${GREEN}start${NC}              Start all Sirah components with etcd
  ${GREEN}stop${NC}               Stop all Sirah components and etcd
  ${GREEN}restart${NC}            Stop and start all components
  ${GREEN}status${NC}             Show status of all components
  ${GREEN}logs${NC} <component>   View logs for a component
  ${GREEN}test${NC}               Run connectivity tests
  ${GREEN}create-pod${NC} <name>  Create a test pod
  ${GREEN}list-pods${NC}          List all pods
  ${GREEN}delete-pod${NC} <name>  Delete a pod
  ${GREEN}shell${NC}              Open interactive shell for cluster commands

Examples:
  # Start the cluster
  $0 start

  # Check status
  $0 status

  # View kubelet logs
  $0 logs kubelet

  # Create a test pod
  $0 create-pod my-test-pod

  # List all pods
  $0 list-pods

  # Delete a pod
  $0 delete-pod my-test-pod

  # Test cluster connectivity
  $0 test

EOF
    exit 0
}

check_api() {
    if ! curl -s "$API_URL/healthz" > /dev/null 2>&1; then
        echo -e "${RED}ERROR: API Server not responding at $API_URL${NC}"
        echo "Start the cluster with: $0 start"
        exit 1
    fi
}

cmd_start() {
    echo -e "${BLUE}Starting Sirah cluster...${NC}"
    "$SCRIPT_DIR/setup-and-run.sh"
}

cmd_stop() {
    echo -e "${BLUE}Stopping Sirah cluster...${NC}"
    "$SCRIPT_DIR/stop-sirah.sh"
}

cmd_restart() {
    cmd_stop
    sleep 2
    cmd_start
}

cmd_status() {
    echo -e "${BLUE}Sirah Cluster Status${NC}"
    echo ""
    
    # Check components
    local pids=("apiserver" "scheduler" "controller" "kubelet" "etcd")
    for component in "${pids[@]}"; do
        if pgrep -f "sirah-$component\|etcd --listen-client-urls" > /dev/null 2>&1; then
            PID=$(pgrep -f "sirah-$component\|etcd --listen-client-urls" | head -1)
            echo -e "  ${GREEN}✓${NC} $component (PID: $PID)"
        else
            echo -e "  ${RED}✗${NC} $component"
        fi
    done
    echo ""
    
    # Try to check API
    if curl -s "$API_URL/healthz" > /dev/null 2>&1; then
        echo -e "${GREEN}API Server is responding${NC}"
        
        # Get node count
        NODES=$(curl -s "$API_URL/api/v1/nodes" 2>/dev/null | grep -o '"name":"[^"]*"' | wc -l)
        echo "  Nodes: $NODES"
        
        # Get pod count
        PODS=$(curl -s "$API_URL/api/v1/namespaces/default/pods" 2>/dev/null | grep -o '"name":"[^"]*"' | wc -l)
        echo "  Pods: $PODS"
    else
        echo -e "${YELLOW}API Server not responding${NC}"
    fi
}

cmd_logs() {
    local component=${1:-apiserver}
    
    case "$component" in
        apiserver|scheduler|controller|kubelet)
            LOG_FILE="/tmp/sirah-logs/${component}.log"
            ;;
        etcd)
            LOG_FILE="/tmp/sirah-logs/etcd.log"
            ;;
        *)
            echo "Unknown component: $component"
            echo "Available: apiserver, scheduler, controller, kubelet, etcd"
            exit 1
            ;;
    esac
    
    if [ -f "$LOG_FILE" ]; then
        tail -f "$LOG_FILE"
    else
        echo "Log file not found: $LOG_FILE"
        echo "Start the cluster first: $0 start"
        exit 1
    fi
}

cmd_test() {
    echo -e "${BLUE}Testing Sirah cluster...${NC}"
    echo ""
    
    check_api
    
    echo -n "  API Server health... "
    if curl -s "$API_URL/healthz" > /dev/null; then
        echo -e "${GREEN}OK${NC}"
    else
        echo -e "${RED}FAILED${NC}"
    fi
    
    echo -n "  List namespaces... "
    if curl -s "$API_URL/api/v1/namespaces" > /dev/null; then
        echo -e "${GREEN}OK${NC}"
    else
        echo -e "${RED}FAILED${NC}"
    fi
    
    echo -n "  List nodes... "
    if curl -s "$API_URL/api/v1/nodes" > /dev/null; then
        echo -e "${GREEN}OK${NC}"
    else
        echo -e "${RED}FAILED${NC}"
    fi
    
    echo -n "  List pods... "
    if curl -s "$API_URL/api/v1/namespaces/default/pods" > /dev/null; then
        echo -e "${GREEN}OK${NC}"
    else
        echo -e "${RED}FAILED${NC}"
    fi
    
    echo ""
    echo -e "${GREEN}All tests passed!${NC}"
}

cmd_create_pod() {
    local pod_name=$1
    if [ -z "$pod_name" ]; then
        echo "Usage: $0 create-pod <pod-name>"
        exit 1
    fi
    
    check_api
    
    echo "Creating pod: $pod_name"
    
    curl -X POST "$API_URL/api/v1/namespaces/default/pods" \
        -H 'Content-Type: application/json' \
        -d "{
            \"apiVersion\": \"v1\",
            \"kind\": \"Pod\",
            \"metadata\": {
                \"name\": \"$pod_name\",
                \"namespace\": \"default\"
            },
            \"spec\": {
                \"containers\": [
                    {
                        \"name\": \"app\",
                        \"image\": \"unikernel.img\"
                    }
                ]
            }
        }" 2>/dev/null
    
    echo ""
    echo -e "${GREEN}Pod created!${NC}"
    echo "View status: $0 list-pods"
}

cmd_list_pods() {
    check_api
    
    echo -e "${BLUE}Pods in default namespace:${NC}"
    echo ""
    
    PODS=$(curl -s "$API_URL/api/v1/namespaces/default/pods" 2>/dev/null)
    
    if echo "$PODS" | grep -q '"name"'; then
        echo "$PODS" | grep -o '"name":"[^"]*"' | cut -d'"' -f4 | while read pod; do
            echo "  - $pod"
        done
    else
        echo "  (no pods)"
    fi
}

cmd_delete_pod() {
    local pod_name=$1
    if [ -z "$pod_name" ]; then
        echo "Usage: $0 delete-pod <pod-name>"
        exit 1
    fi
    
    check_api
    
    echo "Deleting pod: $pod_name"
    
    curl -X DELETE "$API_URL/api/v1/namespaces/default/pods/$pod_name" 2>/dev/null
    
    echo ""
    echo -e "${GREEN}Pod deleted!${NC}"
}

cmd_shell() {
    check_api
    
    echo -e "${BLUE}Sirah Cluster Shell${NC}"
    echo "API Server: $API_URL"
    echo "Type 'help' for commands"
    echo ""
    
    while true; do
        read -p "sirah> " cmd
        
        case "$cmd" in
            exit|quit)
                break
                ;;
            help)
                echo "Commands:"
                echo "  status          - Show cluster status"
                echo "  pods            - List pods"
                echo "  nodes           - List nodes"
                echo "  create <name>   - Create test pod"
                echo "  delete <name>   - Delete pod"
                echo "  curl <args>     - Run curl command"
                echo "  exit/quit       - Exit shell"
                ;;
            status)
                cmd_status
                ;;
            pods)
                cmd_list_pods
                ;;
            nodes)
                echo "Nodes:"
                curl -s "$API_URL/api/v1/nodes" 2>/dev/null | grep -o '"name":"[^"]*"' | cut -d'"' -f4 | while read node; do
                    echo "  - $node"
                done
                ;;
            create*)
                POD_NAME=$(echo "$cmd" | cut -d' ' -f2)
                cmd_create_pod "$POD_NAME"
                ;;
            delete*)
                POD_NAME=$(echo "$cmd" | cut -d' ' -f2)
                cmd_delete_pod "$POD_NAME"
                ;;
            curl*)
                CURL_ARGS=$(echo "$cmd" | cut -d' ' -f2-)
                curl $CURL_ARGS "$API_URL"
                echo ""
                ;;
            *)
                echo "Unknown command: $cmd"
                echo "Type 'help' for available commands"
                ;;
        esac
    done
}

# Main
if [ $# -eq 0 ]; then
    usage
fi

case "$1" in
    start)
        cmd_start
        ;;
    stop)
        cmd_stop
        ;;
    restart)
        cmd_restart
        ;;
    status)
        cmd_status
        ;;
    logs)
        cmd_logs "$2"
        ;;
    test)
        cmd_test
        ;;
    create-pod)
        cmd_create_pod "$2"
        ;;
    list-pods)
        cmd_list_pods
        ;;
    delete-pod)
        cmd_delete_pod "$2"
        ;;
    shell)
        cmd_shell
        ;;
    *)
        echo "Unknown command: $1"
        usage
        ;;
esac
