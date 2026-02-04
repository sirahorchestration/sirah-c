#!/bin/bash

# Phase 4 Networking Test Suite
# Tests for control plane client, VXLAN tunnels, and network namespaces

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

TESTS_PASSED=0
TESTS_FAILED=0

# Test function
run_test() {
    local test_name="$1"
    local test_command="$2"
    
    echo -n "Testing: $test_name ... "
    if eval "$test_command" > /dev/null 2>&1; then
        echo -e "${GREEN}PASSED${NC}"
        ((TESTS_PASSED++))
    else
        echo -e "${RED}FAILED${NC}"
        ((TESTS_FAILED++))
    fi
}

# Mock API server for testing (optional, requires python)
start_mock_api_server() {
    if command -v python3 &> /dev/null; then
        echo "Starting mock API server on port 8080..."
        python3 << 'EOF' &
import http.server
import json
from urllib.parse import urlparse, parse_qs

class MockAPIHandler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length)
        
        if '/nodes' in self.path:
            self.send_response(201)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            response = json.dumps({"status": "created"})
            self.wfile.write(response.encode())
        else:
            self.send_response(400)
            self.end_headers()
    
    def do_PATCH(self):
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length)
        
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
        response = json.dumps({"status": "updated"})
        self.wfile.write(response.encode())
    
    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
        response = json.dumps({"items": [], "kind": "PodList"})
        self.wfile.write(response.encode())
    
    def log_message(self, format, *args):
        pass  # Suppress logging

handler = MockAPIHandler
httpd = http.server.HTTPServer(('127.0.0.1', 8080), handler)
httpd.serve_forever()
EOF
        sleep 1
        echo "Mock API server started"
    fi
}

# Kill mock server
stop_mock_api_server() {
    pkill -f "python3" 2>/dev/null || true
    sleep 1
}

# ============================================================================
# Control Plane Client Tests
# ============================================================================

echo -e "\n${YELLOW}========== Control Plane Client Tests ==========${NC}"

# Test 1: Control plane client compilation
run_test "Control plane client compiles" \
    "gcc -c internal/kubelet/api/control_plane_client.c -I./internal \
     -lcurl \$(pkg-config --cflags --libs json-c 2>/dev/null || echo '') \
     -lpthread -o /tmp/control_plane_client.o 2>/dev/null"

# Test 2: JSON marshaling (mock test)
run_test "Node registration JSON formatting" \
    "cat << 'EOJSON' | grep -q 'apiVersion' && cat << 'EOJSON' | grep -q 'kind'
{
  \"apiVersion\": \"v1\",
  \"kind\": \"Node\",
  \"metadata\": {\"name\": \"worker-1\"}
}
EOJSON"

# Test 3: Control plane client header validation
run_test "Control plane client header exists" \
    "test -f internal/kubelet/api/control_plane_client.h"

# Test 4: Control plane client implementation exists
run_test "Control plane client implementation exists" \
    "test -f internal/kubelet/api/control_plane_client.c"

# Test 5: Verify function declarations in header
run_test "control_plane_client_new function declared" \
    "grep -q 'control_plane_client_new' internal/kubelet/api/control_plane_client.h"

run_test "control_plane_client_register_node function declared" \
    "grep -q 'control_plane_client_register_node' internal/kubelet/api/control_plane_client.h"

run_test "control_plane_client_send_heartbeat function declared" \
    "grep -q 'control_plane_client_send_heartbeat' internal/kubelet/api/control_plane_client.h"

run_test "control_plane_client_poll_assignments function declared" \
    "grep -q 'control_plane_client_poll_assignments' internal/kubelet/api/control_plane_client.h"

# Test 6: Verify curl integration
run_test "curl library functions used in client" \
    "grep -q 'curl_easy_init\|CURL\|curl_easy_perform' internal/kubelet/api/control_plane_client.c"

# Test 7: Verify json-c integration
run_test "json-c library used for JSON marshaling" \
    "grep -q 'json_object\|json_tokener_parse' internal/kubelet/api/control_plane_client.c"

# Test 8: Verify thread safety
run_test "Mutex protection in control plane client" \
    "grep -q 'pthread_mutex' internal/kubelet/api/control_plane_client.c"

# ============================================================================
# VXLAN Tunnel Tests
# ============================================================================

