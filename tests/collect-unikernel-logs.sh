#!/bin/bash

# Collect logs from running unikernels
# Provides multiple ways to view logs from QEMU pods

set -e

API_URL="http://localhost:6443"
ADMIN_USER="admin:admin"

show_usage() {
    cat << EOF
Usage: $0 [COMMAND] [OPTIONS]

Commands:
  logs [POD_NAME]              Get logs from a specific pod
  follow [POD_NAME]            Follow logs in real-time (tail -f style)
  list                         List all pods and their logs
  qemu [VM_NAME]              Get raw QEMU VM logs
  all                          Get all logs (from API + QEMU)
  stats                        Show logging statistics

Options:
  -n, --namespace NS           Namespace (default: default)
  -c, --container NAME         Container name (default: app)
  --tail LINES                 Number of lines to show (default: 50)
  --timestamps                 Include timestamps in output

Examples:
  $0 logs test-qemu-spawn
  $0 follow test-qemu-spawn
  $0 logs -n kube-system my-pod
  $0 qemu default-test-qemu-spawn
  $0 all

EOF
    exit 0
}

# Default values
NAMESPACE="default"
CONTAINER="app"
TAIL_LINES=50
TIMESTAMPS=false
COMMAND="${1:-list}"

# Parse arguments
shift || true
while [[ $# -gt 0 ]]; do
    case $1 in
        -n|--namespace)
            NAMESPACE="$2"
            shift 2
            ;;
        -c|--container)
            CONTAINER="$2"
            shift 2
            ;;
        --tail)
            TAIL_LINES="$2"
            shift 2
            ;;
        --timestamps)
            TIMESTAMPS=true
            shift
            ;;
        -h|--help)
            show_usage
            ;;
        *)
            POD_NAME="$1"
            shift
            ;;
    esac
done

# Get pod logs from API
get_pod_logs() {
    local ns="$1"
    local pod="$2"
    local container="$3"
    local tail="$4"
    local timestamps="$5"
    
    if [ "$timestamps" = "true" ]; then
        QUERY="?timestamps=true&tailLines=$tail"
    else
        QUERY="?tailLines=$tail"
    fi
    
    echo "[API Logs] $ns/$pod ($container)"
    echo "---"
    curl -s -u "$ADMIN_USER" "$API_URL/api/v1/namespaces/$ns/pods/$pod/log$QUERY" 2>/dev/null || echo "  (No logs available)"
    echo ""
}

# Get QEMU VM logs
get_qemu_logs() {
    local vm_name="$1"
    
    echo "[QEMU Logs] $vm_name"
    echo "---"
    
    LOG_FILE="/tmp/qemu-$vm_name.log"
    if [ -f "$LOG_FILE" ]; then
        tail -50 "$LOG_FILE" 2>/dev/null
    else
        echo "  (No QEMU log file found at $LOG_FILE)"
    fi
    echo ""
}

# List all pods and show basic info
list_pods() {
    echo "=== Pod Logs Overview ==="
    echo ""
    
    PODS=$(curl -s -u "$ADMIN_USER" "$API_URL/api/v1/pods" 2>/dev/null | jq -r '.items[] | "\(.metadata.namespace)/\(.metadata.name)"' || echo "")
    
    if [ -z "$PODS" ]; then
        echo "No pods found"
        return
    fi
    
    while read -r pod_ref; do
        NS=$(echo "$pod_ref" | cut -d'/' -f1)
        POD=$(echo "$pod_ref" | cut -d'/' -f2)
        
        echo "Pod: $pod_ref"
        
        # Get quick info
        POD_DATA=$(curl -s -u "$ADMIN_USER" "$API_URL/api/v1/namespaces/$NS/pods/$POD" 2>/dev/null)
        PHASE=$(echo "$POD_DATA" | jq -r '.status.phase' 2>/dev/null || echo "Unknown")
        IMAGE=$(echo "$POD_DATA" | jq -r '.spec.containers[0].image' 2>/dev/null || echo "Unknown")
        
        echo "  Phase: $PHASE"
        echo "  Image: $IMAGE"
        
        # Check for QEMU VM name
        VM_NAME="$NS-$POD"
        if [ -f "/tmp/qemu-$VM_NAME.log" ]; then
            LOG_LINES=$(wc -l < "/tmp/qemu-$VM_NAME.log" 2>/dev/null || echo 0)
            echo "  QEMU Log: $LOG_LINES lines"
        fi
        echo ""
    done <<< "$PODS"
}

