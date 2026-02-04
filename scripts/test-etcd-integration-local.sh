#!/bin/bash

# Phase 2A Week 2 - etcd Integration Test Suite (No Docker)
# Tests all 5 CRUD operations with etcd backing
# Start etcd manually first: etcd --listen-client-urls http://localhost:2379 --advertise-client-urls http://localhost:2379

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test results
PASSED=0
FAILED=0
TOTAL=0

# Configuration
ETCD_PORT=2379
API_PORT=6443
API_PID=""

# Cleanup function
cleanup() {
    echo -e "\n${BLUE}=== Cleanup ===${NC}"
    
    # Kill API server if running
    if [ ! -z "$API_PID" ]; then
        echo "Stopping API server (PID: $API_PID)..."
        kill $API_PID 2>/dev/null || true
        sleep 1
    fi
    
    echo -e "${GREEN}Cleanup complete${NC}"
    echo -e "\n${YELLOW}Note: Please manually stop etcd if you started it${NC}"
}

# Set trap to cleanup on exit
trap cleanup EXIT INT TERM

# Test function
run_test() {
    local test_name="$1"
    local method="$2"
    local endpoint="$3"
    local data="$4"
    local expected_code="$5"
    
    TOTAL=$((TOTAL + 1))
    
    echo -e "\n${BLUE}Test $TOTAL: $test_name${NC}"
    echo "  Method: $method $endpoint"
    
    # Build curl command
    if [ -z "$data" ]; then
        response=$(curl -s -w "\n%{http_code}" -X $method "http://localhost:$API_PORT$endpoint" \
            -H "Content-Type: application/json" 2>/dev/null || echo "000")
    else
        response=$(curl -s -w "\n%{http_code}" -X $method "http://localhost:$API_PORT$endpoint" \
            -H "Content-Type: application/json" \
            -d "$data" 2>/dev/null || echo "000")
    fi
    
    # Extract body and status code
    http_code=$(echo "$response" | tail -n 1)
    body=$(echo "$response" | sed '$d')
    
    echo "  Response Code: $http_code (expected: $expected_code)"
    
    if [ "$http_code" = "$expected_code" ]; then
        echo -e "  ${GREEN}✓ PASSED${NC}"
        PASSED=$((PASSED + 1))
        
        # Return body for further processing
        echo "$body"
        return 0
    else
        echo -e "  ${RED}✗ FAILED${NC}"
        if [ ! -z "$body" ]; then
            echo "  Body: $(echo "$body" | head -c 200)..."
        fi
        FAILED=$((FAILED + 1))
        return 1
    fi
}

echo -e "${BLUE}╔════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  Phase 2A Week 2 - etcd Integration Test Suite          ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════╝${NC}"

# Step 1: Check if etcd is running
echo -e "\n${BLUE}=== Checking etcd ===${NC}"
if ! curl -s http://localhost:$ETCD_PORT/health > /dev/null 2>&1; then
    echo -e "${RED}✗ etcd is not running on port $ETCD_PORT${NC}"
    echo -e "${YELLOW}Start etcd with: etcd --listen-client-urls http://localhost:2379 --advertise-client-urls http://localhost:2379${NC}"
    exit 1
fi
echo -e "${GREEN}✓ etcd is running on port $ETCD_PORT${NC}"

# Step 2: Start API server
echo -e "\n${BLUE}=== Starting Sirah API Server ===${NC}"
cd sirah
./bin/sirah-apiserver --etcd http://localhost:$ETCD_PORT > /tmp/apiserver.log 2>&1 &
API_PID=$!
echo "API Server PID: $API_PID"

# Wait for server to start and initialize
sleep 3

if ! kill -0 $API_PID 2>/dev/null; then
    echo -e "${RED}✗ API Server failed to start${NC}"
    echo -e "${YELLOW}Check logs:${NC}"
    cat /tmp/apiserver.log | head -20
    exit 1
fi
echo -e "${GREEN}✓ API Server is running${NC}"

# Verify API server is responding
if ! curl -s http://localhost:$API_PORT/healthz > /dev/null 2>&1; then
    echo -e "${YELLOW}⚠ Waiting for API server to fully initialize...${NC}"
    sleep 2
fi

# Step 3: Run tests
echo -e "\n${BLUE}╔════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  Running CRUD Tests                                     ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════╝${NC}"