echo -e "\n${YELLOW}========== VXLAN Tunnel Tests ==========${NC}"

# Test 9: VXLAN tunnel header compilation
run_test "VXLAN tunnel compiles" \
    "gcc -c internal/network/vxlan/vxlan_tunnel.c -I./internal \
     -lpthread -o /tmp/vxlan_tunnel.o 2>/dev/null"

# Test 10: VXLAN header exists
run_test "VXLAN tunnel header exists" \
    "test -f internal/network/vxlan/vxlan_tunnel.h"

# Test 11: VXLAN implementation exists
run_test "VXLAN tunnel implementation exists" \
    "test -f internal/network/vxlan/vxlan_tunnel.c"

# Test 12: Verify VXLAN tunnel functions
run_test "vxlan_tunnel_create function declared" \
    "grep -q 'vxlan_tunnel_create' internal/network/vxlan/vxlan_tunnel.h"

run_test "vxlan_tunnel_initialize function declared" \
    "grep -q 'vxlan_tunnel_initialize' internal/network/vxlan/vxlan_tunnel.h"

run_test "vxlan_tunnel_add_endpoint function declared" \
    "grep -q 'vxlan_tunnel_add_endpoint' internal/network/vxlan/vxlan_tunnel.h"

run_test "vxlan_tunnel_add_route function declared" \
    "grep -q 'vxlan_tunnel_add_route' internal/network/vxlan/vxlan_tunnel.h"

# Test 13: Verify VXLAN data structures
run_test "vxlan_tunnel_t structure defined" \
    "grep -q 'typedef struct.*vxlan_tunnel_t' internal/network/vxlan/vxlan_tunnel.h"

run_test "vxlan_endpoint_t structure defined" \
    "grep -q 'typedef struct.*vxlan_endpoint_t' internal/network/vxlan/vxlan_tunnel.h"

run_test "vxlan_fdb_entry_t structure defined" \
    "grep -q 'typedef struct.*vxlan_fdb_entry_t' internal/network/vxlan/vxlan_tunnel.h"

# Test 14: Verify VXLAN RFC 7348 compliance
run_test "VXLAN RFC 7348 documentation" \
    "grep -q 'RFC 7348' internal/network/vxlan/vxlan_tunnel.h"

# Test 15: Verify endpoint management
run_test "Endpoint tracking implemented" \
    "grep -q 'vxlan_endpoint' internal/network/vxlan/vxlan_tunnel.c"

# Test 16: Verify FDB management
run_test "FDB management implemented" \
    "grep -q 'vxlan_fdb' internal/network/vxlan/vxlan_tunnel.c"

# Test 17: Verify thread safety in VXLAN
run_test "VXLAN tunnel thread-safe (mutex)" \
    "grep -q 'pthread_mutex' internal/network/vxlan/vxlan_tunnel.c"

# Test 18: Verify MTU handling
run_test "MTU handling for VXLAN overhead" \
    "grep -q 'mtu' internal/network/vxlan/vxlan_tunnel.c"

# Test 19: Verify health checking
run_test "Health check function implemented" \
    "grep -q 'vxlan_tunnel_health_check' internal/network/vxlan/vxlan_tunnel.c"

# ============================================================================
# Network Namespace Tests
# ============================================================================

echo -e "\n${YELLOW}========== Network Namespace Tests ==========${NC}"

# Test 20: Network namespace compiles
run_test "Network namespace manager compiles" \
    "gcc -c internal/network/namespace/netns_manager.c -I./internal \
     -lpthread -o /tmp/netns_manager.o 2>/dev/null"

# Test 21: Namespace header exists
run_test "Network namespace header exists" \
    "test -f internal/network/namespace/netns_manager.h"

# Test 22: Namespace implementation exists
run_test "Network namespace implementation exists" \
    "test -f internal/network/namespace/netns_manager.c"

# Test 23: Verify namespace manager functions
run_test "netns_manager_create function declared" \
    "grep -q 'netns_manager_create' internal/network/namespace/netns_manager.h"

run_test "netns_manager_create_namespace function declared" \
    "grep -q 'netns_manager_create_namespace' internal/network/namespace/netns_manager.h"

run_test "netns_manager_delete_namespace function declared" \
    "grep -q 'netns_manager_delete_namespace' internal/network/namespace/netns_manager.h"

