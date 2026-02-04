#!/bin/bash

# Sirah Cluster - Complete Startup Script
# Starts all components: etcd, API Server, Scheduler, Controller Manager

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BIN_DIR="$SCRIPT_DIR/bin"
LOG_DIR="/tmp/sirah-logs"
ETCD_ADDR="${ETCD_ADDR:-http://localhost:2379}"
ETCD_PORT="${ETCD_PORT:-2379}"

# Ensure log directory exists
mkdir -p "$LOG_DIR"

# Redirect all script output to log file while also showing it on terminal
SCRIPT_LOG="$LOG_DIR/start-all.log"
exec > >(tee -a "$SCRIPT_LOG")
exec 2>&1

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Cleanup function
cleanup() {
    echo ""
    echo -e "${YELLOW}[CLEANUP] Shutting down all components...${NC}"
    pkill -f "etcd" 2>/dev/null || true
    pkill -f "sirah-apiserver" 2>/dev/null || true
    pkill -f "sirah-scheduler" 2>/dev/null || true
    pkill -f "sirah-controller" 2>/dev/null || true
    pkill -f "sirah-kubelet" 2>/dev/null || true
    sleep 1
    echo -e "${GREEN}[CLEANUP] All components stopped${NC}"
}

# Trap Ctrl+C to cleanup
trap cleanup EXIT INT TERM

