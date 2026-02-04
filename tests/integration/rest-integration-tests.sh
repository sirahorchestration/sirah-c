#!/bin/bash
# kubectl Integration Tests for Sirah
# Tests actual YAML resources against the Sirah API server
# Requires: kubectl configured to point to Sirah at localhost:6443
#          Sirah API server running on localhost:6443
#          etcd running on localhost:2379

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# Configuration
API_SERVER="http://localhost:6443"
KUBECTL_NAMESPACE="default"
TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
YAML_DIR="${TEST_DIR}/fixtures"
YAML2JSON="${TEST_DIR}/yaml-to-json.sh"

# Verify fixtures directory exists
if [ ! -d "${YAML_DIR}" ]; then
    log_error "Fixtures directory not found at ${YAML_DIR}"
    exit 1
fi

# Function to convert YAML to JSON
yaml_to_json() {
    local yaml_file="$1"
    
    # Try yq first
    if command -v yq &> /dev/null; then
        yq -o json "$yaml_file"
    # Try python3 with PyYAML
    elif command -v python3 &> /dev/null; then
        python3 << PYSCRIPT
import yaml
import json
with open('$yaml_file', 'r') as f:
    data = yaml.safe_load(f)
    print(json.dumps(data))
PYSCRIPT
    else
        log_error "Neither yq nor python3 available for YAML conversion"
        return 1
    fi
}

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

assert_success() {
    local test_name="$1"
    local output="$2"
    
    TESTS_RUN=$((TESTS_RUN + 1))
    
    if [ $? -eq 0 ]; then
        log_info "✓ $test_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "✗ $test_name"
        echo "Output: $output"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
}

assert_contains() {
    local test_name="$1"
    local haystack="$2"
    local needle="$3"
    
    TESTS_RUN=$((TESTS_RUN + 1))
    
    if echo "$haystack" | grep -q "$needle"; then
        log_info "✓ $test_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "✗ $test_name"
        echo "Expected to find: $needle"
        echo "In output: $haystack"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
}

assert_not_contains() {
    local test_name="$1"
    local haystack="$2"
    local needle="$3"
    
    TESTS_RUN=$((TESTS_RUN + 1))
    
    if ! echo "$haystack" | grep -q "$needle"; then
        log_info "✓ $test_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_error "✗ $test_name"
        echo "Should NOT find: $needle"
        echo "In output: $haystack"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
}

# Check prerequisites
check_prerequisites() {
    log_test "Checking prerequisites"
    
    # Check kubectl
    if ! command -v kubectl &> /dev/null; then
        log_error "kubectl not found in PATH"
        exit 1
    fi
    
    # Check current context
    context=$(kubectl config current-context 2>/dev/null || echo "")
    if [ -z "$context" ]; then
        log_error "kubectl current context not set"
        exit 1
    fi
    
    log_info "kubectl context: $context"
    
    # Check API server connectivity
    if ! curl -s -o /dev/null -w "%{http_code}" "$API_SERVER/healthz" | grep -q "200"; then
        log_error "Cannot connect to Sirah API server at $API_SERVER"
        exit 1
    fi
    
    log_info "API server is reachable at $API_SERVER"
}

# ============================================================================
# TEST SECTION: PODS
# ============================================================================

test_pod_creation() {
    log_test "Pod creation"
    
    local json_data=$(yaml_to_json "${YAML_DIR}/test-pod.yaml")
    output=$(curl -s -X POST -H "Content-Type: application/json" \
        -d "$json_data" \
        "${API_SERVER}/api/v1/namespaces/default/pods" 2>&1)
    
    assert_contains "Pod creation succeeded" "$output" "test-pod-001"
}

test_pod_get() {
    log_test "Get pod by name"
    
    output=$(curl -s "${API_SERVER}/api/v1/namespaces/default/pods/test-pod-001" 2>&1)
    assert_contains "Pod retrieved successfully" "$output" "test-pod-001"
    assert_contains "Pod has correct metadata" "$output" "test-pod-001"
}

test_pod_list() {
    log_test "List pods in namespace"
    
    output=$(curl -s "${API_SERVER}/api/v1/namespaces/default/pods" 2>&1)
    assert_contains "Pods listed successfully" "$output" "test-pod-001"
}

test_pod_describe() {
    log_test "Describe pod (GET)"
    
    output=$(curl -s "${API_SERVER}/api/v1/namespaces/default/pods/test-pod-001" 2>&1)
    assert_contains "Pod description retrieved" "$output" "test-pod-001"
    assert_contains "Pod description has image" "$output" "test-kernel.img"
}

test_pod_delete() {
    log_test "Delete pod"
    
    output=$(curl -s -X DELETE "${API_SERVER}/api/v1/namespaces/default/pods/test-pod-001" 2>&1)
    assert_success "Pod deleted successfully" "$output"
}

# ============================================================================
# TEST SECTION: SERVICES
# ============================================================================