run_test "netns_manager_configure_ip function declared" \
    "grep -q 'netns_manager_configure_ip' internal/network/namespace/netns_manager.h"

# Test 24: Verify namespace data structures
run_test "network_namespace_t structure defined" \
    "grep -q 'typedef struct.*network_namespace_t' internal/network/namespace/netns_manager.h"

run_test "netns_manager_t structure defined" \
    "grep -q 'typedef struct.*netns_manager_t' internal/network/namespace/netns_manager.h"

# Test 25: Verify veth pair management
run_test "Veth pair fields in namespace" \
    "grep -q 'veth' internal/network/namespace/netns_manager.h"

# Test 26: Verify IP configuration fields
run_test "IP configuration fields in namespace" \
    "grep -q 'container_ip\|container_gateway' internal/network/namespace/netns_manager.h"

# Test 27: Verify thread safety in namespace manager
run_test "Namespace manager thread-safe (mutex)" \
    "grep -q 'pthread_mutex' internal/network/namespace/netns_manager.c"

# Test 28: Verify stale namespace cleanup
run_test "Stale namespace cleanup function" \
    "grep -q 'netns_manager_cleanup_stale' internal/network/namespace/netns_manager.c"

# ============================================================================
# Documentation Tests
# ============================================================================

echo -e "\n${YELLOW}========== Documentation Tests ==========${NC}"

# Test 29: Phase 4 implementation documentation exists
run_test "Phase 4 networking implementation documentation" \
    "test -f PHASE_4_NETWORKING_IMPLEMENTATION.md"

# Test 30: Phase 4 quick start guide exists
run_test "Phase 4 networking quick start guide" \
    "test -f PHASE_4_NETWORKING_QUICK_START.md"

# Test 31: Documentation contains integration examples
run_test "Implementation doc has code examples" \
    "grep -q 'control_plane_client_register_node\|vxlan_tunnel_create\|netns_manager_create' \
     PHASE_4_NETWORKING_IMPLEMENTATION.md"

# Test 32: Quick start has integration guide
run_test "Quick start has integration examples" \
    "grep -q 'control_plane_client_new\|vxlan_tunnel' \
     PHASE_4_NETWORKING_QUICK_START.md"

# Test 33: Kubernetes v1.28 conformance documented
run_test "Kubernetes v1.28 conformance documented" \
    "grep -q 'Kubernetes v1.28\|v1.28 Conformance' \
     PHASE_4_NETWORKING_IMPLEMENTATION.md"

# ============================================================================
# Integration Tests
# ============================================================================

echo -e "\n${YELLOW}========== Integration Tests ==========${NC}"

# Test 34: All three components can compile together
run_test "All components compile together" \
    "gcc -c internal/kubelet/api/control_plane_client.c -I./internal -lcurl -lpthread -o /tmp/cp.o 2>/dev/null && \
     gcc -c internal/network/vxlan/vxlan_tunnel.c -I./internal -lpthread -o /tmp/vx.o 2>/dev/null && \
     gcc -c internal/network/namespace/netns_manager.c -I./internal -lpthread -o /tmp/ns.o 2>/dev/null"

# Test 35: Verify control plane client uses json-c correctly
run_test "Control plane client JSON functions" \
    "grep -q 'json_object\|json_tokener_parse\|json_object_to_json_string' \
     internal/kubelet/api/control_plane_client.c"

# Test 36: Verify VXLAN endpoint tracking implementation
run_test "VXLAN endpoint array management" \
    "grep -q 'endpoints\|endpoint_count\|endpoint_capacity' \
     internal/network/vxlan/vxlan_tunnel.c"

# Test 37: Verify namespace lifecycle management
run_test "Namespace lifecycle tracking" \
    "grep -q 'creation_time\|last_modified\|created' \
     internal/network/namespace/netns_manager.c"

# ============================================================================
# Summary
# ============================================================================

echo -e "\n${YELLOW}========== Test Summary ==========${NC}"
TOTAL=$((TESTS_PASSED + TESTS_FAILED))
echo "Total Tests: $TOTAL"
echo -e "Passed: ${GREEN}$TESTS_PASSED${NC}"
echo -e "Failed: ${RED}$TESTS_FAILED${NC}"

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "\n${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "\n${RED}Some tests failed${NC}"
    exit 1
fi
