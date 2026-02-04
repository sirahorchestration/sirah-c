#!/bin/bash
# Sonobuoy Setup and Conformance Testing Script for Sirah
# Usage: ./setup-sonobuoy.sh [install|run-lite|run-quick|run-full|analyze|cleanup]

set -e

SONOBUOY_VERSION="0.57.1"
SONOBUOY_DIR="./sonobuoy"
RESULTS_DIR="./conformance-results"

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check prerequisites
check_prerequisites() {
    log_info "Checking prerequisites..."
    
    if ! command -v kubectl &> /dev/null; then
        log_error "kubectl not found. Please install kubectl first."
        exit 1
    fi
    
    if ! command -v jq &> /dev/null; then
        log_warn "jq not found. Installing jq would improve result analysis."
    fi
    
    if ! kubectl cluster-info &> /dev/null; then
        log_error "Kubernetes cluster not accessible. Ensure Sirah is running."
        exit 1
    fi
    
    log_success "Prerequisites check passed"
}

# Install Sonobuoy
install_sonobuoy() {
    log_info "Installing Sonobuoy v${SONOBUOY_VERSION}..."
    
    OS=$(uname -s | tr '[:upper:]' '[:lower:]')
    ARCH=$(uname -m)
    
    if [ "$ARCH" = "x86_64" ]; then
        ARCH="amd64"
    fi
    
    URL="https://github.com/vmware-tanzu/sonobuoy/releases/download/v${SONOBUOY_VERSION}/sonobuoy_${OS}_${ARCH}.tar.gz"
    
    log_info "Downloading from: $URL"
    
    mkdir -p "$SONOBUOY_DIR"
    cd "$SONOBUOY_DIR"
    
    curl -sfL -O "$URL"
    tar -xzf "sonobuoy_${OS}_${ARCH}.tar.gz"
    chmod +x sonobuoy
    
    cd ..
    
    log_success "Sonobuoy installed to $SONOBUOY_DIR"
    
    # Version check
    $SONOBUOY_DIR/sonobuoy version
}

# Run lite conformance tests (quick check)
run_lite() {
    log_info "Running LITE conformance tests (~10-15 minutes)..."
    
    $SONOBUOY_DIR/sonobuoy run --mode=lite --wait
    
    log_success "Lite tests completed"
}

# Run quick conformance tests
run_quick() {
    log_info "Running QUICK conformance tests (~30-45 minutes)..."
    
    $SONOBUOY_DIR/sonobuoy run --mode=quick --wait
    
    log_success "Quick tests completed"
}

# Run full certified-conformance tests
run_full() {
    log_info "Running FULL certified-conformance tests (~4-8 hours)..."
    log_warn "This will take a long time. You can monitor progress with:"
    echo "   sonobuoy status"
    echo "   sonobuoy logs -f"
    
    read -p "Continue? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        log_info "Cancelled"
        return
    fi
    
    $SONOBUOY_DIR/sonobuoy run --mode=certified-conformance --wait --timeout=28800
    
    log_success "Full tests completed"
}

# Retrieve and analyze results
analyze_results() {
    log_info "Retrieving test results..."
    
    outfile=$($SONOBUOY_DIR/sonobuoy retrieve)
    
    log_info "Extracting results..."
    mkdir -p "$RESULTS_DIR"
    tar xzf "$outfile" -C "$RESULTS_DIR"
    
    log_success "Results extracted to $RESULTS_DIR"
    
    log_info "Analyzing test results..."
    echo
    
    RESULTS_JSON="$RESULTS_DIR/plugins/e2e/results/global/e2e.json"
    
    if [ -f "$RESULTS_JSON" ]; then
        if command -v jq &> /dev/null; then
            TOTAL=$(jq '.[].result' "$RESULTS_JSON" | wc -l)
            PASSED=$(jq '.[] | select(.result == "passed")' "$RESULTS_JSON" | wc -l)
            FAILED=$(jq '.[] | select(.result == "failed")' "$RESULTS_JSON" | wc -l)
            SKIPPED=$(jq '.[] | select(.result == "skipped")' "$RESULTS_JSON" | wc -l)
            
            PERCENT=$((PASSED * 100 / TOTAL))
            
            echo "╔════════════════════════════════════════╗"
            echo "║ SONOBUOY CONFORMANCE TEST RESULTS      ║"
            echo "╠════════════════════════════════════════╣"
            echo "║ Total Tests:     $TOTAL"
            printf "║ Passed:          %d (%d%%) ✓\n" $PASSED $PERCENT
            printf "║ Failed:          %d\n" $FAILED
            printf "║ Skipped:         %d\n" $SKIPPED
            echo "╠════════════════════════════════════════╣"
            echo "║ Conformance Score: $PERCENT%"
            echo "╚════════════════════════════════════════╝"
            echo
            
            if [ $FAILED -gt 0 ]; then
                echo "Top Failed Tests:"
                jq '.[] | select(.result == "failed") | .name' "$RESULTS_JSON" | head -10 | sed 's/"//g' | nl
                echo
            fi
            
            if [ $SKIPPED -gt 0 ]; then
                echo "Skipped Tests (sample):"
                jq '.[] | select(.result == "skipped") | .name' "$RESULTS_JSON" | head -5 | sed 's/"//g' | nl
                echo
            fi
        else
            log_warn "jq not installed; showing raw summary instead"
            if [ -f "$RESULTS_DIR/plugins/e2e/results/global/summary.txt" ]; then
                cat "$RESULTS_DIR/plugins/e2e/results/global/summary.txt"
            fi
        fi
    else
        log_error "Results JSON not found: $RESULTS_JSON"
        return 1
    fi
    
    log_success "Analysis complete. Full results in: $RESULTS_DIR"
}

# Cleanup Sonobuoy resources
cleanup() {
    log_info "Cleaning up Sonobuoy resources..."
    
    $SONOBUOY_DIR/sonobuoy delete --wait
    
    log_success "Cleanup complete"
}

# Check current status
check_status() {
    log_info "Checking Sonobuoy status..."
    $SONOBUOY_DIR/sonobuoy status
    echo
    
    log_info "Checking kubectl cluster..."
    kubectl cluster-info
    echo
    
    log_info "Checking Sirah components..."
    kubectl get nodes 2>/dev/null || log_warn "No nodes available"
    kubectl get pods -A 2>/dev/null | head -20
}

# Main command handler
case "${1:-help}" in
    install)
        check_prerequisites
        install_sonobuoy
        ;;
    lite)
        run_lite
        analyze_results
        ;;
    quick)
        run_quick
        analyze_results
        ;;
    full)
        run_full
        analyze_results
        ;;
    analyze)
        analyze_results
        ;;
    status)
        check_status
        ;;
    cleanup)
        cleanup
        ;;
    help|--help|-h)
        cat << EOF
Sonobuoy Conformance Testing Script for Sirah

Usage: $0 [COMMAND]

Commands:
    install         Install Sonobuoy
    lite            Run lite conformance tests (~15 min)
    quick           Run quick conformance tests (~45 min)
    full            Run full certified-conformance tests (~4-8 hours)
    analyze         Analyze existing test results
    status          Check Sonobuoy and cluster status
    cleanup         Clean up Sonobuoy resources
    help            Show this help message

Examples:
    # Install Sonobuoy
    $0 install

    # Run quick test and analyze
    $0 quick

    # Run full certification (long running)
    $0 full

    # Check results from previous run
    $0 analyze

    # Clean up and stop tests
    $0 cleanup
EOF
        ;;
    *)
        log_error "Unknown command: $1"
        echo "Run '$0 help' for usage information"
        exit 1
        ;;
esac
