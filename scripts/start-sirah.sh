#!/bin/bash

# Sirah Kubernetes Cluster - Full Startup Script
# Starts all components: API Server, Scheduler, Controller Manager, and Kubelet

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BIN_DIR="$SCRIPT_DIR/bin"
LOG_DIR="/tmp/sirah-logs"
PID_FILE="$LOG_DIR/sirah.pids"
ETCD_ADDR="${ETCD_ADDR:-localhost:2379}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
API_SERVER_PORT=6443
API_SERVER_URL="http://localhost:$API_SERVER_PORT"
NODE_NAME="${NODE_NAME:-worker1}"
LOG_LEVEL="${LOG_LEVEL:-info}"

# Cleanup on exit
cleanup() {
    echo -e "\n${YELLOW}[CLEANUP] Shutting down Sirah components...${NC}"
    
    if [ -f "$PID_FILE" ]; then
        while read pid; do
            if [ ! -z "$pid" ] && ps -p "$pid" > /dev/null 2>&1; then
                echo "  Stopping PID $pid..."
                kill -TERM "$pid" 2>/dev/null || true
                sleep 1
                kill -9 "$pid" 2>/dev/null || true
            fi
        done < "$PID_FILE"
        rm -f "$PID_FILE"
    fi
    
    echo -e "${GREEN}[CLEANUP] All components stopped${NC}"
}

trap cleanup EXIT INT TERM

# Create log directory
mkdir -p "$LOG_DIR"

echo -e "${BLUE}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║            Sirah Kubernetes Cluster Startup               ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Verify binaries exist
echo -e "${YELLOW}[1/5] Checking binaries...${NC}"
for binary in apiserver scheduler controller kubelet; do
    if [ ! -f "$BIN_DIR/sirah-$binary" ]; then
        echo -e "${RED}ERROR: $BIN_DIR/sirah-$binary not found${NC}"
        echo "Run 'make' in the sirah directory to compile"
        exit 1
    fi
    echo -e "  ${GREEN}✓${NC} sirah-$binary"
done
echo ""

# Check dependencies
echo -e "${YELLOW}[2/5] Checking dependencies...${NC}"

# Check for etcd
if command -v etcd &> /dev/null; then
    echo -e "  ${GREEN}✓${NC} etcd found"
    # Check if etcd is running
    if nc -z localhost 2379 2>/dev/null; then
        echo -e "  ${GREEN}✓${NC} etcd is running on port 2379"
    else
        echo -e "  ${YELLOW}ℹ${NC}  etcd not running - starting it..."
        nohup etcd > "$LOG_DIR/etcd.log" 2>&1 &
        ETCD_PID=$!
        echo "$ETCD_PID" >> "$PID_FILE"
        sleep 2
        if nc -z localhost 2379 2>/dev/null; then
            echo -e "  ${GREEN}✓${NC} etcd started (PID: $ETCD_PID)"
        else
            echo -e "  ${YELLOW}⚠${NC}  etcd may not have started (check: $LOG_DIR/etcd.log)"
        fi
    fi
else
    echo -e "  ${YELLOW}⚠${NC}  etcd not found - install with: apt-get install etcd"
    echo "     API Server requires etcd for storage"
fi