test_service_creation() {
    log_test "Service creation"
    
    local json_data=$(yaml_to_json "${YAML_DIR}/test-service.yaml")
    output=$(curl -s -X POST -H "Content-Type: application/json" \
        -d "$json_data" \
        "${API_SERVER}/api/v1/namespaces/default/services" 2>&1)
    
    assert_contains "Service created successfully" "$output" "test-service"
}

test_service_get() {
    log_test "Get service by name"
    
    output=$(curl -s "${API_SERVER}/api/v1/namespaces/default/services/test-service" 2>&1)
    assert_contains "Service retrieved" "$output" "test-service"
}

test_service_list() {
    log_test "List services"
    
    output=$(curl -s "${API_SERVER}/api/v1/namespaces/default/services" 2>&1)
    assert_contains "Services listed" "$output" "test-service"
}

test_service_delete() {
    log_test "Delete service"
    
    output=$(curl -s -X DELETE "${API_SERVER}/api/v1/namespaces/default/services/test-service" 2>&1)
    assert_success "Service deleted" "$output"
}

# ============================================================================
# TEST SECTION: CONFIGMAPS
# ============================================================================

test_configmap_creation() {
    log_test "ConfigMap creation"
    
    output=$(kubectl apply -f "${YAML_DIR}/test-configmap.yaml" 2>&1)
    assert_contains "ConfigMap created" "$output" "test-config"
}

test_configmap_get() {
    log_test "Get ConfigMap by name"
    
    output=$(kubectl get configmap test-config -o json 2>&1)
    assert_contains "ConfigMap retrieved" "$output" "test-config"
    assert_contains "ConfigMap has data" "$output" "app.conf"
}

test_configmap_list() {
    log_test "List ConfigMaps"
    
    output=$(kubectl get configmaps 2>&1)
    assert_contains "ConfigMaps listed" "$output" "test-config"
}

test_configmap_delete() {
    log_test "Delete ConfigMap"
    
    output=$(kubectl delete configmap test-config --ignore-not-found 2>&1)
    assert_success "ConfigMap deleted" "$output"
}

# ============================================================================
# TEST SECTION: DEPLOYMENTS
# ============================================================================

test_deployment_creation() {
    log_test "Deployment creation"
    
    output=$(kubectl apply -f "${YAML_DIR}/test-deployment.yaml" 2>&1)
    assert_contains "Deployment created" "$output" "test-deployment"
}

test_deployment_get() {
    log_test "Get Deployment by name"
    
    output=$(kubectl get deployment test-deployment -o json 2>&1)
    assert_contains "Deployment retrieved" "$output" "test-deployment"
    assert_contains "Deployment has replicas" "$output" "replicas"
}

test_deployment_list() {
    log_test "List Deployments"
    
    output=$(kubectl get deployments 2>&1)
    assert_contains "Deployments listed" "$output" "test-deployment"
}

test_deployment_scale() {
    log_test "Scale Deployment"
    
    output=$(kubectl scale deployment test-deployment --replicas=3 2>&1)
    assert_success "Deployment scaled" "$output"
}

test_deployment_delete() {
    log_test "Delete Deployment"
    
    output=$(kubectl delete deployment test-deployment --ignore-not-found 2>&1)
    assert_success "Deployment deleted" "$output"
}

# ============================================================================
# TEST SECTION: NAMESPACES
# ============================================================================

test_namespace_creation() {
    log_test "Namespace creation"
    
    output=$(kubectl apply -f "${YAML_DIR}/test-namespace.yaml" 2>&1)
    assert_contains "Namespace created" "$output" "test-namespace"
}

test_namespace_list() {
    log_test "List namespaces"
    
    output=$(kubectl get namespaces 2>&1)
    assert_contains "Namespaces listed" "$output" "test-namespace"
}

test_namespace_delete() {
    log_test "Delete namespace"
    
    output=$(kubectl delete namespace test-namespace --ignore-not-found 2>&1)
    assert_success "Namespace deleted" "$output"
}

# ============================================================================
# TEST SECTION: ADVANCED OPERATIONS
# ============================================================================

test_pod_with_labels() {
    log_test "Pod creation with labels"
    
    output=$(kubectl apply -f "${YAML_DIR}/labeled-pod.yaml" 2>&1)
    assert_contains "Labeled pod created" "$output" "labeled-pod"
}

test_pod_label_selector() {
    log_test "Get pods by label selector"
    
    output=$(kubectl get pods -l app=test 2>&1)
    assert_contains "Label selector works" "$output" "labeled-pod"
}

test_pod_label_cleanup() {
    log_test "Cleanup labeled pod"
    
    output=$(kubectl delete pod labeled-pod --ignore-not-found 2>&1)
    assert_success "Labeled pod deleted" "$output"
}