# Follow logs (tail -f style)
follow_logs() {
    local ns="$1"
    local pod="$2"
    
    if [ -z "$pod" ]; then
        echo "Error: Pod name required for follow command"
        show_usage
    fi
    
    echo "Following logs for $ns/$pod (press Ctrl+C to stop)"
    echo ""
    
    # Get QEMU log file
    VM_NAME="$ns-$pod"
    LOG_FILE="/tmp/qemu-$VM_NAME.log"
    
    if [ -f "$LOG_FILE" ]; then
        echo "Following QEMU output..."
        tail -f "$LOG_FILE"
    else
        echo "Error: QEMU log file not found at $LOG_FILE"
        echo ""
        echo "Trying API logs endpoint..."
        while true; do
            clear
            get_pod_logs "$ns" "$pod" "$CONTAINER" "$TAIL_LINES" "true"
            sleep 2
        done
    fi
}

# Get all logs (API + QEMU)
get_all_logs() {
    echo "=== Complete Logs for All Pods ==="
    echo ""
    
    PODS=$(curl -s -u "$ADMIN_USER" "$API_URL/api/v1/pods" 2>/dev/null | jq -r '.items[] | "\(.metadata.namespace)/\(.metadata.name)"' || echo "")
    
    if [ -z "$PODS" ]; then
        echo "No pods found"
        return
    fi
    
    while read -r pod_ref; do
        NS=$(echo "$pod_ref" | cut -d'/' -f1)
        POD=$(echo "$pod_ref" | cut -d'/' -f2)
        
        get_pod_logs "$NS" "$POD" "$CONTAINER" "$TAIL_LINES" "$TIMESTAMPS"
        
        VM_NAME="$NS-$POD"
        get_qemu_logs "$VM_NAME"
    done <<< "$PODS"
}

# Get QEMU-specific logs
get_qemu_specific() {
    local vm_name="$1"
    
    if [ -z "$vm_name" ]; then
        echo "Error: VM name required"
        show_usage
    fi
    
    get_qemu_logs "$vm_name"
}

# Show logging statistics
show_stats() {
    echo "=== Logging Statistics ==="
    echo ""
    
    echo "[API Logs Storage]"
    echo "  Pod entries: (built-in limit: 1000)"
    echo "  Log storage capacity: 1MB per pod"
    echo "  Max lines per pod: 10000"
    echo ""
    
    echo "[QEMU Logs]"
    QEMU_LOG_COUNT=$(find /tmp -name "qemu-*.log" -type f 2>/dev/null | wc -l)
    echo "  QEMU log files: $QEMU_LOG_COUNT"
    
    if [ "$QEMU_LOG_COUNT" -gt 0 ]; then
        echo "  QEMU log files:"
        find /tmp -name "qemu-*.log" -type f 2>/dev/null | while read -r log_file; do
            SIZE=$(du -h "$log_file" | awk '{print $1}')
            LINES=$(wc -l < "$log_file" 2>/dev/null || echo 0)
            echo "    • $log_file"
            echo "      Size: $SIZE, Lines: $LINES"
        done
    fi
    echo ""
    
    echo "[Disk Usage]"
    TOTAL_SIZE=$(du -sh /tmp/qemu-* /tmp/sirah-logs 2>/dev/null | awk '{sum+=$1} END {print sum}')
    echo "  Total log space: $TOTAL_SIZE"
    echo ""
    
    echo "[API Pod Count]"
    POD_COUNT=$(curl -s -u "$ADMIN_USER" "$API_URL/api/v1/pods" 2>/dev/null | jq '.items | length' || echo "0")
    echo "  Pods in cluster: $POD_COUNT"
    echo ""
}

# Main dispatch
case "$COMMAND" in
    logs)
        get_pod_logs "$NAMESPACE" "$POD_NAME" "$CONTAINER" "$TAIL_LINES" "$TIMESTAMPS"
        ;;
    follow)
        follow_logs "$NAMESPACE" "$POD_NAME"
        ;;
    qemu)
        get_qemu_specific "$POD_NAME"
        ;;
    all)
        get_all_logs
        ;;
    stats)
        show_stats
        ;;
    list|"")
        list_pods
        ;;
    -h|--help|help)
        show_usage
        ;;
    *)
        echo "Unknown command: $COMMAND"
        show_usage
        ;;
esac
