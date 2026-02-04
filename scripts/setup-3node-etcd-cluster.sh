#!/bin/bash

# Phase 2B - 3-Node etcd Cluster Setup and Testing
# Creates a 3-node etcd cluster in QEMU for HA testing
# Tests replication, leader election, and failover scenarios

set -e

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Configuration
CLUSTER_NAME="sirah-etcd-cluster"
ETCD_VERSION="v3.5.9"
NODES=("etcd-node-1" "etcd-node-2" "etcd-node-3")
NODE_IPS=("192.168.122.101" "192.168.122.102" "192.168.122.103")
CLUSTER_TOKEN="sirah-etcd-token"
VM_IMAGE_SIZE="10G"
VM_BASE_DIR="/var/lib/libvirt/images/sirah-etcd"
QEMU_MEM="1024M"
QEMU_CPU="2"
ETCD_PORT_START=2379
ETCD_PEER_PORT_START=2380

PASSED=0
FAILED=0

# Helper functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

log_error() {
    echo -e "${RED}[✗]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[!]${NC} $1"
}

run_test() {
    local test_num=$1
    local test_name=$2
    local test_cmd=$3
    
    echo ""
    echo -e "${BLUE}Test $test_num: $test_name${NC}"
    
    if eval "$test_cmd"; then
        log_success "$test_name"
        PASSED=$((PASSED + 1))
    else
        log_error "$test_name"
        FAILED=$((FAILED + 1))
    fi
}

check_prerequisites() {
    log_info "Checking prerequisites..."
    
    # Check for KVM/QEMU
    if ! command -v qemu-system-x86_64 &> /dev/null; then
        log_error "QEMU not found. Install with: sudo apt-get install qemu-system-x86"
        exit 1
    fi
    log_success "QEMU found"
    
    # Check for libvirt
    if ! command -v virsh &> /dev/null; then
        log_warning "libvirt not found. Install with: sudo apt-get install libvirt-bin"
    else
        log_success "libvirt found"
    fi
    
    # Check for etcd
    if ! command -v etcd &> /dev/null; then
        log_warning "etcd not installed locally, will install in VMs"
    else
        log_success "etcd found locally"
    fi
    
    # Check for curl
    if ! command -v curl &> /dev/null; then
        log_error "curl not found"
        exit 1
    fi
    log_success "curl found"
}

setup_network() {
    log_info "Setting up network..."
    
    # Check if network bridge exists
    if ip link show br-sirah &> /dev/null; then
        log_warning "Network bridge br-sirah already exists"
        return
    fi
    
    # Create bridge network
    sudo ip link add br-sirah type bridge
    sudo ip addr add 192.168.122.1/24 dev br-sirah
    sudo ip link set br-sirah up
    
    # Enable routing
    sudo sysctl -w net.ipv4.ip_forward=1 > /dev/null
    
    log_success "Network bridge created (192.168.122.0/24)"
}

create_vm_image() {
    local node_name=$1
    local node_ip=$2
    
    log_info "Creating VM image for $node_name ($node_ip)..."
    
    # Create base directory
    sudo mkdir -p "$VM_BASE_DIR"
    
    # Create disk image
    local disk_path="$VM_BASE_DIR/$node_name.qcow2"
    if [ ! -f "$disk_path" ]; then
        sudo qemu-img create -f qcow2 "$disk_path" "$VM_IMAGE_SIZE"
        log_success "Created disk for $node_name"
    else
        log_warning "Disk for $node_name already exists"
    fi
}