# Check QEMU setup
echo -e "  Checking QEMU environment..."
if [ -d "/var/lib/sirah/unikernels" ]; then
    IMG_COUNT=$(ls /var/lib/sirah/unikernels/*.img 2>/dev/null | wc -l)
    echo -e "    ${GREEN}✓${NC} Unikernel directory ($IMG_COUNT images)"
else
    echo -e "    ${YELLOW}⚠${NC}  Creating unikernel directory..."
    sudo mkdir -p /var/lib/sirah/unikernels
fi

if [ -d "/var/lib/sirah/vms" ]; then
    echo -e "    ${GREEN}✓${NC} VM directory"
else
    echo -e "    ${YELLOW}ℹ${NC}  Creating VM directory..."
    sudo mkdir -p /var/lib/sirah/vms 2>/dev/null || true
fi
echo ""

# Start components
echo -e "${YELLOW}[3/5] Starting Sirah components...${NC}"
> "$PID_FILE"  # Clear PID file

# NOTE: etcd is expected to be running as a system service
# The API server will connect to it via the --etcd argument

# 1. API Server
echo -n "  Starting API Server... "
"$BIN_DIR/sirah-apiserver" --port "$API_SERVER_PORT" --etcd "$ETCD_ADDR" > "$LOG_DIR/apiserver.log" 2>&1 &
APISERVER_PID=$!
echo "$APISERVER_PID" >> "$PID_FILE"
sleep 2
if ps -p "$APISERVER_PID" > /dev/null; then
    echo -e "${GREEN}OK${NC} (PID: $APISERVER_PID)"
else
    echo -e "${RED}FAILED${NC}"
    echo "  Log: cat $LOG_DIR/apiserver.log"
    tail -20 "$LOG_DIR/apiserver.log"
    exit 1
fi

# 2. Scheduler
echo -n "  Starting Scheduler... "
"$BIN_DIR/sirah-scheduler" --api-server "$API_SERVER_URL" > "$LOG_DIR/scheduler.log" 2>&1 &
SCHEDULER_PID=$!
echo "$SCHEDULER_PID" >> "$PID_FILE"
sleep 1
if ps -p "$SCHEDULER_PID" > /dev/null; then
    echo -e "${GREEN}OK${NC} (PID: $SCHEDULER_PID)"
else
    echo -e "${RED}FAILED${NC}"
    echo "  Log: cat $LOG_DIR/scheduler.log"
    exit 1
fi

# 3. Controller Manager
echo -n "  Starting Controller Manager... "
"$BIN_DIR/sirah-controller" --api-server "$API_SERVER_URL" > "$LOG_DIR/controller.log" 2>&1 &
CONTROLLER_PID=$!
echo "$CONTROLLER_PID" >> "$PID_FILE"
sleep 1
if ps -p "$CONTROLLER_PID" > /dev/null; then
    echo -e "${GREEN}OK${NC} (PID: $CONTROLLER_PID)"
else
    echo -e "${RED}FAILED${NC}"
    echo "  Log: cat $LOG_DIR/controller.log"
    exit 1
fi

# 4. Kubelet (Node Agent)
echo -n "  Starting Kubelet ($NODE_NAME)... "
"$BIN_DIR/sirah-kubelet" --node "$NODE_NAME" --api-server "$API_SERVER_URL" > "$LOG_DIR/kubelet.log" 2>&1 &
KUBELET_PID=$!
echo "$KUBELET_PID" >> "$PID_FILE"
sleep 1
if ps -p "$KUBELET_PID" > /dev/null; then
    echo -e "${GREEN}OK${NC} (PID: $KUBELET_PID)"
else
    echo -e "${RED}FAILED${NC}"
    echo "  Log: cat $LOG_DIR/kubelet.log"
    exit 1
fi
echo ""

# Wait for API server to be ready
echo -e "${YELLOW}[4/5] Waiting for API Server to be ready...${NC}"
MAX_RETRIES=10
RETRY_COUNT=0
while [ $RETRY_COUNT -lt $MAX_RETRIES ]; do
    if curl -s "$API_SERVER_URL/healthz" > /dev/null 2>&1; then
        echo -e "  ${GREEN}✓${NC} API Server is ready"
        break
    fi
    RETRY_COUNT=$((RETRY_COUNT + 1))
    echo -n "."
    sleep 1
done

if [ $RETRY_COUNT -eq $MAX_RETRIES ]; then
    echo -e "\n  ${YELLOW}⚠${NC}  API Server may not be responding yet (check logs)"
fi
echo ""

# Run tests
echo -e "${YELLOW}[5/5] Running connectivity tests...${NC}"

# Test healthz
echo -n "  Health check... "
if curl -s "$API_SERVER_URL/healthz" > /dev/null 2>&1; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC}"
fi

# Test API endpoints
echo -n "  List namespaces... "
if NAMESPACES=$(curl -s "$API_SERVER_URL/api/v1/namespaces" 2>/dev/null); then
    if echo "$NAMESPACES" | grep -q "default"; then
        echo -e "${GREEN}✓${NC}"
    else
        echo -e "${YELLOW}⚠${NC} (no default namespace)"
    fi
else
    echo -e "${RED}✗${NC}"
fi

echo -n "  List pods... "
if curl -s "$API_SERVER_URL/api/v1/namespaces/default/pods" > /dev/null 2>&1; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC}"
fi

echo -n "  List nodes... "
if curl -s "$API_SERVER_URL/api/v1/nodes" > /dev/null 2>&1; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC}"
fi

echo ""
echo -e "${BLUE}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║              Sirah Cluster is Running!                    ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${GREEN}Summary:${NC}"
echo "  API Server:         $API_SERVER_URL"
echo "  Node Name:          $NODE_NAME"
echo "  Log Directory:      $LOG_DIR"
echo ""
echo -e "${GREEN}Components Running:${NC}"
echo "  [PID $APISERVER_PID]  API Server      - Log: tail -f $LOG_DIR/apiserver.log"
echo "  [PID $SCHEDULER_PID]  Scheduler       - Log: tail -f $LOG_DIR/scheduler.log"
echo "  [PID $CONTROLLER_PID] Controller      - Log: tail -f $LOG_DIR/controller.log"
echo "  [PID $KUBELET_PID]  Kubelet         - Log: tail -f $LOG_DIR/kubelet.log"
echo ""
echo -e "${GREEN}Quick Start:${NC}"
echo "  Create a pod:"
echo "    curl -X POST $API_SERVER_URL/api/v1/namespaces/default/pods \\\\"
echo "      -H 'Content-Type: application/json' \\\\"
echo "      -d '{\"apiVersion\":\"v1\",\"kind\":\"Pod\",\"metadata\":{\"name\":\"test\"},\"spec\":{\"containers\":[{\"name\":\"app\",\"image\":\"unikernel.img\"}]}}'"
echo ""
echo "  Get pod status:"
echo "    curl $API_SERVER_URL/api/v1/namespaces/default/pods/test"
echo ""
echo "  Watch components:"
echo "    - API Server:  tail -f $LOG_DIR/apiserver.log"
echo "    - Kubelet:     tail -f $LOG_DIR/kubelet.log"
echo "    - Scheduler:   tail -f $LOG_DIR/scheduler.log"
echo ""
echo -e "${YELLOW}Press Ctrl+C to stop all components${NC}"
echo ""

# Keep the script running
wait
