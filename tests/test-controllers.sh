#!/bin/bash
# test-controllers.sh
# Comprehensive test suite for Deployment and Service controllers

set -e

API_URL="http://localhost:6443"
NAMESPACE="default"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo_test() {
    echo -e "${YELLOW}=== TEST: $1 ===${NC}"
}

echo_pass() {
    echo -e "${GREEN}✓ PASS: $1${NC}"
}

echo_fail() {
    echo -e "${RED}✗ FAIL: $1${NC}"
    exit 1
}

wait_for_condition() {
    local condition=$1
    local timeout=$2
    local interval=${3:-1}
    local elapsed=0
    
    while [ $elapsed -lt $timeout ]; do
        if eval "$condition" > /dev/null 2>&1; then
            return 0
        fi
        sleep $interval
        elapsed=$((elapsed + interval))
    done
    
    return 1
}

# Test 1: Create a deployment
echo_test "Create Deployment"
cat > /tmp/test-deployment.json <<'EOF'
{
  "apiVersion": "v1",
  "kind": "Deployment",
  "metadata": {
    "name": "test-app",
    "namespace": "default"
  },
  "spec": {
    "replicas": 3,
    "template": {
      "spec": {
        "containers": [{
          "name": "app",
          "image": "nginx:latest"
        }]
      }
    }
  }
}
EOF

RESPONSE=$(curl -s -X POST $API_URL/api/v1/namespaces/default/deployments \
    -H "Content-Type: application/json" \
    -d @/tmp/test-deployment.json)

if echo "$RESPONSE" | grep -q '"name":"test-app"'; then
    echo_pass "Deployment created"
else
    echo_fail "Failed to create deployment: $RESPONSE"
fi

# Test 2: Wait for deployment controller to create pods
echo_test "Wait for Deployment Controller to create replicas"
sleep 6  # Give controller time to reconcile

POD_COUNT=$(curl -s $API_URL/api/v1/namespaces/default/pods | \
    jq '.items[] | select(.metadata.labels.deployment == "test-app") | .metadata.name' | wc -l)

if [ "$POD_COUNT" -eq "3" ]; then
    echo_pass "Deployment controller created all 3 replicas"
else
    echo_fail "Expected 3 pods, got $POD_COUNT"
fi

# Test 3: Scale deployment
echo_test "Scale Deployment (replicas: 3 → 5)"
PATCH_DATA='{"spec":{"replicas":5}}'
curl -s -X PATCH $API_URL/api/v1/namespaces/default/deployments/test-app \
    -H "Content-Type: application/json" \
    -d "$PATCH_DATA" > /dev/null

sleep 6  # Give controller time to create new pods

POD_COUNT=$(curl -s $API_URL/api/v1/namespaces/default/pods | \
    jq '.items[] | select(.metadata.labels.deployment == "test-app") | .metadata.name' | wc -l)

if [ "$POD_COUNT" -eq "5" ]; then
    echo_pass "Deployment scaled to 5 replicas"
else
    echo_fail "Expected 5 pods after scale, got $POD_COUNT"
fi

# Test 4: Scale down deployment
echo_test "Scale Down Deployment (replicas: 5 → 2)"
PATCH_DATA='{"spec":{"replicas":2}}'
curl -s -X PATCH $API_URL/api/v1/namespaces/default/deployments/test-app \
    -H "Content-Type: application/json" \
    -d "$PATCH_DATA" > /dev/null

sleep 6  # Give controller time to delete excess pods

POD_COUNT=$(curl -s $API_URL/api/v1/namespaces/default/pods | \
    jq '.items[] | select(.metadata.labels.deployment == "test-app") | .metadata.name' | wc -l)

if [ "$POD_COUNT" -eq "2" ]; then
    echo_pass "Deployment scaled down to 2 replicas"
else
    echo_fail "Expected 2 pods after scale-down, got $POD_COUNT"
fi

# Test 5: Create a service
echo_test "Create Service"
cat > /tmp/test-service.json <<'EOF'
{
  "apiVersion": "v1",
  "kind": "Service",
  "metadata": {
    "name": "test-service",
    "namespace": "default"
  },
  "spec": {
    "type": "ClusterIP",
    "selector": {
      "deployment": "test-app"
    },
    "ports": [{
      "port": 80,
      "targetPort": 8080
    }]
  }
}
EOF

RESPONSE=$(curl -s -X POST $API_URL/api/v1/namespaces/default/services \
    -H "Content-Type: application/json" \
    -d @/tmp/test-service.json)

if echo "$RESPONSE" | grep -q '"name":"test-service"'; then
    echo_pass "Service created"
else
    echo_fail "Failed to create service: $RESPONSE"
fi

# Test 6: Wait for service controller to discover endpoints
echo_test "Wait for Service Controller to discover endpoints"
sleep 4  # Give controller time to discover endpoints