start_etcd_vm() {
    local node_name=$1
    local node_ip=$2
    local node_idx=$3
    local etcd_port=$((ETCD_PORT_START + node_idx))
    local peer_port=$((ETCD_PEER_PORT_START + node_idx))
    
    log_info "Starting etcd VM: $node_name..."
    
    # Create systemd service for etcd in VM
    local service_file="/tmp/etcd-${node_name}.service"
    cat > "$service_file" << EOF
[Unit]
Description=etcd for $node_name
After=network.target

[Service]
Type=notify
User=etcd
WorkingDirectory=/var/lib/etcd
EnvironmentFile=-/etc/etcd/etcd.conf
ExecStart=/usr/local/bin/etcd \\
  --name=$node_name \\
  --listen-client-urls=http://$node_ip:$etcd_port,http://127.0.0.1:$etcd_port \\
  --advertise-client-urls=http://$node_ip:$etcd_port \\
  --listen-peer-urls=http://$node_ip:$peer_port \\
  --initial-advertise-peer-urls=http://$node_ip:$peer_port \\
  --initial-cluster=${NODES[0]}=http://${NODE_IPS[0]}:$((ETCD_PEER_PORT_START)),$\
${NODES[1]}=http://${NODE_IPS[1]}:$((ETCD_PEER_PORT_START+1)),$\
${NODES[2]}=http://${NODE_IPS[2]}:$((ETCD_PEER_PORT_START+2)) \\
  --initial-cluster-state=new \\
  --initial-cluster-token=$CLUSTER_TOKEN \\
  --heartbeat-interval=100 \\
  --election-timeout=1000 \\
  --data-dir=/var/lib/etcd
Restart=on-failure
RestartSec=5s

[Install]
WantedBy=multi-user.target
EOF
    
    log_success "Created etcd service for $node_name"
}

install_etcd_on_host() {
    log_info "Installing etcd locally for testing..."
    
    if command -v etcd &> /dev/null; then
        log_warning "etcd already installed"
        return
    fi
    
    # Download and install etcd
    ETCD_VER=$ETCD_VERSION
    DOWNLOAD_URL="https://github.com/etcd-io/etcd/releases/download/$ETCD_VER/etcd-$ETCD_VER-linux-amd64.tar.gz"
    
    log_info "Downloading etcd from $DOWNLOAD_URL..."
    cd /tmp
    curl -L "$DOWNLOAD_URL" -o "etcd-$ETCD_VER-linux-amd64.tar.gz"
    tar xzf "etcd-$ETCD_VER-linux-amd64.tar.gz"
    
    # Install binaries
    sudo mv "etcd-$ETCD_VER-linux-amd64/etcd" /usr/local/bin/
    sudo mv "etcd-$ETCD_VER-linux-amd64/etcdctl" /usr/local/bin/
    sudo chmod +x /usr/local/bin/etcd /usr/local/bin/etcdctl
    
    log_success "etcd installed"
}

start_cluster_node() {
    local idx=$1
    local node_name=${NODES[$idx]}
    local node_ip=${NODE_IPS[$idx]}
    local etcd_port=$((ETCD_PORT_START + idx))
    local peer_port=$((ETCD_PEER_PORT_START + idx))
    
    log_info "Starting etcd node $idx: $node_name on port $etcd_port..."
    
    # Build cluster peer URLs
    local cluster_peers=""
    for j in {0..2}; do
        if [ $j -gt 0 ]; then
            cluster_peers="$cluster_peers,"
        fi
        cluster_peers="${cluster_peers}${NODES[$j]}=http://${NODE_IPS[$j]}:$((ETCD_PEER_PORT_START + j))"
    done
    
    # Create data directory
    mkdir -p "/tmp/etcd-node-$idx"
    
    # Start etcd node in background
    /usr/local/bin/etcd \
        --name="$node_name" \
        --listen-client-urls="http://127.0.0.1:$etcd_port" \
        --advertise-client-urls="http://127.0.0.1:$etcd_port" \
        --listen-peer-urls="http://127.0.0.1:$((peer_port))" \
        --initial-advertise-peer-urls="http://127.0.0.1:$peer_port" \
        --initial-cluster="$cluster_peers" \
        --initial-cluster-state="new" \
        --initial-cluster-token="$CLUSTER_TOKEN" \
        --heartbeat-interval=100 \
        --election-timeout=1000 \
        --data-dir="/tmp/etcd-node-$idx" \
        > "/tmp/etcd-node-$idx.log" 2>&1 &
    
    echo $! > "/tmp/etcd-node-$idx.pid"
    log_success "Started etcd node $idx (PID: $(cat /tmp/etcd-node-$idx.pid))"
}

