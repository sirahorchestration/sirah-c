#!/bin/bash
# tests/run_all_tests.sh
# Master test runner for unit, integration, and e2e tests

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

echo "=========================================="
echo "Sirah Kubernetes E2E Test Suite"
echo "=========================================="
echo ""
echo "Project Root: $PROJECT_ROOT"
echo "Current Time: $(date)"
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Track results
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Function to run a test file
run_test() {
    local test_file=$1
    local test_name=$(basename "$test_file")
    
    echo ""
    echo "Running: $test_name"
    echo "---"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    if [ -f "$test_file" ]; then
        if bash "$test_file"; then
            echo -e "${GREEN}✓ PASSED${NC}: $test_name"
            PASSED_TESTS=$((PASSED_TESTS + 1))
        else
            echo -e "${RED}✗ FAILED${NC}: $test_name"
            FAILED_TESTS=$((FAILED_TESTS + 1))
        fi
    else
        echo -e "${YELLOW}⚠ SKIPPED${NC}: $test_name (file not found)"
    fi
}

# Function to compile and run C unit tests
run_c_test() {
    local test_file=$1
    local test_name=$(basename "$test_file" .c)
    
    echo ""
    echo "Running: $test_name"
    echo "---"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    if [ -f "$test_file" ]; then
        # Compile
        local output_binary="/tmp/${test_name}"
        if gcc -std=c99 "$test_file" -ljson-c -o "$output_binary" 2>/dev/null; then
            # Run
            if "$output_binary"; then
                echo -e "${GREEN}✓ PASSED${NC}: $test_name"
                PASSED_TESTS=$((PASSED_TESTS + 1))
            else
                echo -e "${RED}✗ FAILED${NC}: $test_name (execution failed)"
                FAILED_TESTS=$((FAILED_TESTS + 1))
            fi
            rm -f "$output_binary"
        else
            echo -e "${RED}✗ FAILED${NC}: $test_name (compilation failed)"
            FAILED_TESTS=$((FAILED_TESTS + 1))
        fi
    else
        echo -e "${YELLOW}⚠ SKIPPED${NC}: $test_name (file not found)"
    fi
}

# Phase 1: Unit Tests
echo ""
echo "=========================================="
echo "Phase 1: Running Unit Tests"
echo "=========================================="

if [ -d "tests/unit" ]; then
    for test_file in tests/unit/test_*.c; do
        if [ -f "$test_file" ]; then
            run_c_test "$test_file"
        fi
    done
else
    echo "⚠ Unit tests directory not found"
fi

# Phase 2: Integration Tests (requires running cluster)
echo ""
echo "=========================================="
echo "Phase 2: Running Integration Tests"
echo "=========================================="
echo ""

# Check if cluster is running
if curl -s http://localhost:6443/healthz | grep -q "ok"; then
    echo "✓ Cluster is running"
    echo ""
    
    if [ -d "tests/integration" ]; then
        for test_file in tests/integration/test_*.sh; do
            if [ -f "$test_file" ]; then
                chmod +x "$test_file"
                run_test "$test_file"
            fi
        done
    else
        echo "⚠ Integration tests directory not found"
    fi
else
    echo -e "${YELLOW}⚠ SKIPPED${NC}: Cluster is not running"
    echo "   To run integration tests, start the cluster first:"
    echo "   cd sirah && ./bin/sirah-apiserver --port 6443 &"
    echo "   ./bin/sirah-scheduler &"
    echo "   ./bin/sirah-controller &"
    echo "   ./bin/sirah-kubelet --node-name worker1 &"
fi

# Phase 3: End-to-End Tests (requires running cluster)
echo ""
echo "=========================================="
echo "Phase 3: Running End-to-End Tests"
echo "=========================================="
echo ""

if curl -s http://localhost:6443/healthz | grep -q "ok"; then
    if [ -d "tests/e2e" ]; then
        for test_file in tests/e2e/test_*.sh; do
            if [ -f "$test_file" ]; then
                chmod +x "$test_file"
                run_test "$test_file"
            fi
        done
    else
        echo "⚠ E2E tests directory not found"
    fi
else
    echo -e "${YELLOW}⚠ SKIPPED${NC}: Cluster is not running"
fi

# Summary
echo ""
echo "=========================================="
echo "Test Summary"
echo "=========================================="
echo "Total Tests: $TOTAL_TESTS"
echo -e "Passed: ${GREEN}$PASSED_TESTS${NC}"
echo -e "Failed: ${RED}$FAILED_TESTS${NC}"
echo ""

if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "${GREEN}All tests passed! ✓${NC}"
    echo "=========================================="
    exit 0
else
    echo -e "${RED}Some tests failed!${NC}"
    echo "=========================================="
    exit 1
fi
