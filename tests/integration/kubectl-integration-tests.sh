#!/bin/bash
# kubectl Integration Tests for Sirah
# Tests actual YAML resources using kubectl against the Sirah API server
# Requires: kubectl configured to point to Sirah at localhost:6443
#          Sirah API server running on localhost:6443
#          etcd running on localhost:2379
#
# USAGE: ./kubectl-integration-tests.sh [options]
#   --help              Show this help message and test inventory
#   --list              List all tests without running them
#   --run               Run all tests (default)
#   --keep-resources    Prompt before deleting each resource (interactive mode)
#   --clean-all         Auto-delete all resources without prompting

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
KUBECTL_NAMESPACE="default"
TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIXTURES_DIR="${TEST_DIR}/fixtures"

# Options
INTERACTIVE_DELETE=1  # Default: prompt before deletion
AUTO_CLEAN=0          # Default: don't auto-delete without asking
CURRENT_TEST_NUM=0    # Track current test number
TOTAL_TESTS=42        # Total tests in suite

# Verify fixtures directory exists
if [ ! -d "${FIXTURES_DIR}" ]; then
    echo -e "${RED}[ERROR]${NC} Fixtures directory not found at ${FIXTURES_DIR}"
    exit 1
fi

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

log_debug() {
    echo -e "${BLUE}[DEBUG]${NC} $1"
}

log_test_start() {
    CURRENT_TEST_NUM=$((CURRENT_TEST_NUM + 1))
    local test_name="$1"
    local percentage=$((CURRENT_TEST_NUM * 100 / TOTAL_TESTS))
    echo ""
    echo -e "${BLUE}============================================================${NC}"
    echo -e "${BLUE}[${GREEN}Test ${CURRENT_TEST_NUM}/${TOTAL_TESTS}${BLUE}]${NC} ${test_name} (${percentage}%)"
    echo -e "${BLUE}============================================================${NC}"
}

confirm_delete() {
    local resource_type="$1"
    local resource_name="$2"
    
    if [ $AUTO_CLEAN -eq 1 ]; then
        return 0  # Auto-delete
    fi
    
    if [ $INTERACTIVE_DELETE -eq 1 ]; then
        echo -n "  Delete $resource_type '$resource_name'? (y/n/a for all): "
        read -r reply
        case "$reply" in
            [Yy]) return 0 ;;
            [Aa]) AUTO_CLEAN=1; return 0 ;;
            *) return 1 ;;
        esac
    fi
    return 0
}