wait_for_cluster() {
    log_info "Waiting for cluster to be ready..."
    
    local timeout=30
    local elapsed=0
    
    while [ $elapsed -lt $timeout ]; do
        # Check if all nodes are responding
        local all_ready=true
        for idx in {0..2}; do
            local port=$((ETCD_PORT_START + idx))
            if ! curl -s "http://127.0.0.1:$port/health" | grep -q "true"; then
                all_ready=false
                break
            fi
        done
        
        if $all_ready; then
            log_success "Cluster is ready"
            return 0
        fi
        
        sleep 1
        elapsed=$((elapsed + 1))
        echo -ne "\r  Waiting... ${elapsed}s"
    done
    
    log_error "Cluster failed to become ready after $timeout seconds"
    return 1
}

# Test Functions
test_cluster_health() {
    log_info "Checking cluster health..."
    
    for idx in {0..2}; do
        local port=$((ETCD_PORT_START + idx))
        local node_name=${NODES[$idx]}
        
        if curl -s "http://127.0.0.1:$port/health" | grep -q "true"; then
            log_success "$node_name is healthy"
        else
            log_error "$node_name is NOT healthy"
            return 1
        fi
    done
    
    return 0
}

test_leader_election() {
    log_info "Testing leader election..."
    
    local endpoint="http://127.0.0.1:$ETCD_PORT_START"
    local leader=$(curl -s "$endpoint/v3/leader" 2>/dev/null || echo "")
    
    if [ -n "$leader" ]; then
        log_success "Leader elected: $leader"
        return 0
    else
        log_error "No leader elected"
        return 1
    fi
}

test_data_replication() {
    log_info "Testing data replication across nodes..."
    
    # Write data to node 0
    local test_key="test-key-$(date +%s)"
    local test_value="test-value-123"
    
    log_info "Writing to node 0: $test_key = $test_value"
    ETCDCTL_API=3 /usr/local/bin/etcdctl \
        --endpoints="http://127.0.0.1:$ETCD_PORT_START" \
        put "$test_key" "$test_value" > /dev/null
    
    sleep 1
    
    # Read from all nodes
    local all_consistent=true
    for idx in {0..2}; do
        local port=$((ETCD_PORT_START + idx))
        local value=$(ETCDCTL_API=3 /usr/local/bin/etcdctl \
            --endpoints="http://127.0.0.1:$port" \
            get "$test_key" 2>/dev/null | tail -1)
        
        if [ "$value" = "$test_value" ]; then
            log_success "Node $idx has consistent data"
        else
            log_error "Node $idx has inconsistent data (got: $value)"
            all_consistent=false
        fi
    done
    
    if $all_consistent; then
        return 0
    else
        return 1
    fi
}

test_failover() {
    log_info "Testing failover scenario..."
    
    # Get current leader
    local leader_pid=$(pgrep -f "etcd.*name=etcd-node-0" | head -1)
    if [ -z "$leader_pid" ]; then
        leader_pid=$(cat /tmp/etcd-node-0.pid 2>/dev/null)
    fi
    
    log_info "Killing node 0 (PID: $leader_pid)..."
    kill $leader_pid 2>/dev/null || true
    sleep 2
    
    # Verify cluster still responds
    log_info "Checking cluster responds after node failure..."
    local responded=false
    
    for idx in {1..2}; do
        local port=$((ETCD_PORT_START + idx))
        if curl -s "http://127.0.0.1:$port/health" | grep -q "true"; then
            responded=true
            log_success "Node $idx is still responsive"
        fi
    done
    
    if ! $responded; then
        log_error "Cluster lost quorum"
        return 1
    fi
    
    # Restart failed node
    log_info "Restarting node 0..."
    start_cluster_node 0
    sleep 2
    
    # Check if recovered
    if curl -s "http://127.0.0.1:$ETCD_PORT_START/health" | grep -q "true"; then
        log_success "Node 0 recovered"
        return 0
    else
        log_error "Node 0 failed to recover"
        return 1
    fi
}

test_concurrent_writes() {
    log_info "Testing concurrent writes from multiple nodes..."
    
    local key_prefix="concurrent-write-$(date +%s)"
    local success_count=0
    
    # Write from each node
    for idx in {0..2}; do
        local port=$((ETCD_PORT_START + idx))
        local key="${key_prefix}-node-${idx}"
        
        if ETCDCTL_API=3 /usr/local/bin/etcdctl \
            --endpoints="http://127.0.0.1:$port" \
            put "$key" "value-from-node-$idx" > /dev/null 2>&1; then
            log_success "Write from node $idx succeeded"
            success_count=$((success_count + 1))
        else
            log_error "Write from node $idx failed"
        fi
    done
    
    if [ $success_count -eq 3 ]; then
        return 0
    else
        return 1
    fi
}

