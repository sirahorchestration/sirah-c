#!/bin/bash

# Sirah Cluster - Complete Startup Script
# Starts all components: etcd, API Server, Scheduler, Controller Manager

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BIN_DIR="$SCRIPT_DIR/bin"
LOG_DIR="/tmp/sirah-logs"

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
echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Kill any existing processes
echo -e "${YELLOW}[1/5] Cleaning up old processes...${NC}"
pkill -f "etcd" 2>/dev/null || true
pkill -f "sirah-apiserver" 2>/dev/null || true
pkill -f "sirah-scheduler" 2>/dev/null || true
pkill -f "sirah-controller" 2>/dev/null || true
pkill -f "sirah-kubelet" 2>/dev/null || true
sleep 2
echo -e "${GREEN}✓ Old processes cleaned${NC}"
echo ""

# Start etcd
echo -e "${YELLOW}[2/5] Starting etcd...${NC}"
rm -rf /tmp/sirah-etcd 2>/dev/null || true
etcd --listen-client-urls http://localhost:2379 --advertise-client-urls http://localhost:2379 --data-dir /tmp/sirah-etcd > "$LOG_DIR/etcd.log" 2>&1 &
ETCD_PID=$!
sleep 2

if ps -p $ETCD_PID > /dev/null; then
    echo -e "${GREEN}✓ etcd started (PID: $ETCD_PID)${NC}"
else
    echo -e "${RED}✗ etcd failed to start${NC}"
    cat "$LOG_DIR/etcd.log"
    exit 1
fi
echo ""

# Start API Server
echo -e "${YELLOW}[3/5] Starting API Server...${NC}"
cd "$SCRIPT_DIR"
stdbuf -oL "$BIN_DIR/sirah-apiserver" > "$LOG_DIR/apiserver.log" 2>&1 &
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
echo -e "${YELLOW}[4/5] Starting Scheduler...${NC}"
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
echo -e "${YELLOW}[5/5] Starting Controller Manager...${NC}"
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

echo -e "${GREEN}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║          ✓ Sirah Cluster is Running!                       ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${GREEN}Running Components:${NC}"
echo "  [PID $ETCD_PID]       etcd           - Log: tail -f $LOG_DIR/etcd.log"
echo "  [PID $APISERVER_PID]      API Server     - Log: tail -f $LOG_DIR/apiserver.log"
echo "  [PID $SCHEDULER_PID]      Scheduler      - Log: tail -f $LOG_DIR/scheduler.log"
echo "  [PID $CONTROLLER_PID]      Controller     - Log: tail -f $LOG_DIR/controller.log"
echo ""
echo -e "${BLUE}Test the cluster:${NC}"
echo "  curl http://localhost:6443/api/v1/nodes"
echo "  curl http://localhost:6443/api/v1/namespaces/default/pods"
echo ""
echo -e "${BLUE}Create a test pod:${NC}"
echo "  curl -X POST http://localhost:6443/api/v1/namespaces/default/pods \\"
echo "    -H 'Content-Type: application/json' \\"
echo "    -d '{\"apiVersion\":\"v1\",\"kind\":\"Pod\",\"metadata\":{\"name\":\"test-pod\"},\"spec\":{\"containers\":[{\"name\":\"app\",\"image\":\"unikernel.img\"}]}}'"
echo ""
echo -e "${YELLOW}Press Ctrl+C to stop all components${NC}"
echo ""

# Keep script running
wait