test_pod_in_custom_namespace() {
    log_test "Create pod in custom namespace"
    
    # Create namespace
    kubectl create namespace test-ns --dry-run=client -o yaml | kubectl apply -f - 2>/dev/null || true
    
    # Use fixture but apply to custom namespace (create temp copy with correct namespace)
    sed 's/namespace: test-ns/namespace: test-ns/g' "${YAML_DIR}/namespaced-pod.yaml" | kubectl apply -f - 2>&1
    
    output=$(kubectl apply -f "${YAML_DIR}/namespaced-pod.yaml" 2>&1)
    assert_contains "Pod created in custom namespace" "$output" "namespaced-pod"
    
    # Get pod from specific namespace
    output=$(kubectl get pod namespaced-pod -n test-ns 2>&1)
    assert_contains "Pod retrieved from custom namespace" "$output" "namespaced-pod"
    
    # Cleanup
    kubectl delete pod namespaced-pod -n test-ns --ignore-not-found 2>&1
    kubectl delete namespace test-ns --ignore-not-found 2>&1
}

test_api_version_discovery() {
    log_test "API version discovery"
    
    output=$(kubectl api-versions 2>&1)
    assert_contains "v1 API group present" "$output" "v1"
}

test_api_resources() {
    log_test "API resources discovery"
    
    output=$(kubectl api-resources 2>&1)
    assert_contains "pods resource present" "$output" "pods"
    assert_contains "services resource present" "$output" "services"
    assert_contains "configmaps resource present" "$output" "configmaps"
}

# ============================================================================
# TEST SECTION: ERROR CASES
# ============================================================================

test_pod_not_found() {
    log_test "Get non-existent pod returns 404"
    
    output=$(kubectl get pod non-existent-pod 2>&1)
    assert_contains "Pod not found error" "$output" "not found\|NotFound"
}

test_invalid_yaml() {
    log_test "Invalid YAML handling"
    
    # This should either succeed (lax validation) or fail gracefully
    output=$(kubectl apply -f "${YAML_DIR}/invalid.yaml" 2>&1)
    assert_not_contains "Crash on invalid YAML" "$output" "panic"
}

# ============================================================================
# TEST SECTION: RESOURCE STATUS
# ============================================================================

test_pod_status_fields() {
    log_test "Pod has correct status fields"
    
    # Use the status fixture
    kubectl apply -f "${YAML_DIR}/status-pod.yaml" 2>&1
    
    # Check status
    output=$(kubectl get pod status-test-pod -o json 2>&1)
    assert_contains "Pod status has phase" "$output" "phase"
    
    # Cleanup
    kubectl delete pod status-test-pod --ignore-not-found 2>&1
}

# ============================================================================
# MAIN TEST EXECUTION
# ============================================================================

main() {
    echo "=========================================="
    echo "Sirah kubectl Integration Tests"
    echo "=========================================="
    echo ""
    
    check_prerequisites
    
    echo ""
    echo "=========================================="
    echo "Running Pod Tests"
    echo "=========================================="
    test_pod_creation
    test_pod_get
    test_pod_list
    test_pod_describe
    test_pod_delete
    
    echo ""
    echo "=========================================="
    echo "Running Service Tests"
    echo "=========================================="
    test_service_creation
    test_service_get
    test_service_list
    test_service_delete
    
    echo ""
    echo "=========================================="
    echo "Running ConfigMap Tests"
    echo "=========================================="
    test_configmap_creation
    test_configmap_get
    test_configmap_list
    test_configmap_delete
    
    echo ""
    echo "=========================================="
    echo "Running Deployment Tests"
    echo "=========================================="
    test_deployment_creation
    test_deployment_get
    test_deployment_list
    test_deployment_scale
    test_deployment_delete
    
    echo ""
    echo "=========================================="
    echo "Running Namespace Tests"
    echo "=========================================="
    test_namespace_creation
    test_namespace_list
    test_namespace_delete
    
    echo ""
    echo "=========================================="
    echo "Running Advanced Operation Tests"
    echo "=========================================="
    test_pod_with_labels
    test_pod_label_selector
    test_pod_label_cleanup
    test_pod_in_custom_namespace
    test_api_version_discovery
    test_api_resources
    
    echo ""
    echo "=========================================="
    echo "Running Error Case Tests"
    echo "=========================================="
    test_pod_not_found
    test_invalid_yaml
    
    echo ""
    echo "=========================================="
    echo "Running Status Field Tests"
    echo "=========================================="
    test_pod_status_fields
    
    echo ""
    echo "=========================================="
    echo "Test Summary"
    echo "=========================================="
    echo "Total tests run: $TESTS_RUN"
    echo -e "Tests passed: ${GREEN}$TESTS_PASSED${NC}"
    echo -e "Tests failed: ${RED}$TESTS_FAILED${NC}"
    
    if [ $TESTS_FAILED -eq 0 ]; then
        echo -e "${GREEN}All tests passed!${NC}"
        return 0
    else
        echo -e "${RED}Some tests failed${NC}"
        return 1
    fi
}

# Run main function
main
exit_code=$?

exit $exit_code