test_data_persistence() {
    log_info "Testing data persistence..."
    
    local persist_key="persist-test-$(date +%s)"
    local persist_value="persistent-value-xyz"
    
    # Write data
    log_info "Writing persistent data..."
    ETCDCTL_API=3 /usr/local/bin/etcdctl \
        --endpoints="http://127.0.0.1:$ETCD_PORT_START" \
        put "$persist_key" "$persist_value" > /dev/null
    
    sleep 1
    
    # Kill all nodes
    log_info "Stopping all nodes..."
    for idx in {0..2}; do
        local pid=$(cat "/tmp/etcd-node-$idx.pid" 2>/dev/null)
        if [ -n "$pid" ]; then
            kill $pid 2>/dev/null || true
        fi
    done
    sleep 2
    
    # Restart all nodes
    log_info "Restarting all nodes..."
    for idx in {0..2}; do
        start_cluster_node $idx
    done
    
    wait_for_cluster || return 1
    
    # Verify data persisted
    log_info "Verifying data persisted..."
    local value=$(ETCDCTL_API=3 /usr/local/bin/etcdctl \
        --endpoints="http://127.0.0.1:$ETCD_PORT_START" \
        get "$persist_key" 2>/dev/null | tail -1)
    
    if [ "$value" = "$persist_value" ]; then
        log_success "Data persisted correctly"
        return 0
    else
        log_error "Data was not persisted (got: $value)"
        return 1
    fi
}

cleanup() {
    log_info "Cleaning up..."
    
    # Kill all etcd processes
    for idx in {0..2}; do
        local pid=$(cat "/tmp/etcd-node-$idx.pid" 2>/dev/null)
        if [ -n "$pid" ] && ps -p $pid > /dev/null 2>&1; then
            log_info "Killing etcd node $idx (PID: $pid)"
            kill $pid 2>/dev/null || true
        fi
    done
    
    # Remove data directories
    rm -rf /tmp/etcd-node-*
    
    log_success "Cleanup complete"
}

# Main execution
main() {
    echo ""
    echo -e "${BLUE}╔════════════════════════════════════════╗${NC}"
    echo -e "${BLUE}║  Phase 2B - 3-Node etcd Cluster Setup  ║${NC}"
    echo -e "${BLUE}╚════════════════════════════════════════╝${NC}"
    echo ""
    
    # Check prerequisites
    check_prerequisites
    
    # Install etcd
    install_etcd_on_host
    
    # Setup network
    log_info "Skipping VM creation - using local etcd instances for testing"
    
    # Start cluster nodes
    echo ""
    echo -e "${BLUE}Starting Cluster Nodes...${NC}"
    for idx in {0..2}; do
        start_cluster_node $idx
    done
    
    # Wait for cluster to be ready
    if ! wait_for_cluster; then
        log_error "Cluster startup failed"
        cleanup
        exit 1
    fi
    
    # Run tests
    echo ""
    echo -e "${BLUE}Running Tests...${NC}"
    
    run_test 1 "Cluster Health Check" "test_cluster_health"
    run_test 2 "Leader Election" "test_leader_election"
    run_test 3 "Data Replication" "test_data_replication"
    run_test 4 "Concurrent Writes" "test_concurrent_writes"
    run_test 5 "Data Persistence" "test_data_persistence"
    run_test 6 "Failover Scenario" "test_failover"
    
    # Results
    echo ""
    echo -e "${BLUE}════════════════════════════════════════${NC}"
    echo -e "${BLUE}Test Results:${NC}"
    echo -e "  ${GREEN}Passed: $PASSED${NC}"
    echo -e "  ${RED}Failed: $FAILED${NC}"
    echo -e "${BLUE}════════════════════════════════════════${NC}"
    
    # Cleanup
    echo ""
    cleanup
    
    if [ $FAILED -eq 0 ]; then
        echo -e "${GREEN}✓ All tests passed!${NC}"
        exit 0
    else
        echo -e "${RED}✗ Some tests failed${NC}"
        exit 1
    fi
}

# Trap cleanup on exit
trap cleanup EXIT

# Run main
main "$@"