# Test 1: Create Pod
echo -e "\n${YELLOW}Test Group 1: Pod Creation${NC}"
create_response=$(run_test "Create pod" "POST" "/api/v1/namespaces/default/pods" \
    '{"apiVersion":"v1","kind":"Pod","metadata":{"name":"test-pod"},"spec":{"containers":[{"name":"main","image":"busybox"}]}}' \
    "201")

# Extract resourceVersion from creation response
resource_version=$(echo "$create_response" | grep -o '"resourceVersion":"[^"]*"' | head -1 | cut -d'"' -f4)
if [ ! -z "$resource_version" ]; then
    echo "  ResourceVersion: $resource_version"
fi

# Test 2: Get Pod
echo -e "\n${YELLOW}Test Group 2: Pod Retrieval${NC}"
get_response=$(run_test "Get pod" "GET" "/api/v1/namespaces/default/pods/test-pod" "" "200")

# Test 3: List Pods
echo -e "\n${YELLOW}Test Group 3: Pod Listing${NC}"
run_test "List pods" "GET" "/api/v1/namespaces/default/pods" "" "200" > /dev/null

# Test 4: Patch Pod (Success)
echo -e "\n${YELLOW}Test Group 4: Pod Update${NC}"
run_test "Patch pod (valid)" "PATCH" "/api/v1/namespaces/default/pods/test-pod" \
    '{"metadata":{"annotations":{"key":"value"}}}' "200" > /dev/null

# Test 5: Patch Pod with Old ResourceVersion (Conflict)
echo -e "\n${YELLOW}Test Group 5: Optimistic Locking (CAS)${NC}"
run_test "Patch pod (old version - should conflict)" "PATCH" "/api/v1/namespaces/default/pods/test-pod" \
    '{"resourceVersion":"old-version-12345","metadata":{"annotations":{"key":"value2"}}}' "409" > /dev/null

# Test 6: Delete Pod
echo -e "\n${YELLOW}Test Group 6: Pod Deletion${NC}"
run_test "Delete pod" "DELETE" "/api/v1/namespaces/default/pods/test-pod" "" "204" > /dev/null

# Test 7: Get Non-existent Pod (404)
echo -e "\n${YELLOW}Test Group 7: Error Handling${NC}"
run_test "Get non-existent pod" "GET" "/api/v1/namespaces/default/pods/nonexistent" "" "404" > /dev/null

# Test 8: Create pod with invalid JSON (400)
echo -e "\n${YELLOW}Test Group 8: Invalid Input Handling${NC}"
run_test "Create pod with invalid JSON" "POST" "/api/v1/namespaces/default/pods" \
    'invalid json not valid' "400" > /dev/null

# Step 4: Verify etcd contains data
echo -e "\n${BLUE}=== Verifying etcd Data ===${NC}"
echo "Checking etcd for pod keys using etcdctl..."
if command -v etcdctl &> /dev/null; then
    etcd_output=$(etcdctl get --prefix /sirah/pods/ 2>/dev/null || echo "")
    if [ -z "$etcd_output" ]; then
        echo -e "${YELLOW}⚠ No pods found in etcd (keys: /sirah/pods/*)${NC}"
    else
        echo -e "${GREEN}✓ Found pod data in etcd${NC}"
        echo "$etcd_output" | head -10
    fi
else
    echo -e "${YELLOW}⚠ etcdctl not found, skipping etcd verification${NC}"
fi

# Summary
echo -e "\n${BLUE}╔════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  Test Results Summary                                   ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════╝${NC}"
echo "Total Tests:  $TOTAL"
echo -e "Passed:       ${GREEN}$PASSED${NC}"
echo -e "Failed:       ${RED}$FAILED${NC}"

if [ $FAILED -eq 0 ]; then
    echo -e "\n${GREEN}✓ All tests passed!${NC}"
    echo -e "\n${YELLOW}Next steps:${NC}"
    echo "  1. Verify pod data persisted in etcd"
    echo "  2. Run Phase 2B: 3-node etcd cluster setup"
    exit 0
else
    echo -e "\n${RED}✗ Some tests failed${NC}"
    echo -e "\n${YELLOW}Check API server logs:${NC}"
    tail -20 /tmp/apiserver.log
    exit 1
fi