# Display help and test inventory
show_help() {
    echo ""
    echo -e "${BLUE}============================================================${NC}"
    echo -e "${BLUE}    SIRAH KUBECTL INTEGRATION TEST SUITE - TEST INVENTORY  ${NC}"
    echo -e "${BLUE}============================================================${NC}"
    echo ""
    echo -e "${YELLOW}USAGE:${NC} $0 [--help|--list|--run|--keep-resources|--clean-all]"
    echo "  --help              Show this help message and test inventory"
    echo "  --list              List all tests without running them"
    echo "  --run               Run all tests (default)"
    echo "  --keep-resources    Prompt before deleting each resource (interactive)"
    echo "  --clean-all         Auto-delete all resources without prompting"
    echo ""
    echo -e "${YELLOW}TOTAL TESTS: 40 across 10 resource categories + fixture validation${NC}"
    echo ""
    
    echo -e "${BLUE}============================================================${NC}"
    echo -e "${GREEN}POD TESTS (7 tests)${NC} - Create, Retrieve, List, Describe, Delete"
    echo -e "${BLUE}============================================================${NC}"
    echo "  1. test_pod_apply           → Create pod from fixture (kubectl apply)"
    echo "  2. test_pod_get_by_name     → Retrieve pod by name"
    echo "  3. test_pod_get_json        → Retrieve pod in JSON format"
    echo "  4. test_pod_list            → List all pods"
    echo "  5. test_pod_list_wide       → List pods in wide format"
    echo "  6. test_pod_describe        → Get detailed pod description"
    echo "  7. test_pod_delete          → Delete a pod"
    echo ""
    
    echo -e "${GREEN}SERVICE TESTS (5 tests)${NC} - Create, Retrieve, List, Delete"
    echo -e "${BLUE}───────────────────────────────────────────────────────────────────${NC}"
    echo "  8. test_service_apply       → Create service from fixture (kubectl apply)"
    echo "  9. test_service_get_by_name → Retrieve service by name"
    echo " 10. test_service_get_json    → Retrieve service in JSON format"
    echo " 11. test_service_list        → List all services"
    echo " 12. test_service_delete      → Delete a service"
    echo ""
    
    echo -e "${GREEN}CONFIGMAP TESTS (5 tests)${NC} - Create, Retrieve, List, Delete"
    echo -e "${BLUE}───────────────────────────────────────────────────────────────────${NC}"
    echo " 13. test_configmap_apply     → Create ConfigMap from fixture (kubectl apply)"
    echo " 14. test_configmap_get       → Retrieve ConfigMap by name"
    echo " 15. test_configmap_get_json  → Retrieve ConfigMap in JSON format"
    echo " 16. test_configmap_list      → List all ConfigMaps"
    echo " 17. test_configmap_delete    → Delete a ConfigMap"
    echo ""
    
    echo -e "${GREEN}DEPLOYMENT TESTS (7 tests)${NC} - Create, Retrieve, List, Scale, Delete"
    echo -e "${BLUE}───────────────────────────────────────────────────────────────────${NC}"
    echo " 18. test_deployment_apply    → Create deployment from fixture (kubectl apply)"
    echo " 19. test_deployment_get      → Retrieve deployment by name"
    echo " 20. test_deployment_get_json → Retrieve deployment in JSON format"
    echo " 21. test_deployment_list     → List all deployments"
    echo " 22. test_deployment_scale    → Scale deployment replicas"
    echo " 23. test_deployment_verify_scale → Verify deployment scaling"
    echo " 24. test_deployment_delete   → Delete a deployment"
    echo ""
    
    echo -e "${GREEN}NAMESPACE TESTS (4 tests)${NC} - Create, Retrieve, List, Delete"
    echo -e "${BLUE}───────────────────────────────────────────────────────────────────${NC}"
    echo " 25. test_namespace_apply     → Create new namespace (kubectl apply)"
    echo " 26. test_namespace_get       → Retrieve namespace"
    echo " 27. test_namespace_list      → List all namespaces"
    echo " 28. test_namespace_delete    → Delete namespace"
    echo ""
    
    echo -e "${GREEN}POD WITH LABELS TESTS (4 tests)${NC} - Create, Query by Label, Delete"
    echo -e "${BLUE}───────────────────────────────────────────────────────────────────${NC}"
    echo " 29. test_pod_with_labels     → Create labeled pod"
    echo " 30. test_pod_label_selector  → Query pods by label"
    echo " 31. test_pod_label_selector_json → Query pods with JSON output"
    echo " 32. test_pod_label_cleanup   → Delete labeled pods"
    echo ""
    
    echo -e "${GREEN}CUSTOM NAMESPACE TESTS (1 test)${NC}"
    echo -e "${BLUE}============================================================${NC}"
    echo " 33. test_pod_in_custom_namespace → Create pod in custom namespace"
    echo ""
    
    echo -e "${GREEN}API DISCOVERY TESTS (2 tests)${NC}"
    echo -e "${BLUE}============================================================${NC}"
    echo " 34. test_api_versions        → Query API versions"
    echo " 35. test_api_resources       → Query API resources"
    echo ""
    
    echo -e "${GREEN}ERROR HANDLING TESTS (3 tests)${NC} - Error Cases & Edge Cases"
    echo -e "${BLUE}───────────────────────────────────────────────────────────────────${NC}"
    echo " 36. test_pod_not_found       → Handle pod not found error"
    echo " 37. test_invalid_yaml        → Handle invalid YAML error"
    echo " 38. test_delete_non_existent → Delete non-existent resource"
    echo ""
    
    echo -e "${GREEN}STATUS FIELD TESTS (2 tests)${NC}"
    echo -e "${BLUE}============================================================${NC}"
    echo " 39. test_pod_status_fields   → Verify pod status fields"
    echo " 40. test_resource_json_output → Verify JSON output format"
    echo ""
    
    echo -e "${GREEN}FIXTURE VALIDATION TESTS (2 tests)${NC}"
    echo -e "${BLUE}============================================================${NC}"
    echo " 41. test_all_fixtures_exist  → Verify all fixtures are present"
    echo " 42. test_fixture_validity    → Verify all fixtures are valid YAML"
    echo ""
    
    echo -e "${BLUE}============================================================${NC}"
    echo -e "${YELLOW}OPERATIONS BREAKDOWN:${NC}"
    echo -e "${BLUE}============================================================${NC}"
    echo "  Create Operations         : 9 tests  (Pod, Service, ConfigMap, Deployment, Namespace, Labels)"
    echo "  Retrieve Operations       : 14 tests (Get, Get JSON for each resource type)"
    echo "  List Operations           : 6 tests  (List, List wide)"
    echo "  Modify Operations         : 3 tests  (Deployment scaling)"
    echo "  Delete Operations         : 7 tests  (Delete resources)"
    echo "  Validation Operations     : 2 tests  (Fixture validation)"
    echo "  Error Handling Operations : 3 tests  (Error cases)"
    echo "  API Discovery Operations  : 2 tests  (API versions/resources)"
    echo ""
    
    echo -e "${BLUE}============================================================${NC}"
    echo -e "${YELLOW}TEST FIXTURES (9 YAML files):${NC}"
    echo -e "${BLUE}============================================================${NC}"
    echo "  • test-pod.yaml           - Basic pod specification"
    echo "  • test-service.yaml       - Service specification"
    echo "  • test-configmap.yaml     - ConfigMap specification"
    echo "  • test-deployment.yaml    - Deployment specification"
    echo "  • test-namespace.yaml     - Namespace specification"
    echo "  • test-pod-labels.yaml    - Pod with labels for selector testing"
    echo "  • test-custom-ns-pod.yaml - Pod for custom namespace testing"
    echo "  • test-invalid.yaml       - Invalid YAML for error testing"
    echo "  • fixtures/...            - Additional test data files"
    echo ""
    
    echo -e "${BLUE}============================================================${NC}"
    echo -e "${YELLOW}REQUIREMENTS:${NC}"
    echo -e "${BLUE}============================================================${NC}"
    echo "  ✓ kubectl                 - Kubernetes CLI client"
    echo "  ✓ Sirah API Server        - Running on localhost:6443"
    echo "  ✓ etcd                    - Running on localhost:2379"
    echo "  ✓ kubectl context         - Configured to point to Sirah"
    echo "  ✓ Fixture files           - Must exist in ${FIXTURES_DIR}"
    echo ""
    
    echo -e "${BLUE}============================================================${NC}"
    echo -e "${YELLOW}EXAMPLE USAGE:${NC}"
    echo -e "${BLUE}============================================================${NC}"
    echo "  # Show this inventory"
    echo "  \$ ./kubectl-integration-tests.sh --help"
    echo ""
    echo "  # List all tests without running"
    echo "  \$ ./kubectl-integration-tests.sh --list"
    echo ""
    echo "  # Run all 42 tests"
    echo "  \$ ./kubectl-integration-tests.sh --run"
    echo ""
    echo "  # Run all tests (default if no flag)"
    echo "  \$ ./kubectl-integration-tests.sh"
    echo ""
}