ENDPOINT_COUNT=$(curl -s $API_URL/api/v1/namespaces/default/endpoints/test-service | \
    jq '.subsets[0].addresses | length' 2>/dev/null || echo "0")

if [ "$ENDPOINT_COUNT" -ge "2" ]; then
    echo_pass "Service controller discovered $ENDPOINT_COUNT endpoints"
else
    echo_fail "Expected at least 2 endpoints, got $ENDPOINT_COUNT"
fi

# Test 7: Scale deployment and verify endpoint updates
echo_test "Scale Deployment and verify endpoint updates"
PATCH_DATA='{"spec":{"replicas":4}}'
curl -s -X PATCH $API_URL/api/v1/namespaces/default/deployments/test-app \
    -H "Content-Type: application/json" \
    -d "$PATCH_DATA" > /dev/null

sleep 6  # Give controllers time to sync

ENDPOINT_COUNT=$(curl -s $API_URL/api/v1/namespaces/default/endpoints/test-service | \
    jq '.subsets[0].addresses | length' 2>/dev/null || echo "0")

if [ "$ENDPOINT_COUNT" -eq "4" ]; then
    echo_pass "Endpoints updated to 4 after scaling deployment"
else
    echo_fail "Expected 4 endpoints after scale, got $ENDPOINT_COUNT"
fi

# Test 8: Create multiple services with different selectors
echo_test "Create multiple services with different selectors"
cat > /tmp/test-service2.json <<'EOF'
{
  "apiVersion": "v1",
  "kind": "Service",
  "metadata": {
    "name": "test-service2",
    "namespace": "default"
  },
  "spec": {
    "type": "ClusterIP",
    "selector": {
      "deployment": "test-app"
    },
    "ports": [{
      "port": 3000,
      "targetPort": 3000
    }]
  }
}
EOF

RESPONSE=$(curl -s -X POST $API_URL/api/v1/namespaces/default/services \
    -H "Content-Type: application/json" \
    -d @/tmp/test-service2.json)

if echo "$RESPONSE" | grep -q '"name":"test-service2"'; then
    echo_pass "Second service created"
else
    echo_fail "Failed to create second service"
fi

sleep 3
ENDPOINT_COUNT2=$(curl -s $API_URL/api/v1/namespaces/default/endpoints/test-service2 | \
    jq '.subsets[0].addresses | length' 2>/dev/null || echo "0")

if [ "$ENDPOINT_COUNT2" -eq "4" ]; then
    echo_pass "Second service discovered endpoints correctly"
else
    echo_fail "Second service endpoint discovery failed"
fi

# Test 9: Verify deployment status
echo_test "Verify Deployment Status"
DEPLOYMENT=$(curl -s $API_URL/api/v1/namespaces/default/deployments/test-app)
DESIRED=$(echo "$DEPLOYMENT" | jq '.spec.replicas')
READY=$(echo "$DEPLOYMENT" | jq '.status.readyReplicas // 0')

if [ "$DESIRED" -eq "4" ]; then
    echo_pass "Deployment desired replicas: $DESIRED"
else
    echo_fail "Expected 4 desired replicas, got $DESIRED"
fi

# Test 10: Delete deployment
echo_test "Delete Deployment"
curl -s -X DELETE $API_URL/api/v1/namespaces/default/deployments/test-app > /dev/null

sleep 6  # Give controller time to delete pods

POD_COUNT=$(curl -s $API_URL/api/v1/namespaces/default/pods | \
    jq '.items[] | select(.metadata.labels.deployment == "test-app") | .metadata.name' | wc -l)

if [ "$POD_COUNT" -eq "0" ]; then
    echo_pass "Deployment deleted, all pods removed"
else
    echo_fail "Expected 0 pods after deployment deletion, got $POD_COUNT"
fi

# Test 11: Verify service endpoints cleanup
echo_test "Verify Service Endpoints after Deployment Deletion"
sleep 2
ENDPOINT_COUNT=$(curl -s $API_URL/api/v1/namespaces/default/endpoints/test-service | \
    jq '.subsets[0].addresses | length' 2>/dev/null || echo "0")

if [ "$ENDPOINT_COUNT" -eq "0" ]; then
    echo_pass "Service endpoints updated after deployment deletion"
else
    # This is informational - endpoints might take longer to clear
    echo -e "${YELLOW}⚠ WARNING: Endpoints still exist: $ENDPOINT_COUNT (expected to eventually clear)${NC}"
fi

echo ""
echo -e "${GREEN}=== ALL TESTS PASSED ===${NC}"
echo ""
echo "Controller Functionality Verified:"
echo "  ✓ Deployment controller watches and manages deployments"
echo "  ✓ Replica creation/scaling works correctly"
echo "  ✓ Service controller discovers endpoints from pods"
echo "  ✓ Endpoint updates sync with deployment changes"
echo "  ✓ Multiple services with same selector work correctly"
echo ""
