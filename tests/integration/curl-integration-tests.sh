#!/bin/bash
# Sirah API Integration Tests (using curl directly)
# Tests actual YAML resources against the Sirah API server via curl
# Requires: Sirah API server running on localhost:6443
#          etcd running on localhost:2379
#          Python 3 with PyYAML (or yq)

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# Configuration
API_SERVER="http://localhost:6443"
TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
YAML_DIR="${TEST_DIR}/fixtures"

# Helper functions
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_test() {
    echo -e "${YELLOW}[TEST]${NC} $1"
}

log_pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
}

log_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
}

assert_http_success() {
    local test_name="$1"
    local http_code="$2"
    
    TESTS_RUN=$((TESTS_RUN + 1))
    
    # Accept 2xx and 3xx status codes
    if [[ $http_code =~ ^[23][0-9]{2}$ ]]; then
        log_pass "$test_name (HTTP $http_code)"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        log_fail "$test_name (HTTP $http_code)"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

assert_contains() {
    local test_name="$1"
    local haystack="$2"
    local needle="$3"
    
    TESTS_RUN=$((TESTS_RUN + 1))
    
    if echo "$haystack" | grep -q "$needle"; then
        log_pass "$test_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_fail "$test_name"
        echo "  Expected to find: $needle"
        echo "  In: $(echo "$haystack" | head -c 200)..."
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
}

# Convert YAML to JSON using Python or yq
yaml_to_json() {
    local yaml_file="$1"
    
    if command -v yq &> /dev/null; then
        yq -o json "$yaml_file"
    elif command -v python3 &> /dev/null; then
        python3 << PYSCRIPT
import yaml
import json
with open('$yaml_file', 'r') as f:
    data = yaml.safe_load(f)
    print(json.dumps(data))
PYSCRIPT
    else
        log_error "Neither yq nor python3 found. Cannot convert YAML to JSON."
        return 1
    fi
}

# Check prerequisites
check_prerequisites() {
    log_test "Checking prerequisites"
    
    if [ ! -d "$YAML_DIR" ]; then
        log_error "Fixtures directory not found at $YAML_DIR"
        exit 1
    fi
    log_info "Fixtures directory found: $YAML_DIR"
    
    # Check API server connectivity
    if ! curl -s -o /dev/null -w "%{http_code}" "$API_SERVER/healthz" | grep -q "200"; then
        log_error "Cannot connect to Sirah API server at $API_SERVER"
        exit 1
    fi
    log_info "API server is reachable at $API_SERVER"
    
    # Check YAML converter
    if ! command -v yq &> /dev/null && ! command -v python3 &> /dev/null; then
        log_error "Neither yq nor python3 available for YAML conversion"
        exit 1
    fi
    log_info "YAML converter available"
}

# ============================================================================
# TESTS: PODS
# ============================================================================

test_pod_create() {
    log_test "Create pod"
    
    local json=$(yaml_to_json "$YAML_DIR/test-pod.yaml")
    local http_code=$(curl -s -w "%{http_code}" -o /tmp/response.json \
        -X POST -H "Content-Type: application/json" \
        -d "$json" \
        "$API_SERVER/api/v1/namespaces/default/pods")
    
    assert_http_success "Pod created" "$http_code"
    
    # Check response contains pod name
    response=$(cat /tmp/response.json)
    assert_contains "Response contains pod name" "$response" "test-pod-001"
}

test_pod_get() {
    log_test "Get pod by name"
    
    local http_code=$(curl -s -w "%{http_code}" -o /tmp/response.json \
        "$API_SERVER/api/v1/namespaces/default/pods/test-pod-001")
    
    assert_http_success "Pod retrieved" "$http_code"
    
    response=$(cat /tmp/response.json)
    assert_contains "Response contains pod name" "$response" "test-pod-001"
}

test_pod_list() {
    log_test "List pods in namespace"
    
    local http_code=$(curl -s -w "%{http_code}" -o /tmp/response.json \
        "$API_SERVER/api/v1/namespaces/default/pods")
    
    assert_http_success "Pods listed" "$http_code"
    
    response=$(cat /tmp/response.json)
    assert_contains "List contains test pod" "$response" "test-pod-001"
}

test_pod_delete() {
    log_test "Delete pod"
    
    local http_code=$(curl -s -w "%{http_code}" -o /tmp/response.json \
        -X DELETE \
        "$API_SERVER/api/v1/namespaces/default/pods/test-pod-001")
    
    # 204 = No Content (successful delete)
    if [[ $http_code == 204 ]]; then
        log_pass "Pod deleted (HTTP 204)"
        TESTS_RUN=$((TESTS_RUN + 1))
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        assert_http_success "Pod deleted" "$http_code"
    fi
}

# ============================================================================
# TESTS: SERVICES
# ============================================================================

test_service_create() {
    log_test "Create service"
    
    local json=$(yaml_to_json "$YAML_DIR/test-service.yaml")
    local http_code=$(curl -s -w "%{http_code}" -o /tmp/response.json \
        -X POST -H "Content-Type: application/json" \
        -d "$json" \
        "$API_SERVER/api/v1/namespaces/default/services")
    
    assert_http_success "Service created" "$http_code"
    
    response=$(cat /tmp/response.json)
    assert_contains "Response contains service name" "$response" "test-service"
}

test_service_get() {
    log_test "Get service by name"
    
    local http_code=$(curl -s -w "%{http_code}" -o /tmp/response.json \
        "$API_SERVER/api/v1/namespaces/default/services/test-service")
    
    assert_http_success "Service retrieved" "$http_code"
    
    response=$(cat /tmp/response.json)
    assert_contains "Response contains service name" "$response" "test-service"
}

test_service_list() {
    log_test "List services"
    
    # Small delay to ensure service is persisted
    sleep 0.5
    
    local http_code=$(curl -s -w "%{http_code}" -o /tmp/response.json \
        "$API_SERVER/api/v1/namespaces/default/services")
    
    assert_http_success "Services listed" "$http_code"
    
    response=$(cat /tmp/response.json)
    assert_contains "List contains test service" "$response" "test-service"
}

test_service_delete() {
    log_test "Delete service"
    
    local http_code=$(curl -s -w "%{http_code}" -o /tmp/response.json \
        -X DELETE \
        "$API_SERVER/api/v1/namespaces/default/services/test-service")
    
    if [[ $http_code == 204 ]]; then
        log_pass "Service deleted (HTTP 204)"
        TESTS_RUN=$((TESTS_RUN + 1))
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        assert_http_success "Service deleted" "$http_code"
    fi
}

# ============================================================================
# TESTS: CONFIGMAPS
# ============================================================================

test_configmap_create() {
    log_test "Create ConfigMap"
    
    local json=$(yaml_to_json "$YAML_DIR/test-configmap.yaml")
    local http_code=$(curl -s -w "%{http_code}" -o /tmp/response.json \
        -X POST -H "Content-Type: application/json" \
        -d "$json" \
        "$API_SERVER/api/v1/namespaces/default/configmaps")
    
    assert_http_success "ConfigMap created" "$http_code"
    
    response=$(cat /tmp/response.json)
    assert_contains "Response contains configmap name" "$response" "test-config"
}

test_configmap_get() {
    log_test "Get ConfigMap by name"
    
    local http_code=$(curl -s -w "%{http_code}" -o /tmp/response.json \
        "$API_SERVER/api/v1/namespaces/default/configmaps/test-config")
    
    assert_http_success "ConfigMap retrieved" "$http_code"
    
    response=$(cat /tmp/response.json)
    assert_contains "Response contains configmap name" "$response" "test-config"
}

test_configmap_list() {
    log_test "List ConfigMaps"
    
    local http_code=$(curl -s -w "%{http_code}" -o /tmp/response.json \
        "$API_SERVER/api/v1/namespaces/default/configmaps")
    
    assert_http_success "ConfigMaps listed" "$http_code"
    
    response=$(cat /tmp/response.json)
    assert_contains "List contains test configmap" "$response" "test-config"
}

test_configmap_delete() {
    log_test "Delete ConfigMap"
    
    local http_code=$(curl -s -w "%{http_code}" -o /tmp/response.json \
        -X DELETE \
        "$API_SERVER/api/v1/namespaces/default/configmaps/test-config")
    
    if [[ $http_code == 204 ]]; then
        log_pass "ConfigMap deleted (HTTP 204)"
        TESTS_RUN=$((TESTS_RUN + 1))
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        assert_http_success "ConfigMap deleted" "$http_code"
    fi
}

# ============================================================================
# TESTS: API DISCOVERY
# ============================================================================

test_api_versions() {
    log_test "API version discovery (/api)"
    
    local http_code=$(curl -s -w "%{http_code}" -o /tmp/response.json \
        "$API_SERVER/api")
    
    assert_http_success "API versions endpoint" "$http_code"
    
    response=$(cat /tmp/response.json)
    assert_contains "Response contains v1 version" "$response" "v1"
}

test_api_resources() {
    log_test "API resources discovery (/api/v1)"
    
    local http_code=$(curl -s -w "%{http_code}" -o /tmp/response.json \
        "$API_SERVER/api/v1")
    
    assert_http_success "API resources endpoint" "$http_code"
    
    response=$(cat /tmp/response.json)
    assert_contains "Response contains pods resource" "$response" "pods"
}

test_healthz() {
    log_test "Health check endpoint"
    
    local http_code=$(curl -s -w "%{http_code}" -o /tmp/response.json \
        "$API_SERVER/healthz")
    
    assert_http_success "Health check" "$http_code"
}

# ============================================================================
# MAIN EXECUTION
# ============================================================================

main() {
    echo ""
    echo "=========================================="
    echo "Sirah curl Integration Tests"
    echo "=========================================="
    echo ""
    
    check_prerequisites
    
    echo ""
    echo "=========================================="
    echo "API Discovery Tests"
    echo "=========================================="
    test_healthz
    test_api_versions
    test_api_resources
    
    echo ""
    echo "=========================================="
    echo "Pod CRUD Tests"
    echo "=========================================="
    test_pod_create
    test_pod_get
    test_pod_list
    test_pod_delete
    
    echo ""
    echo "=========================================="
    echo "Service CRUD Tests"
    echo "=========================================="
    test_service_create
    test_service_get
    test_service_list
    test_service_delete
    
    echo ""
    echo "=========================================="
    echo "ConfigMap CRUD Tests"
    echo "=========================================="
    test_configmap_create
    test_configmap_get
    test_configmap_list
    test_configmap_delete
    
    echo ""
    echo "=========================================="
    echo "Test Summary"
    echo "=========================================="
    echo "Total tests run:  $TESTS_RUN"
    echo -e "Tests passed:     ${GREEN}$TESTS_PASSED${NC}"
    echo -e "Tests failed:     ${RED}$TESTS_FAILED${NC}"
    echo ""
    
    if [ $TESTS_FAILED -eq 0 ]; then
        echo -e "${GREEN}✓ All tests passed!${NC}"
        return 0
    else
        echo -e "${RED}✗ Some tests failed${NC}"
        return 1
    fi
}

# Run main function
main
exit_code=$?

# Cleanup
rm -f /tmp/response.json

exit $exit_code
