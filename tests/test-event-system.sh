#!/bin/bash
# test-event-system.sh - Test event system and buffering

set -e

API_URL="${API_URL:-http://localhost:6443}"
NAMESPACE="${NAMESPACE:-default}"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_success() { echo -e "${GREEN}✓ $1${NC}"; }
log_error() { echo -e "${RED}✗ $1${NC}"; exit 1; }
log_info() { echo -e "${YELLOW}→ $1${NC}"; }

echo "=========================================="
echo "Event System & Buffering Tests"
echo "=========================================="

# Test 1: Events on pod creation
log_info "Test 1: Event generation on pod creation"

POD_JSON=$(cat <<'EOF'
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "event-test-1",
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

# Start watching for events
WATCH_OUTPUT="/tmp/event_test1.log"
> $WATCH_OUTPUT
curl -s "$API_URL/api/v1/pods?watch=true" > $WATCH_OUTPUT 2>&1 &
WATCH_PID=$!
sleep 1

# Create pod
curl -s -X POST "$API_URL/api/v1/namespaces/$NAMESPACE/pods" \
  -H "Content-Type: application/json" \
  -d "$POD_JSON" > /dev/null

sleep 2
kill $WATCH_PID 2>/dev/null || true
wait $WATCH_PID 2>/dev/null || true

if grep -q '"type":"ADDED"' $WATCH_OUTPUT; then
    log_success "ADDED event generated on pod creation"
else
    log_error "No ADDED event. Output: $(cat $WATCH_OUTPUT | head -20)"
fi

# Test 2: Events on pod update
log_info "Test 2: Event generation on pod update"

WATCH_OUTPUT="/tmp/event_test2.log"
> $WATCH_OUTPUT
curl -s "$API_URL/api/v1/pods?watch=true" > $WATCH_OUTPUT 2>&1 &
WATCH_PID=$!
sleep 1

# Update pod
UPDATE_JSON=$(cat <<'EOF'
{
  "spec": {
    "containers": [
      {
        "name": "app",
        "image": "alpine:3.18"
      }
    ]
  }
}
EOF
)

curl -s -X PATCH "$API_URL/api/v1/namespaces/$NAMESPACE/pods/event-test-1" \
  -H "Content-Type: application/json" \
  -d "$UPDATE_JSON" > /dev/null

sleep 2
kill $WATCH_PID 2>/dev/null || true
wait $WATCH_PID 2>/dev/null || true

if grep -q '"type":"MODIFIED"' $WATCH_OUTPUT; then
    log_success "MODIFIED event generated on pod update"
else
    log_error "No MODIFIED event"
fi

# Test 3: Events on pod deletion
log_info "Test 3: Event generation on pod deletion"

WATCH_OUTPUT="/tmp/event_test3.log"
> $WATCH_OUTPUT
curl -s "$API_URL/api/v1/pods?watch=true" > $WATCH_OUTPUT 2>&1 &
WATCH_PID=$!
sleep 1

# Delete pod
curl -s -X DELETE "$API_URL/api/v1/namespaces/$NAMESPACE/pods/event-test-1" > /dev/null

sleep 2
kill $WATCH_PID 2>/dev/null || true
wait $WATCH_PID 2>/dev/null || true

if grep -q '"type":"DELETED"' $WATCH_OUTPUT; then
    log_success "DELETED event generated on pod deletion"
else
    log_error "No DELETED event"
fi

# Test 4: Event buffering (multiple events)
log_info "Test 4: Event buffering with multiple operations"

WATCH_OUTPUT="/tmp/event_test4.log"
> $WATCH_OUTPUT
curl -s "$API_URL/api/v1/pods?watch=true" > $WATCH_OUTPUT 2>&1 &
WATCH_PID=$!
sleep 1

# Create multiple pods
for i in {1..3}; do
    POD=$(cat <<EOF
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "event-test-multi-$i",
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
      -d "$POD" > /dev/null
    sleep 0.5
done

sleep 2
kill $WATCH_PID 2>/dev/null || true
wait $WATCH_PID 2>/dev/null || true

EVENT_COUNT=$(grep -c '"type":"ADDED"' $WATCH_OUTPUT)
if [ "$EVENT_COUNT" -ge 3 ]; then
    log_success "Event buffering works ($EVENT_COUNT events buffered)"
else
    log_error "Expected at least 3 ADDED events, got $EVENT_COUNT"
fi

# Test 5: Event retention and ordering
log_info "Test 5: Event ordering and retention"

WATCH_OUTPUT="/tmp/event_test5.log"
> $WATCH_OUTPUT
curl -s "$API_URL/api/v1/pods?watch=true" > $WATCH_OUTPUT 2>&1 &
WATCH_PID=$!
sleep 1

# Create, update, delete sequence
POD_JSON=$(cat <<'EOF'
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "event-test-sequence",
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

# CREATE
curl -s -X POST "$API_URL/api/v1/namespaces/$NAMESPACE/pods" \
  -H "Content-Type: application/json" \
  -d "$POD_JSON" > /dev/null
sleep 1

# UPDATE
curl -s -X PATCH "$API_URL/api/v1/namespaces/$NAMESPACE/pods/event-test-sequence" \
  -H "Content-Type: application/json" \
  -d '{"spec": {"containers": [{"name": "app", "image": "alpine:3.18"}]}}' > /dev/null
sleep 1

# DELETE
curl -s -X DELETE "$API_URL/api/v1/namespaces/$NAMESPACE/pods/event-test-sequence" > /dev/null

sleep 2
kill $WATCH_PID 2>/dev/null || true
wait $WATCH_PID 2>/dev/null || true

# Check event order
ADDED_LINE=$(grep -n '"type":"ADDED"' $WATCH_OUTPUT | head -1 | cut -d: -f1)
MODIFIED_LINE=$(grep -n '"type":"MODIFIED"' $WATCH_OUTPUT | head -1 | cut -d: -f1)
DELETED_LINE=$(grep -n '"type":"DELETED"' $WATCH_OUTPUT | head -1 | cut -d: -f1)

if [ -n "$ADDED_LINE" ] && [ -n "$MODIFIED_LINE" ] && [ -n "$DELETED_LINE" ]; then
    if [ "$ADDED_LINE" -lt "$MODIFIED_LINE" ] && [ "$MODIFIED_LINE" -lt "$DELETED_LINE" ]; then
        log_success "Events are in correct order (ADDED -> MODIFIED -> DELETED)"
    else
        log_error "Events are out of order"
    fi
else
    log_error "Not all event types present"
fi

echo ""
echo "=========================================="
log_success "All event system tests passed!"
echo "=========================================="