# Ensure log directory exists
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/*.log

echo -e "${BLUE}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║        Sirah Kubernetes Cluster - Startup Script            ║${NC}"
echo -e "${BLUE}║  NOTE: Controller is REQUIRED for pod logs to work!         ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Check prerequisites
echo -e "${YELLOW}[0/5] Checking prerequisites...${NC}"
MISSING=0

# Check if binaries exist
if [ ! -f "$BIN_DIR/sirah-apiserver" ]; then
    echo -e "${YELLOW}  ! Need to build binaries...${NC}"
    cd "$SCRIPT_DIR"
    make clean > /dev/null 2>&1 || true
    make
    cd - > /dev/null
fi

# Check etcd connectivity
if ! nc -zv localhost $ETCD_PORT 2>&1 | grep -q "succeeded"; then
    echo -e "${YELLOW}  ⚠ etcd is not running on localhost:$ETCD_PORT${NC}"
    echo -e "${YELLOW}    etcd should be running before starting Sirah${NC}"
    echo -e "${YELLOW}    (Most WSL users have etcd running as a service)${NC}"
    echo ""
    read -p "  Continue without checking etcd? (y/n): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
else
    echo -e "${GREEN}  ✓ etcd is running${NC}"
fi

echo -e "${GREEN}✓ Prerequisites OK${NC}"
echo ""

# Kill any existing processes
echo -e "${YELLOW}[1/5] Cleaning up old processes...${NC}"
pkill -f "sirah-apiserver" 2>/dev/null || true
pkill -f "sirah-scheduler" 2>/dev/null || true
pkill -f "sirah-controller" 2>/dev/null || true
pkill -f "sirah-kubelet" 2>/dev/null || true
sleep 2
echo -e "${GREEN}✓ Old processes cleaned${NC}"
echo ""

# Start etcd
# NOTE: etcd is expected to be running as a system service
# If running etcd manually, uncomment the section below
# echo -e "${YELLOW}[1/4] Starting etcd...${NC}"
# rm -rf /tmp/sirah-etcd 2>/dev/null || true
# etcd --listen-client-urls http://$ETCD_ADDR --advertise-client-urls http://$ETCD_ADDR --data-dir /tmp/sirah-etcd > "$LOG_DIR/etcd.log" 2>&1 &
# ETCD_PID=$!
# sleep 2
# if ps -p $ETCD_PID > /dev/null; then
#     echo -e "${GREEN}✓ etcd started (PID: $ETCD_PID)${NC}"
# else
#     echo -e "${RED}✗ etcd failed to start${NC}"
#     cat "$LOG_DIR/etcd.log"
#     exit 1
# fi
# echo ""

# Start API Server
echo -e "${YELLOW}[2/5] Starting API Server...${NC}"
cd "$SCRIPT_DIR"
stdbuf -oL "$BIN_DIR/sirah-apiserver" --etcd "$ETCD_ADDR" > "$LOG_DIR/apiserver.log" 2>&1 &
APISERVER_PID=$!
sleep 3

if ps -p $APISERVER_PID > /dev/null; then
    echo -e "${GREEN}✓ API Server started (PID: $APISERVER_PID)${NC}"
    echo -e "   Endpoint: http://localhost:6443"
else
    echo -e "${RED}✗ API Server failed to start${NC}"
    cat "$LOG_DIR/apiserver.log"
    exit 1
fi
echo ""

# Start Scheduler
echo -e "${YELLOW}[3/5] Starting Scheduler...${NC}"
stdbuf -oL "$BIN_DIR/sirah-scheduler" > "$LOG_DIR/scheduler.log" 2>&1 &
SCHEDULER_PID=$!
sleep 2

if ps -p $SCHEDULER_PID > /dev/null; then
    echo -e "${GREEN}✓ Scheduler started (PID: $SCHEDULER_PID)${NC}"
else
    echo -e "${RED}✗ Scheduler failed to start${NC}"
    cat "$LOG_DIR/scheduler.log"
    exit 1
fi
echo ""

# Start Controller Manager
echo -e "${YELLOW}[4/4] Starting Controller Manager (CRITICAL FOR POD LOGS)...${NC}"
stdbuf -oL "$BIN_DIR/sirah-controller" > "$LOG_DIR/controller.log" 2>&1 &
CONTROLLER_PID=$!
sleep 2

if ps -p $CONTROLLER_PID > /dev/null; then
    echo -e "${GREEN}✓ Controller Manager started (PID: $CONTROLLER_PID)${NC}"
else
    echo -e "${RED}✗ Controller Manager failed to start${NC}"
    cat "$LOG_DIR/controller.log"
    exit 1
fi
echo ""

# Start Kubelet
echo -e "${YELLOW}[5/5] Starting Kubelet (control-plane node)...${NC}"
stdbuf -oL "$BIN_DIR/sirah-kubelet" --node-name control-plane > "$LOG_DIR/kubelet.log" 2>&1 &
KUBELET_PID=$!
sleep 2

if ps -p $KUBELET_PID > /dev/null; then
    echo -e "${GREEN}✓ Kubelet started (PID: $KUBELET_PID)${NC}"
else
    echo -e "${RED}✗ Kubelet failed to start${NC}"
    cat "$LOG_DIR/kubelet.log"
    exit 1
fi
echo ""

echo -e "${GREEN}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║          ✓ Sirah Cluster is Running!                       ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${GREEN}Running Components:${NC}"
echo "  [PID $APISERVER_PID]       API Server       - Log: tail -f $LOG_DIR/apiserver.log"
echo "  [PID $SCHEDULER_PID]       Scheduler        - Log: tail -f $LOG_DIR/scheduler.log"
echo "  [PID $CONTROLLER_PID]       Controller*      - Log: tail -f $LOG_DIR/controller.log"
echo "  [PID $KUBELET_PID]       Kubelet          - Log: tail -f $LOG_DIR/kubelet.log"
echo ""
echo -e "${BLUE}* Controller is REQUIRED for pod lifecycle management${NC}"
echo -e "${BLUE}* Kubelet is REQUIRED to spawn QEMU VMs for pods${NC}"
echo ""
echo -e "${BLUE}Pod Logs Location:${NC}"
echo "  /tmp/sirah-logs/pods/{namespace}/{pod_name}/app.log"
echo ""
echo -e "${BLUE}Test the cluster:${NC}"
echo "  curl -u admin:admin http://localhost:6443/api/v1/nodes"
echo "  curl -u admin:admin http://localhost:6443/api/v1/namespaces/default/pods"
echo ""
echo -e "${BLUE}Create a test pod:${NC}"
echo "  curl -X POST -u admin:admin http://localhost:6443/api/v1/namespaces/default/pods \\"
echo "    -H 'Content-Type: application/json' \\"
echo "    -d '{\"apiVersion\":\"v1\",\"kind\":\"Pod\",\"metadata\":{\"name\":\"test-pod-'$(date +%s)'\"},\"spec\":{\"containers\":[{\"name\":\"app\",\"image\":\"/tmp/sirah-unikernels/test-kernel.img\",\"resources\":{\"limits\":{\"memory\":\"128Mi\",\"cpu\":\"1\"}}}]}}'"
echo ""
echo -e "${BLUE}Get pod logs (after ~5 seconds):${NC}"
echo "  curl -u admin:admin http://localhost:6443/api/v1/namespaces/default/pods/test-pod-*/log"
echo "  ls /tmp/sirah-logs/pods/default/*/"
echo ""
echo -e "${YELLOW}Press Ctrl+C to stop all components${NC}"
echo ""

# Keep script running
wait
