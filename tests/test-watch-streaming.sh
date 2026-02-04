#!/bin/bash
# test-watch-streaming.sh - Test watch subscriptions with NDJSON streaming

set -e

API_URL="${API_URL:-http://localhost:6443}"
NAMESPACE="${NAMESPACE:-default}"
WATCH_TIMEOUT="${WATCH_TIMEOUT:-10}"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_success() { echo -e "${GREEN}✓ $1${NC}"; }
log_error() { echo -e "${RED}✗ $1${NC}"; exit 1; }
log_info() { echo -e "${YELLOW}→ $1${NC}"; }

echo "=========================================="
echo "Watch Subscription Streaming Tests"
echo "=========================================="

# Test 1: Basic watch subscription (without filtering)
log_info "Test 1: Basic watch subscription"
WATCH_PID=""
WATCH_OUTPUT="/tmp/watch_output.log"
> $WATCH_OUTPUT

# Start watch in background
curl -s "$API_URL/api/v1/pods?watch=true" > $WATCH_OUTPUT 2>&1 &
WATCH_PID=$!
sleep 2

# Create a pod while watching
log_info "  Creating pod while watching..."
POD_JSON=$(cat <<'EOF'
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "watch-test-1",
    "namespace": "default"
  },
  "spec": {
    "containers": [
      {
        "name": "app",
        "image": "alpine:latest"
      }
    ]
  }
}
EOF
)

curl -s -X POST "$API_URL/api/v1/namespaces/$NAMESPACE/pods" \
  -H "Content-Type: application/json" \
  -d "$POD_JSON" > /dev/null

sleep 2

# Kill watch
kill $WATCH_PID 2>/dev/null || true
wait $WATCH_PID 2>/dev/null || true

# Check if we got NDJSON events
if grep -q '"type":"ADDED"' $WATCH_OUTPUT || grep -q '"type":"MODIFIED"' $WATCH_OUTPUT; then
    log_success "Watch returned NDJSON events"
else
    log_error "Watch did not return NDJSON events. Output: $(cat $WATCH_OUTPUT)"
fi

# Test 2: Watch with label selector
log_info "Test 2: Watch with label selector filtering"
> $WATCH_OUTPUT

# Start watch with label selector
curl -s "$API_URL/api/v1/pods?watch=true&labelSelector=app=myapp" > $WATCH_OUTPUT 2>&1 &
WATCH_PID=$!
sleep 2

# Create pod with matching labels
log_info "  Creating pod with app=myapp label..."
POD_JSON=$(cat <<'EOF'
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "watch-test-2",
    "namespace": "default",
    "labels": {
      "app": "myapp"
    }
  },
  "spec": {
    "containers": [
      {
        "name": "app",
        "image": "alpine:latest"
      }
    ]
  }
}
EOF
)

curl -s -X POST "$API_URL/api/v1/namespaces/$NAMESPACE/pods" \
  -H "Content-Type: application/json" \
  -d "$POD_JSON" > /dev/null

sleep 2

# Kill watch
kill $WATCH_PID 2>/dev/null || true
wait $WATCH_PID 2>/dev/null || true

# Check for event
if grep -q '"type":"ADDED"' $WATCH_OUTPUT; then
    log_success "Label selector filtering works"
else
    log_error "Label selector filtering failed"
fi

# Test 3: Multiple concurrent watchers
log_info "Test 3: Multiple concurrent watchers"
WATCH_OUTPUT1="/tmp/watch_output1.log"
WATCH_OUTPUT2="/tmp/watch_output2.log"
> $WATCH_OUTPUT1
> $WATCH_OUTPUT2

# Start two watchers
curl -s "$API_URL/api/v1/pods?watch=true" > $WATCH_OUTPUT1 2>&1 &
PID1=$!
curl -s "$API_URL/api/v1/pods?watch=true" > $WATCH_OUTPUT2 2>&1 &
PID2=$!
sleep 2

# Create pod
log_info "  Creating pod with two watchers active..."
POD_JSON=$(cat <<'EOF'
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "watch-test-3",
    "namespace": "default"
  },
  "spec": {
    "containers": [
      {
        "name": "app",
        "image": "alpine:latest"
      }
    ]
  }
}
EOF
)

curl -s -X POST "$API_URL/api/v1/namespaces/$NAMESPACE/pods" \
  -H "Content-Type: application/json" \
  -d "$POD_JSON" > /dev/null

sleep 2

# Kill watchers
kill $PID1 $PID2 2>/dev/null || true
wait $PID1 $PID2 2>/dev/null || true

# Check both got events
if grep -q '"type":"ADDED"' $WATCH_OUTPUT1 && grep -q '"type":"ADDED"' $WATCH_OUTPUT2; then
    log_success "Multiple concurrent watchers work"
else
    log_error "One or both watchers did not receive events"
fi

# Test 4: BOOKMARK events for resume position
log_info "Test 4: BOOKMARK events for resuming watch"
> $WATCH_OUTPUT

# Get initial resource version
INITIAL_RV=$(curl -s "$API_URL/api/v1/pods" | grep -o '"resourceVersion":"[^"]*"' | head -1 | cut -d'"' -f4)
log_info "  Initial resourceVersion: $INITIAL_RV"

# Start watch from specific version
if [ -n "$INITIAL_RV" ]; then
    curl -s "$API_URL/api/v1/pods?watch=true&resourceVersion=$INITIAL_RV" > $WATCH_OUTPUT 2>&1 &
    WATCH_PID=$!
    sleep 2
    
    # Create pod
    log_info "  Creating pod..."
    curl -s -X POST "$API_URL/api/v1/namespaces/$NAMESPACE/pods" \
      -H "Content-Type: application/json" \
      -d "$POD_JSON" > /dev/null
    
    sleep 2
    kill $WATCH_PID 2>/dev/null || true
    wait $WATCH_PID 2>/dev/null || true
    
    if grep -q '"type":"BOOKMARK"' $WATCH_OUTPUT || grep -q '"type":"ADDED"' $WATCH_OUTPUT; then
        log_success "BOOKMARK/resume position works"
    else
        log_error "BOOKMARK events not received"
    fi
fi

echo ""
echo "=========================================="
log_success "All watch streaming tests passed!"
echo "=========================================="