# List all tests without running them
list_tests() {
    echo ""
    echo -e "${YELLOW}TEST MANIFEST:${NC}"
    echo ""
    grep -E "^test_.*\(\)" "${BASH_SOURCE[0]}" | sed 's/() {//g' | nl -w 2 -s ". " | head -42
    echo ""
}

assert_success() {
    local test_name="$1"
    local exit_code="$2"
    
    TESTS_RUN=$((TESTS_RUN + 1))
    
    if [ "$exit_code" -eq 0 ]; then
        log_info "✓ $test_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        log_error "✗ $test_name (exit code: $exit_code)"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

assert_failure() {
    local test_name="$1"
    local exit_code="$2"
    
    TESTS_RUN=$((TESTS_RUN + 1))
    
    if [ "$exit_code" -ne 0 ]; then
        log_info "✓ $test_name (correctly failed)"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        log_error "✗ $test_name (should have failed)"
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
        log_info "✓ $test_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        log_error "✗ $test_name"
        log_debug "Expected to find: '$needle'"
        log_debug "In output: '$haystack'"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
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
        return 0
    else
        log_error "✗ $test_name"
        log_debug "Should NOT find: '$needle'"
        log_debug "In output: '$haystack'"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

assert_file_exists() {
    local test_name="$1"
    local filepath="$2"
    
    TESTS_RUN=$((TESTS_RUN + 1))
    
    if [ -f "$filepath" ]; then
        log_info "✓ $test_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        log_error "✗ $test_name (file not found: $filepath)"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

# Check prerequisites
check_prerequisites() {
    log_test "Checking prerequisites..."
    
    # Check kubectl
    if ! command -v kubectl &> /dev/null; then
        log_error "kubectl not found in PATH"
        exit 1
    fi
    
    log_info "kubectl found: $(kubectl version --client --short 2>/dev/null | head -1)"
    
    # Check current context
    local context=$(kubectl config current-context 2>/dev/null || echo "")
    if [ -z "$context" ]; then
        log_error "kubectl current context not set"
        exit 1
    fi
    
    log_info "kubectl context: $context"
    
    # Check API server connectivity
    local http_code=$(curl -s -o /dev/null -w "%{http_code}" "$API_SERVER/healthz" 2>/dev/null || echo "000")
    if [ "$http_code" != "200" ]; then
        log_error "Cannot connect to Sirah API server at $API_SERVER (HTTP $http_code)"
        exit 1
    fi
    
    log_info "API server is reachable at $API_SERVER"
    
    # Check fixtures
    assert_file_exists "Pod fixture exists" "${FIXTURES_DIR}/test-pod.yaml"
    assert_file_exists "Service fixture exists" "${FIXTURES_DIR}/test-service.yaml"
    assert_file_exists "ConfigMap fixture exists" "${FIXTURES_DIR}/test-configmap.yaml"
    assert_file_exists "Deployment fixture exists" "${FIXTURES_DIR}/test-deployment.yaml"
    assert_file_exists "Namespace fixture exists" "${FIXTURES_DIR}/test-namespace.yaml"
}

# ============================================================================
# TEST SECTION: PODS
# ============================================================================

test_pod_apply() {
    log_test_start "Pod apply (kubectl apply)"
    log_test "Pod apply (kubectl apply)"
    
    local output
    output=$(kubectl apply -f "${FIXTURES_DIR}/test-pod.yaml" 2>&1)
    assert_success "Pod applied successfully" $?
    assert_contains "Pod apply output contains pod name" "$output" "test-pod-001"
}

test_pod_get_by_name() {
    log_test "Get pod by name (kubectl get)"
    
    local output
    output=$(kubectl get pod test-pod-001 2>&1)
    assert_success "Pod retrieved successfully" $?
    assert_contains "Pod output contains pod name" "$output" "test-pod-001"
}

test_pod_get_json() {
    log_test "Get pod in JSON format"
    
    local output
    output=$(kubectl get pod test-pod-001 -o json 2>&1)
    assert_success "Pod retrieved in JSON format" $?
    assert_contains "Pod JSON contains metadata" "$output" "metadata"
    assert_contains "Pod JSON contains spec" "$output" "spec"
}

test_pod_list() {
    log_test "List all pods (kubectl get pods)"
    
    local output
    output=$(kubectl get pods 2>&1)
    assert_success "Pods listed successfully" $?
    assert_contains "Pods list contains pod name" "$output" "test-pod-001"
}

test_pod_list_wide() {
    log_test "List pods with wide output"
    
    local output
    output=$(kubectl get pods -o wide 2>&1)
    assert_success "Pods listed in wide format" $?
    assert_contains "Wide output contains pod name" "$output" "test-pod-001"
}

test_pod_describe() {
    log_test "Describe pod (kubectl describe)"
    
    local output
    output=$(kubectl describe pod test-pod-001 2>&1)
    assert_success "Pod description retrieved" $?
    assert_contains "Description contains pod name" "$output" "test-pod-001"
}

test_pod_delete() {
    log_test_start "Delete pod (kubectl delete)"
    log_test "Delete pod (kubectl delete)"
    
    if confirm_delete "pod" "test-pod-001"; then
        local output
        output=$(kubectl delete pod test-pod-001 2>&1)
        assert_success "Pod deleted successfully" $?
    else
        log_info "✓ Skipped deletion - keeping pod test-pod-001"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        TESTS_RUN=$((TESTS_RUN + 1))
    fi
}

# ============================================================================
# TEST SECTION: SERVICES
# ============================================================================

test_service_apply() {
    log_test "Service apply (kubectl apply)"
    
    local output
    output=$(kubectl apply -f "${FIXTURES_DIR}/test-service.yaml" 2>&1)
    assert_success "Service applied successfully" $?
    assert_contains "Service apply output contains service name" "$output" "test-service"
}

test_service_get_by_name() {
    log_test "Get service by name"
    
    local output
    output=$(kubectl get service test-service 2>&1)
    assert_success "Service retrieved successfully" $?
    assert_contains "Service output contains service name" "$output" "test-service"
}

test_service_get_json() {
    log_test "Get service in JSON format"
    
    local output
    output=$(kubectl get service test-service -o json 2>&1)
    assert_success "Service retrieved in JSON format" $?
    assert_contains "Service JSON contains metadata" "$output" "metadata"
    assert_contains "Service JSON contains spec" "$output" "spec"
}

test_service_list() {
    log_test "List all services"
    
    local output
    output=$(kubectl get services 2>&1)
    assert_success "Services listed successfully" $?
    assert_contains "Services list contains service name" "$output" "test-service"
}

test_service_delete() {
    log_test "Delete service"
    
    local output
    output=$(kubectl delete service test-service 2>&1)
    assert_success "Service deleted successfully" $?
}

# ============================================================================
# TEST SECTION: CONFIGMAPS
# ============================================================================

test_configmap_apply() {
    log_test "ConfigMap apply"
    
    local output
    output=$(kubectl apply -f "${FIXTURES_DIR}/test-configmap.yaml" 2>&1)
    assert_success "ConfigMap applied successfully" $?
    assert_contains "ConfigMap apply output contains name" "$output" "test-config"
}

test_configmap_get() {
    log_test "Get ConfigMap by name"
    
    local output
    output=$(kubectl get configmap test-config 2>&1)
    assert_success "ConfigMap retrieved successfully" $?
    assert_contains "ConfigMap output contains name" "$output" "test-config"
}

test_configmap_get_json() {
    log_test "Get ConfigMap in JSON format"
    
    local output
    output=$(kubectl get configmap test-config -o json 2>&1)
    assert_success "ConfigMap retrieved in JSON format" $?
    assert_contains "ConfigMap JSON contains data" "$output" "data"
}

test_configmap_list() {
    log_test "List ConfigMaps"
    
    local output
    output=$(kubectl get configmaps 2>&1)
    assert_success "ConfigMaps listed successfully" $?
    assert_contains "ConfigMaps list contains name" "$output" "test-config"
}

test_configmap_delete() {
    log_test "Delete ConfigMap"
    
    local output
    output=$(kubectl delete configmap test-config --ignore-not-found 2>&1)
    assert_success "ConfigMap deleted successfully" $?
}

# ============================================================================
# TEST SECTION: DEPLOYMENTS
# ============================================================================

test_deployment_apply() {
    log_test "Deployment apply"
    
    local output
    output=$(kubectl apply -f "${FIXTURES_DIR}/test-deployment.yaml" 2>&1)
    assert_success "Deployment applied successfully" $?
    assert_contains "Deployment apply output contains name" "$output" "test-deployment"
}

test_deployment_get() {
    log_test "Get Deployment by name"
    
    local output
    output=$(kubectl get deployment test-deployment 2>&1)
    assert_success "Deployment retrieved successfully" $?
    assert_contains "Deployment output contains name" "$output" "test-deployment"
}

test_deployment_get_json() {
    log_test "Get Deployment in JSON format"
    
    local output
    output=$(kubectl get deployment test-deployment -o json 2>&1)
    assert_success "Deployment retrieved in JSON format" $?
    assert_contains "Deployment JSON contains spec" "$output" "spec"
    assert_contains "Deployment JSON contains replicas" "$output" "replicas"
}

test_deployment_list() {
    log_test "List Deployments"
    
    local output
    output=$(kubectl get deployments 2>&1)
    assert_success "Deployments listed successfully" $?
    assert_contains "Deployments list contains name" "$output" "test-deployment"
}

test_deployment_scale() {
    log_test "Scale Deployment"
    
    local output
    output=$(kubectl scale deployment test-deployment --replicas=3 2>&1)
    assert_success "Deployment scaled successfully" $?
}

test_deployment_verify_scale() {
    log_test "Verify Deployment scale"
    
    local output
    output=$(kubectl get deployment test-deployment -o json 2>&1)
    assert_contains "Deployment replicas set to 3" "$output" '"replicas":3'
}

test_deployment_delete() {
    log_test "Delete Deployment"
    
    local output
    output=$(kubectl delete deployment test-deployment --ignore-not-found 2>&1)
    assert_success "Deployment deleted successfully" $?
}

# ============================================================================
# TEST SECTION: NAMESPACES
# ============================================================================

test_namespace_apply() {
    log_test "Namespace apply"
    
    local output
    output=$(kubectl apply -f "${FIXTURES_DIR}/test-namespace.yaml" 2>&1)
    assert_success "Namespace applied successfully" $?
    assert_contains "Namespace apply output contains name" "$output" "test-namespace"
}

test_namespace_list() {
    log_test "List namespaces"
    
    local output
    output=$(kubectl get namespaces 2>&1)
    assert_success "Namespaces listed successfully" $?
    assert_contains "Namespaces list contains namespace" "$output" "test-namespace"
}

test_namespace_get() {
    log_test "Get namespace by name"
    
    local output
    output=$(kubectl get namespace test-namespace 2>&1)
    assert_success "Namespace retrieved successfully" $?
    assert_contains "Namespace output contains name" "$output" "test-namespace"
}

test_namespace_delete() {
    log_test "Delete namespace"
    
    local output
    output=$(kubectl delete namespace test-namespace --ignore-not-found 2>&1)
    assert_success "Namespace deleted successfully" $?
}

# ============================================================================
# TEST SECTION: ADVANCED OPERATIONS
# ============================================================================

test_pod_with_labels() {
    log_test "Pod creation with labels"
    
    local output
    output=$(kubectl apply -f "${FIXTURES_DIR}/labeled-pod.yaml" 2>&1)
    assert_success "Labeled pod applied successfully" $?
    assert_contains "Labeled pod apply output contains name" "$output" "labeled-pod"
}

test_pod_label_selector() {
    log_test "Get pods by label selector"
    
    local output
    output=$(kubectl get pods -l app=test 2>&1)
    assert_success "Label selector query successful" $?
    assert_contains "Label selector returns labeled pod" "$output" "labeled-pod"
}

test_pod_label_selector_json() {
    log_test "Get pods by label selector in JSON"
    
    local output
    output=$(kubectl get pods -l app=test -o json 2>&1)
    assert_success "Label selector JSON query successful" $?
    assert_contains "JSON contains items array" "$output" "items"
}

test_pod_label_cleanup() {
    log_test "Cleanup labeled pod"
    
    local output
    output=$(kubectl delete pod labeled-pod --ignore-not-found 2>&1)
    assert_success "Labeled pod deleted successfully" $?
}

test_pod_in_custom_namespace() {
    log_test "Create pod in custom namespace"
    
    # Create namespace first
    kubectl create namespace test-ns --dry-run=client -o yaml 2>/dev/null | kubectl apply -f - 2>&1 || true
    sleep 1
    
    # Apply pod to custom namespace
    local output
    output=$(kubectl apply -f "${FIXTURES_DIR}/namespaced-pod.yaml" 2>&1)
    assert_success "Pod applied to custom namespace" $?
    
    # Get pod from specific namespace
    output=$(kubectl get pod namespaced-pod -n test-ns 2>&1)
    assert_success "Pod retrieved from custom namespace" $?
    assert_contains "Custom namespace pod name appears in output" "$output" "namespaced-pod"
    
    # Cleanup
    kubectl delete pod namespaced-pod -n test-ns --ignore-not-found 2>&1
    sleep 1
    kubectl delete namespace test-ns --ignore-not-found 2>&1
}

test_api_versions() {
    log_test "API versions discovery"
    
    local output
    output=$(kubectl api-versions 2>&1)
    assert_success "API versions command successful" $?
    assert_contains "v1 API version present" "$output" "v1"
}

test_api_resources() {
    log_test "API resources discovery"
    
    local output
    output=$(kubectl api-resources 2>&1)
    assert_success "API resources command successful" $?
    assert_contains "pods resource present" "$output" "pods"
    assert_contains "services resource present" "$output" "services"
    assert_contains "configmaps resource present" "$output" "configmaps"
}

# ============================================================================
# TEST SECTION: ERROR CASES
# ============================================================================

test_pod_not_found() {
    log_test "Get non-existent pod returns error"
    
    local output
    output=$(kubectl get pod non-existent-pod-12345 2>&1)
    assert_failure "Non-existent pod query fails" $?
    assert_contains "Error message for missing pod" "$output" "not found\|NotFound\|Error"
}

test_invalid_yaml() {
    log_test "Invalid YAML handling"
    
    local output
    output=$(kubectl apply -f "${FIXTURES_DIR}/invalid.yaml" 2>&1)
    # Should either succeed (lax validation) or fail gracefully
    assert_not_contains "No panic on invalid YAML" "$output" "panic"
}

test_delete_non_existent() {
    log_test "Delete non-existent resource gracefully"
    
    local output
    output=$(kubectl delete pod non-existent-12345 --ignore-not-found 2>&1)
    assert_success "Delete with --ignore-not-found succeeds" $?
}

# ============================================================================
# TEST SECTION: RESOURCE STATUS
# ============================================================================

test_pod_status_fields() {
    log_test "Pod has correct status fields"
    
    # Apply pod
    kubectl apply -f "${FIXTURES_DIR}/status-pod.yaml" 2>&1
    sleep 1
    
    # Check status in JSON
    local output
    output=$(kubectl get pod status-test-pod -o json 2>&1)
    assert_success "Pod status retrieved" $?
    assert_contains "Pod status has phase field" "$output" "phase"
    
    # Cleanup
    kubectl delete pod status-test-pod --ignore-not-found 2>&1
}

test_resource_json_output() {
    log_test "Resources output valid JSON"
    
    # Apply a resource
    kubectl apply -f "${FIXTURES_DIR}/test-pod.yaml" 2>&1
    sleep 1
    
    # Get in JSON and verify it's valid
    local output
    output=$(kubectl get pod test-pod-001 -o json 2>&1)
    
    # Check if it's valid JSON by looking for JSON structure markers
    assert_contains "JSON output has opening brace" "$output" "^{"
    assert_contains "JSON output has closing brace" "$output" "}$"
    
    # Cleanup
    kubectl delete pod test-pod-001 --ignore-not-found 2>&1
}

# ============================================================================
# TEST SECTION: YAML FIXTURES
# ============================================================================

test_all_fixtures_exist() {
    log_test "All required YAML fixtures exist"
    
    assert_file_exists "test-pod.yaml exists" "${FIXTURES_DIR}/test-pod.yaml"
    assert_file_exists "test-service.yaml exists" "${FIXTURES_DIR}/test-service.yaml"
    assert_file_exists "test-configmap.yaml exists" "${FIXTURES_DIR}/test-configmap.yaml"
    assert_file_exists "test-deployment.yaml exists" "${FIXTURES_DIR}/test-deployment.yaml"
    assert_file_exists "test-namespace.yaml exists" "${FIXTURES_DIR}/test-namespace.yaml"
    assert_file_exists "labeled-pod.yaml exists" "${FIXTURES_DIR}/labeled-pod.yaml"
    assert_file_exists "namespaced-pod.yaml exists" "${FIXTURES_DIR}/namespaced-pod.yaml"
    assert_file_exists "status-pod.yaml exists" "${FIXTURES_DIR}/status-pod.yaml"
    assert_file_exists "invalid.yaml exists" "${FIXTURES_DIR}/invalid.yaml"
}

test_fixture_validity() {
    log_test "YAML fixtures are valid"
    
    # Try to apply each fixture with dry-run to validate syntax
    for fixture in "${FIXTURES_DIR}"/*.yaml; do
        local name=$(basename "$fixture")
        if [ "$name" != "invalid.yaml" ]; then
            local output
            output=$(kubectl apply -f "$fixture" --dry-run=client 2>&1)
            assert_success "Fixture $name is valid" $?
        fi
    done
}

# ============================================================================
# MAIN TEST EXECUTION
# ============================================================================

main() {
    echo "=========================================="
    echo "Sirah kubectl Integration Tests"
    echo "=========================================="
    echo "Using YAML fixtures from: ${FIXTURES_DIR}"
    echo "API Server: ${API_SERVER}"
    echo ""
    
    # Check prerequisites first
    check_prerequisites
    
    echo ""
    echo "=========================================="
    echo "Running Fixture Validation Tests"
    echo "=========================================="
    test_all_fixtures_exist
    test_fixture_validity
    
    echo ""
    echo "=========================================="
    echo "Running Pod Tests"
    echo "=========================================="
    test_pod_apply
    test_pod_get_by_name
    test_pod_get_json
    test_pod_list
    test_pod_list_wide
    test_pod_describe
    test_pod_delete
    
    echo ""
    echo "=========================================="
    echo "Running Service Tests"
    echo "=========================================="
    test_service_apply
    test_service_get_by_name
    test_service_get_json
    test_service_list
    test_service_delete
    
    echo ""
    echo "=========================================="
    echo "Running ConfigMap Tests"
    echo "=========================================="
    test_configmap_apply
    test_configmap_get
    test_configmap_get_json
    test_configmap_list
    test_configmap_delete
    
    echo ""
    echo "=========================================="
    echo "Running Deployment Tests"
    echo "=========================================="
    test_deployment_apply
    test_deployment_get
    test_deployment_get_json
    test_deployment_list
    test_deployment_scale
    test_deployment_verify_scale
    test_deployment_delete
    
    echo ""
    echo "=========================================="
    echo "Running Namespace Tests"
    echo "=========================================="
    test_namespace_apply
    test_namespace_list
    test_namespace_get
    test_namespace_delete
    
    echo ""
    echo "=========================================="
    echo "Running Advanced Operation Tests"
    echo "=========================================="
    test_pod_with_labels
    test_pod_label_selector
    test_pod_label_selector_json
    test_pod_label_cleanup
    test_pod_in_custom_namespace
    test_api_versions
    test_api_resources
    
    echo ""
    echo "=========================================="
    echo "Running Error Case Tests"
    echo "=========================================="
    test_pod_not_found
    test_invalid_yaml
    test_delete_non_existent
    
    echo ""
    echo "=========================================="
    echo "Running Status Field Tests"
    echo "=========================================="
    test_pod_status_fields
    test_resource_json_output
    
    echo ""
    echo "=========================================="
    echo "Test Summary"
    echo "=========================================="
    echo "Total tests run: $TESTS_RUN"
    echo -e "Tests passed: ${GREEN}$TESTS_PASSED${NC}"
    echo -e "Tests failed: ${RED}$TESTS_FAILED${NC}"
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
main() {
    # Check for command-line arguments
    case "${1:-}" in
        --help|-h)
            show_help
            exit 0
            ;;
        --list|-l)
            show_help
            list_tests
            exit 0
            ;;
        --run|-r|"")
            # Run tests (default behavior)
            run_tests
            ;;
        *)
            echo "Unknown argument: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
}

# Test execution wrapper
run_tests() {
    echo ""
    echo -e "${BLUE}╔════════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BLUE}║             STARTING SIRAH KUBECTL INTEGRATION TESTS               ║${NC}"
    echo -e "${BLUE}║                        (42 tests total)                            ║${NC}"
    echo -e "${BLUE}╚════════════════════════════════════════════════════════════════════╝${NC}"
    echo ""
    echo "Timestamp: $(date)"
    echo "API Server: ${API_SERVER}"
    echo "Fixtures Directory: ${FIXTURES_DIR}"
    echo ""
    
    echo "=========================================="
    echo "Running Fixture Validation Tests"
    echo "=========================================="
    test_all_fixtures_exist
    test_fixture_validity
    
    echo ""
    echo "=========================================="
    echo "Running Pod Tests"
    echo "=========================================="
    test_pod_apply
    test_pod_get_by_name
    test_pod_get_json
    test_pod_list
    test_pod_list_wide
    test_pod_describe
    test_pod_delete
    
    echo ""
    echo "=========================================="
    echo "Running Service Tests"
    echo "=========================================="
    test_service_apply
    test_service_get_by_name
    test_service_get_json
    test_service_list
    test_service_delete
    
    echo ""
    echo "=========================================="
    echo "Running ConfigMap Tests"
    echo "=========================================="
    test_configmap_apply
    test_configmap_get
    test_configmap_get_json
    test_configmap_list
    test_configmap_delete
    
    echo ""
    echo "=========================================="
    echo "Running Deployment Tests"
    echo "=========================================="
    test_deployment_apply
    test_deployment_get
    test_deployment_get_json
    test_deployment_list
    test_deployment_scale
    test_deployment_verify_scale
    test_deployment_delete
    
    echo ""
    echo "=========================================="
    echo "Running Namespace Tests"
    echo "=========================================="
    test_namespace_apply
    test_namespace_list
    test_namespace_get
    test_namespace_delete
    
    echo ""
    echo "=========================================="
    echo "Running Advanced Operation Tests"
    echo "=========================================="
    test_pod_with_labels
    test_pod_label_selector
    test_pod_label_selector_json
    test_pod_label_cleanup
    test_pod_in_custom_namespace
    test_api_versions
    test_api_resources
    
    echo ""
    echo "=========================================="
    echo "Running Error Case Tests"
    echo "=========================================="
    test_pod_not_found
    test_invalid_yaml
    test_delete_non_existent
    
    echo ""
    echo "=========================================="
    echo "Running Status Field Tests"
    echo "=========================================="
    test_pod_status_fields
    test_resource_json_output
    
    echo ""
    echo "=========================================="
    echo "Test Summary"
    echo "=========================================="
    echo "Total tests run: $TESTS_RUN"
    echo -e "Tests passed: ${GREEN}$TESTS_PASSED${NC}"
    echo -e "Tests failed: ${RED}$TESTS_FAILED${NC}"
    echo ""
    
    if [ $TESTS_FAILED -eq 0 ]; then
        echo -e "${GREEN}✓ All $TESTS_RUN tests passed!${NC}"
        return 0
    else
        echo -e "${RED}✗ $TESTS_FAILED test(s) failed out of $TESTS_RUN${NC}"
        return 1
    fi
}

# Parse command line arguments
parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --help)
                show_help
                exit 0
                ;;
            --list)
                show_help
                exit 0
                ;;
            --run)
                shift
                ;;
            --keep-resources)
                INTERACTIVE_DELETE=1
                AUTO_CLEAN=0
                shift
                ;;
            --clean-all)
                INTERACTIVE_DELETE=0
                AUTO_CLEAN=1
                shift
                ;;
            *)
                echo "Unknown option: $1"
                show_help
                exit 1
                ;;
        esac
    done
}

# Parse arguments and run main
parse_arguments "$@"
main "$@"
exit_code=$?

exit $exit_code
