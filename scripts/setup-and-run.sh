#!/bin/bash

# Sirah Setup & Startup - Simple version with dependency checking

echo "=== Sirah Kubernetes - Setup & Startup ==="
echo ""

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BIN_DIR="$SCRIPT_DIR/bin"

# Step 1: Check dependencies
echo "[1] Checking dependencies..."

MISSING_DEPS=()

# Check for etcd
if ! command -v etcd &> /dev/null; then
    MISSING_DEPS+=("etcd")
    echo "  ✗ etcd not found (required for persistent storage)"
else
    echo "  ✓ etcd found"
fi

# Check for nc (for port checking)
if ! command -v nc &> /dev/null; then
    MISSING_DEPS+=("netcat")
    echo "  ⚠ netcat not found (optional, for port checking)"
fi

if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
    echo ""
    echo "Missing dependencies: ${MISSING_DEPS[@]}"
    echo ""
    echo "To install on Ubuntu/Debian:"
    echo "  sudo apt-get update"
    echo "  sudo apt-get install -y etcd-server netcat-openbsd"
    echo ""
    echo "For macOS:"
    echo "  brew install etcd netcat"
    echo ""
    echo "Then run this script again."
    exit 1
fi

echo ""
echo "[2] Checking compiled binaries..."

for binary in apiserver scheduler controller kubelet; do
    if [ ! -f "$BIN_DIR/sirah-$binary" ]; then
        echo "  ✗ Missing: $BIN_DIR/sirah-$binary"
        echo "Run 'make' in the sirah directory first"
        exit 1
    fi
    echo "  ✓ sirah-$binary"
done

echo ""
echo "[3] Setting up directories..."

# Create required directories - skip if permission denied
mkdir -p /tmp/sirah-logs && \
    echo "  ✓ Created /tmp/sirah-logs"

# Try to create sirah dirs but don't fail if can't
mkdir -p /var/lib/sirah/{vms,unikernels} 2>/dev/null && \
    echo "  ✓ Created /var/lib/sirah/{vms,unikernels}" || \
    echo "  ⚠ Note: /var/lib/sirah needs 'sudo mkdir -p /var/lib/sirah/{vms,unikernels}'"

echo ""
echo "[4] Starting etcd..."

# Kill existing processes
pkill -f etcd
pkill -f sirah
sleep 1

# Stop etcd service if running via systemd
sudo systemctl stop etcd 2>/dev/null || true
sleep 1

# Kill any existing etcd processes
pkill -f etcd 2>/dev/null || true
sleep 1

# Remove old etcd data directory to avoid lock conflicts
rm -rf /tmp/sirah-etcd 2>/dev/null || true

# Start etcd in tmux
tmux new-session -d -s etcd -x 200 -y 50 "etcd --listen-client-urls http://localhost:2379 --advertise-client-urls http://localhost:2379 --data-dir /tmp/sirah-etcd"
sleep 2

ETCD_PID=$(pgrep -f "etcd --listen-client-urls")
if [ -n "$ETCD_PID" ]; then
    echo "  ✓ etcd started (PID: $ETCD_PID)"
else
    echo "  ✗ etcd failed to start"
    echo "Check: tmux capture-session -t etcd -p"
    exit 1
fi

echo ""
echo "[5] Starting Sirah components..."

# Kill any existing components
pkill -f "sirah-apiserver" 2>/dev/null || true
pkill -f "sirah-scheduler" 2>/dev/null || true
pkill -f "sirah-controller" 2>/dev/null || true
pkill -f "sirah-kubelet" 2>/dev/null || true
sleep 1

# Create log directory
LOG_DIR="/tmp/sirah-logs"
mkdir -p "$LOG_DIR"

# Start components in tmux sessions with logging
echo "  Starting API Server..."
tmux new-session -d -s apiserver -x 200 -y 50 "cd $BIN_DIR/..; $BIN_DIR/sirah-apiserver 2>&1 | tee $LOG_DIR/apiserver.log"
sleep 3

# Verify API Server started
if pgrep -f "sirah-apiserver" > /dev/null; then
    echo "  ✓ API Server started"
else
    echo "  ✗ API Server failed to start"
    echo "Check: tail -f $LOG_DIR/apiserver.log"
    exit 1
fi

echo "  Starting Scheduler..."
tmux new-session -d -s scheduler -x 200 -y 50 "cd $BIN_DIR/..; $BIN_DIR/sirah-scheduler 2>&1 | tee $LOG_DIR/scheduler.log"
sleep 2

echo "  Starting Controller Manager..."
tmux new-session -d -s controller -x 200 -y 50 "cd $BIN_DIR/..; $BIN_DIR/sirah-controller 2>&1 | tee $LOG_DIR/controller.log"
sleep 2

# Verify Controller started
if pgrep -f "sirah-controller" > /dev/null; then
    echo "  ✓ Controller started"
else
    echo "  ✗ Controller failed to start"
    echo "Check: tail -f $LOG_DIR/controller.log"
fi

echo "  Starting Kubelet (node: worker1)..."
tmux new-session -d -s kubelet -x 200 -y 50 "cd $BIN_DIR/..; $BIN_DIR/sirah-kubelet 2>&1 | tee $LOG_DIR/kubelet.log"
sleep 2

echo ""
echo "=== All components started in tmux ==="
echo ""
echo "API Server:  http://localhost:6443"
echo ""
echo "Component Logs:"
echo "  API Server:  $LOG_DIR/apiserver.log"
echo "  Scheduler:   $LOG_DIR/scheduler.log"
echo "  Controller:  $LOG_DIR/controller.log"
echo "  Kubelet:     $LOG_DIR/kubelet.log"
echo ""
echo "To view logs:"
echo "  tail -f $LOG_DIR/apiserver.log"
echo "  tail -f $LOG_DIR/scheduler.log"
echo "  tail -f $LOG_DIR/controller.log"
echo "  tail -f $LOG_DIR/kubelet.log"
echo ""
echo "To view component logs in tmux:"
echo "  API Server:  tmux capture-session -t apiserver -p"
echo "  Scheduler:   tmux capture-session -t scheduler -p"
echo "  Controller:  tmux capture-session -t controller -p"
echo "  Kubelet:     tmux capture-session -t kubelet -p"
echo "  etcd:        tmux capture-session -t etcd -p"
echo ""
echo "To attach to a component:"
echo "  tmux attach-session -t apiserver"
echo "  tmux attach-session -t kubelet"
echo "  (Press Ctrl+B then D to detach)"
echo ""
echo "Test the cluster:"
echo "  curl http://localhost:6443/api/v1/nodes"
echo "  curl http://localhost:6443/api/v1/namespaces/default/pods"
echo ""
echo "Create a test pod:"
echo "  curl -X POST http://localhost:6443/api/v1/namespaces/default/pods \\"
echo "    -H 'Content-Type: application/json' \\"
echo "    -d '{\"apiVersion\":\"v1\",\"kind\":\"Pod\",\"metadata\":{\"name\":\"test-pod\"},\"spec\":{\"containers\":[{\"name\":\"app\",\"image\":\"unikernel.img\"}]}}'"
echo ""
echo "To stop all components, run:"
echo "  ./stop-sirah.sh"
echo ""
