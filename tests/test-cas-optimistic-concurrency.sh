#!/bin/bash
# test-cas-optimistic-concurrency.sh - Test Compare-And-Swap with optimistic concurrency

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
echo "CAS Optimistic Concurrency Tests"
echo "=========================================="

# Test 1: Create pod and check resourceVersion
log_info "Test 1: Pod creation with resourceVersion"

POD_JSON=$(cat <<'EOF'
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "cas-test-1",
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

RESPONSE=$(curl -s -X POST "$API_URL/api/v1/namespaces/$NAMESPACE/pods" \
  -H "Content-Type: application/json" \
  -d "$POD_JSON")

# Extract resourceVersion
RV=$(echo "$RESPONSE" | grep -o '"resourceVersion":"[^"]*"' | head -1 | cut -d'"' -f4)

if [ -z "$RV" ]; then
    log_error "No resourceVersion in created pod"
fi

log_info "  Created pod with resourceVersion: $RV"
log_success "Pod has resourceVersion"

# Test 2: Update with matching resourceVersion (should succeed)
log_info "Test 2: Update with matching resourceVersion (should succeed)"

UPDATE_JSON=$(cat <<EOF
{
  "metadata": {
    "resourceVersion": "$RV"
  },
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

HTTP_CODE=$(curl -s -w "%{http_code}" -X PATCH "$API_URL/api/v1/namespaces/$NAMESPACE/pods/cas-test-1" \
  -H "Content-Type: application/json" \
  -d "$UPDATE_JSON" -o /tmp/patch_response.json)

if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "201" ]; then
    log_success "Update with matching resourceVersion succeeded (HTTP $HTTP_CODE)"
    
    # Extract new resourceVersion
    NEW_RV=$(cat /tmp/patch_response.json | grep -o '"resourceVersion":"[^"]*"' | head -1 | cut -d'"' -f4)
    log_info "  New resourceVersion: $NEW_RV"
    
    # Verify version changed
    if [ "$NEW_RV" != "$RV" ]; then
        log_success "resourceVersion was incremented after update"
    fi
else
    log_error "Update failed with HTTP $HTTP_CODE. Response: $(cat /tmp/patch_response.json)"
fi

# Test 3: Update with old resourceVersion (should fail with 409 Conflict)
log_info "Test 3: Update with old resourceVersion (should fail with 409)"

CONFLICTING_UPDATE=$(cat <<EOF
{
  "metadata": {
    "resourceVersion": "$RV"
  },
  "spec": {
    "containers": [
      {
        "name": "app",
        "image": "alpine:3.17"
      }
    ]
  }
}
EOF
)

HTTP_CODE=$(curl -s -w "%{http_code}" -X PATCH "$API_URL/api/v1/namespaces/$NAMESPACE/pods/cas-test-1" \
  -H "Content-Type: application/json" \
  -d "$CONFLICTING_UPDATE" -o /tmp/conflict_response.json)

if [ "$HTTP_CODE" = "409" ]; then
    log_success "CAS conflict detection works (HTTP 409)"
    RESPONSE_TEXT=$(cat /tmp/conflict_response.json)
    if echo "$RESPONSE_TEXT" | grep -q "Conflict\|modified"; then
        log_success "Conflict response message is appropriate"
    fi
else
    log_error "Expected 409 Conflict, got HTTP $HTTP_CODE"
fi

# Test 4: Concurrent updates from two sources (simulate race condition)
log_info "Test 4: Concurrent updates (race condition simulation)"

POD_JSON=$(cat <<'EOF'
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "cas-test-2",
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

RESPONSE=$(curl -s -X POST "$API_URL/api/v1/namespaces/$NAMESPACE/pods" \
  -H "Content-Type: application/json" \
  -d "$POD_JSON")

INITIAL_RV=$(echo "$RESPONSE" | grep -o '"resourceVersion":"[^"]*"' | head -1 | cut -d'"' -f4)
log_info "  Created pod with resourceVersion: $INITIAL_RV"

# First update (should succeed)
log_info "  First update..."
UPDATE1=$(cat <<EOF
{
  "metadata": {
    "resourceVersion": "$INITIAL_RV"
  },
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

RESPONSE1=$(curl -s -X PATCH "$API_URL/api/v1/namespaces/$NAMESPACE/pods/cas-test-2" \
  -H "Content-Type: application/json" \
  -d "$UPDATE1")

UPDATED_RV=$(echo "$RESPONSE1" | grep -o '"resourceVersion":"[^"]*"' | head -1 | cut -d'"' -f4)
log_info "  After first update, resourceVersion: $UPDATED_RV"

# Second update with old version (should fail)
log_info "  Second update with old resourceVersion..."
UPDATE2=$(cat <<EOF
{
  "metadata": {
    "resourceVersion": "$INITIAL_RV"
  },
  "spec": {
    "containers": [
      {
        "name": "app",
        "image": "alpine:3.17"
      }
    ]
  }
}
EOF
)

HTTP_CODE=$(curl -s -w "%{http_code}" -X PATCH "$API_URL/api/v1/namespaces/$NAMESPACE/pods/cas-test-2" \
  -H "Content-Type: application/json" \
  -d "$UPDATE2" -o /tmp/second_conflict.json)

if [ "$HTTP_CODE" = "409" ]; then
    log_success "Second concurrent update correctly rejected with 409"
else
    log_error "Expected 409 for concurrent update, got HTTP $HTTP_CODE"
fi

# Test 5: Retry with updated resourceVersion (eventual consistency)
log_info "Test 5: Retry with updated resourceVersion"

GET_RESPONSE=$(curl -s "$API_URL/api/v1/namespaces/$NAMESPACE/pods/cas-test-2")
CURRENT_RV=$(echo "$GET_RESPONSE" | grep -o '"resourceVersion":"[^"]*"' | head -1 | cut -d'"' -f4)
log_info "  Current resourceVersion from GET: $CURRENT_RV"

# Retry with current version
RETRY_UPDATE=$(cat <<EOF
{
  "metadata": {
    "resourceVersion": "$CURRENT_RV"
  },
  "spec": {
    "containers": [
      {
        "name": "app",
        "image": "alpine:3.16"
      }
    ]
  }
}
EOF
)

HTTP_CODE=$(curl -s -w "%{http_code}" -X PATCH "$API_URL/api/v1/namespaces/$NAMESPACE/pods/cas-test-2" \
  -H "Content-Type: application/json" \
  -d "$RETRY_UPDATE" -o /tmp/retry_response.json)

if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "201" ]; then
    log_success "Retry with current resourceVersion succeeded"
else
    log_error "Retry failed with HTTP $HTTP_CODE"
fi

echo ""
echo "=========================================="
log_success "All CAS optimistic concurrency tests passed!"
echo "=========================================="
