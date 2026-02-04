#!/bin/bash
# Start all Sirah services needed for pod logging to work

set -e

SIRAH_DIR="/mnt/c/projects/sirah-c/sirah"
LOG_DIR="/tmp/sirah-logs"
ETCD_ADDR="http://localhost:2379"

echo "=== Sirah Full Stack Startup ==="
echo ""

# Check prerequisites
echo "[1] Checking prerequisites..."

if ! command -v qemu-system-x86_64 &> /dev/null; then
    echo "✗ QEMU is not installed"
    echo "  Install with: sudo apt install qemu-system-x86"
    exit 1
fi
echo "  ✓ QEMU installed"

if ! command -v etcd &> /dev/null; then
    echo "✗ etcd is not installed"
    echo "  Install with: sudo apt install etcd or use snap install etcd"
    exit 1
fi
echo "  ✓ etcd installed"

# Create log directories
echo ""
echo "[2] Creating log directories..."
mkdir -p "$LOG_DIR"
mkdir -p "$LOG_DIR/pods"
echo "  ✓ Created $LOG_DIR"

# Check if services are already running
echo ""
echo "[3] Checking for existing services..."
APISERVER_PID=$(pgrep -f "sirah-apiserver" || echo "")
CONTROLLER_PID=$(pgrep -f "sirah-controller" || echo "")
SCHEDULER_PID=$(pgrep -f "sirah-scheduler" || echo "")

if [ -n "$APISERVER_PID" ] || [ -n "$CONTROLLER_PID" ] || [ -n "$SCHEDULER_PID" ]; then
    echo "  ⚠ Some Sirah services are already running"
    echo "    API Server:     $APISERVER_PID"
    echo "    Controller:     $CONTROLLER_PID"
    echo "    Scheduler:      $SCHEDULER_PID"
    echo ""
    read -p "  Continue anyway? (y/n) " -n 1 -r
    echo ""
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "  Aborted"
        exit 1
    fi
fi

# Start etcd if not running
echo ""
echo "[4] Checking etcd..."
if ! nc -zv localhost 2379 2>&1 | grep -q "succeeded"; then
    echo "  Starting etcd..."
    mkdir -p /tmp/sirah-etcd
    etcd --listen-client-urls http://localhost:2379 \
         --advertise-client-urls http://localhost:2379 \
         --data-dir /tmp/sirah-etcd \
         2>&1 > "$LOG_DIR/etcd.log" &
    ETCD_PID=$!
    echo "  ✓ etcd started (PID: $ETCD_PID)"
    sleep 2
else
    echo "  ✓ etcd already running"
fi

# Start API Server
echo ""
echo "[5] Starting API Server..."
cd "$SIRAH_DIR"
if [ ! -f "bin/sirah-apiserver" ]; then
    echo "  Building..."
    make clean
    make
fi
./bin/sirah-apiserver --etcd "$ETCD_ADDR" \
    2>&1 | tee "$LOG_DIR/apiserver.log" &
APISERVER_PID=$!
echo "  ✓ API Server started (PID: $APISERVER_PID)"
sleep 2

# Wait for API server to be ready
echo ""
echo "[6] Waiting for API Server to be ready..."
for i in {1..30}; do
    if curl -s -u admin:admin "http://localhost:6443/healthz" > /dev/null 2>&1; then
        echo "  ✓ API Server is ready"
        break
    fi
    echo "  Attempt $i/30..."
    sleep 1
done

# Start Scheduler
echo ""
echo "[7] Starting Scheduler..."
./bin/sirah-scheduler 2>&1 | tee "$LOG_DIR/scheduler.log" &
SCHEDULER_PID=$!
echo "  ✓ Scheduler started (PID: $SCHEDULER_PID)"
sleep 1

# Start Pod Controller (THIS IS THE CRITICAL ONE FOR LOGS!)
echo ""
echo "[8] Starting Pod Controller (CRITICAL FOR POD LOGS)..."
./bin/sirah-controller 2>&1 | tee "$LOG_DIR/controller.log" &
CONTROLLER_PID=$!
echo "  ✓ Pod Controller started (PID: $CONTROLLER_PID)"
sleep 1

# Summary
echo ""
echo "=== Services Started ==="
echo ""
echo "API Server:     ✓ PID $APISERVER_PID  (http://localhost:6443)"
echo "Scheduler:      ✓ PID $SCHEDULER_PID"
echo "Pod Controller: ✓ PID $CONTROLLER_PID  ← Creates logs in /tmp/sirah-logs/pods/"
echo "etcd:           ✓ PID $ETCD_PID"
echo ""
echo "=== Next Steps ==="
echo ""
echo "1. Create a test pod:"
echo "   curl -X POST -u admin:admin -H 'Content-Type: application/json' \\"
echo "     'http://localhost:6443/api/v1/namespaces/default/pods' \\"
echo "     -d '{\"apiVersion\":\"v1\",\"kind\":\"Pod\",\"metadata\":{\"name\":\"test-$(date +%s)\"},\"spec\":{\"containers\":[{\"name\":\"app\",\"image\":\"/tmp/sirah-unikernels/test-kernel.img\",\"resources\":{\"limits\":{\"memory\":\"128Mi\",\"cpu\":\"1\"}}}]}}'"
echo ""
echo "2. Watch the controller logs:"
echo "   tail -f $LOG_DIR/controller.log | grep -E 'SYNC|SPAWN|FETCH'"
echo ""
echo "3. Check for pod logs (after ~5 seconds):"
echo "   ls -la /tmp/sirah-logs/pods/"
echo ""
echo "4. Get logs via curl:"
echo "   curl -u admin:admin 'http://localhost:6443/api/v1/namespaces/default/pods/test-*/log'"
echo ""
echo "=== Monitoring ==="
echo ""
echo "API Server logs:     tail -f $LOG_DIR/apiserver.log"
echo "Controller logs:     tail -f $LOG_DIR/controller.log"
echo "Scheduler logs:      tail -f $LOG_DIR/scheduler.log"
echo "Pod logs:            ls -la /tmp/sirah-logs/pods/default/*/"
echo "QEMU processes:      pgrep -a qemu-system"
echo ""
echo "=== Cleanup ==="
echo ""
echo "To stop all services:"
echo "  killall sirah-apiserver sirah-controller sirah-scheduler etcd"
echo ""
echo "To clean up logs:"
echo "  rm -rf /tmp/sirah-logs /tmp/sirah-etcd"
echo ""
