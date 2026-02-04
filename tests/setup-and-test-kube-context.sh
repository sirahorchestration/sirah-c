#!/bin/bash

# Script: setup-and-test-kube-context.sh
# Description: Sets the default kube context to the local dev server and tests the connection
# 
# This script:
# 1. Identifies the sirah kubeconfig location
# 2. Sets up the kube context to use the local dev server
# 3. Verifies the context is properly set
# 4. Tests connectivity and functionality

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KUBECONFIG_FILE="${SCRIPT_DIR}/config/kubeconfig"
KUBECONFIG_BACKUP="${SCRIPT_DIR}/config/kubeconfig.backup.$(date +%s)"

echo -e "${BLUE}=== Kubernetes Context Setup and Test ===${NC}\n"

# Check if kubeconfig file exists
if [[ ! -f "$KUBECONFIG_FILE" ]]; then
    echo -e "${RED}Error: kubeconfig file not found at $KUBECONFIG_FILE${NC}"
    exit 1
fi

echo -e "${BLUE}Step 1: Backup existing kubeconfig${NC}"
cp "$KUBECONFIG_FILE" "$KUBECONFIG_BACKUP"
echo -e "${GREEN}✓ Backed up to: $KUBECONFIG_BACKUP${NC}\n"

# Check if we need to set up the home kubeconfig
KUBE_DIR="${HOME}/.kube"
KUBE_CONFIG="${KUBE_DIR}/config"

echo -e "${BLUE}Step 2: Set up kubeconfig in ~/.kube/config${NC}"
if [[ ! -d "$KUBE_DIR" ]]; then
    mkdir -p "$KUBE_DIR"
    echo -e "${YELLOW}Created ~/.kube directory${NC}"
fi

# Check if kubeconfig exists and merge or copy
if [[ -f "$KUBE_CONFIG" ]]; then
    echo -e "${YELLOW}Found existing ~/.kube/config, checking for sirah-local context...${NC}"
    
    # Check if sirah-local context already exists
    if kubectl config get-contexts sirah-local &>/dev/null; then
        echo -e "${YELLOW}sirah-local context already exists${NC}"
    else
        echo -e "${YELLOW}Adding sirah-local context to existing config...${NC}"
        # Temporarily merge configs to add the context
        KUBECONFIG="$KUBE_CONFIG:$KUBECONFIG_FILE" kubectl config view --merge --flatten > /tmp/merged-config
        mv /tmp/merged-config "$KUBE_CONFIG"
        chmod 600 "$KUBE_CONFIG"
        echo -e "${GREEN}✓ Merged sirah-local context${NC}"
    fi
else
    cp "$KUBECONFIG_FILE" "$KUBE_CONFIG"
    chmod 600 "$KUBE_CONFIG"
    echo -e "${GREEN}✓ Copied kubeconfig to ~/.kube/config${NC}"
fi

echo ""

echo -e "${BLUE}Step 3: Set default context to sirah-local${NC}"
kubectl config use-context sirah-local
echo -e "${GREEN}✓ Context set to sirah-local${NC}\n"

echo -e "${BLUE}Step 4: Verify context configuration${NC}"
CURRENT_CONTEXT=$(kubectl config current-context)
CURRENT_CLUSTER=$(kubectl config view --minify --raw -o jsonpath='{.contexts[0].context.cluster}')
CURRENT_USER=$(kubectl config view --minify --raw -o jsonpath='{.contexts[0].context.user}')

echo -e "  Current Context: ${GREEN}${CURRENT_CONTEXT}${NC}"
echo -e "  Cluster: ${GREEN}${CURRENT_CLUSTER}${NC}"
echo -e "  User: ${GREEN}${CURRENT_USER}${NC}\n"

echo -e "${BLUE}Step 5: Test connectivity to API server${NC}"
if kubectl cluster-info &>/dev/null; then
    echo -e "${GREEN}✓ Connected to API server${NC}"
    kubectl cluster-info
else
    echo -e "${YELLOW}⚠ API server not responding. This is expected if the server is not running.${NC}"
    echo -e "${YELLOW}  Make sure to start the server with: sirah-apiserver${NC}"
fi

echo ""
echo -e "${BLUE}Step 6: Test basic kubectl commands${NC}"

# Test 1: List API resources
echo -e "\n  Testing 'kubectl api-resources'..."
if kubectl api-resources &>/dev/null; then
    RESOURCE_COUNT=$(kubectl api-resources --no-headers 2>/dev/null | wc -l)
    echo -e "  ${GREEN}✓ Available resources: $RESOURCE_COUNT${NC}"
else
    echo -e "  ${YELLOW}⚠ API not fully responsive (server may not be running)${NC}"
fi

# Test 2: Get nodes
echo -e "\n  Testing 'kubectl get nodes'..."
if kubectl get nodes &>/dev/null; then
    NODE_COUNT=$(kubectl get nodes --no-headers 2>/dev/null | wc -l)
    echo -e "  ${GREEN}✓ Available nodes: $NODE_COUNT${NC}"
    kubectl get nodes
else
    echo -e "  ${YELLOW}⚠ Cannot retrieve nodes (server may not be running)${NC}"
fi

# Test 3: Get namespaces
echo -e "\n  Testing 'kubectl get namespaces'..."
if kubectl get namespaces &>/dev/null; then
    NS_COUNT=$(kubectl get namespaces --no-headers 2>/dev/null | wc -l)
    echo -e "  ${GREEN}✓ Available namespaces: $NS_COUNT${NC}"
else
    echo -e "  ${YELLOW}⚠ Cannot retrieve namespaces (server may not be running)${NC}"
fi

echo ""
echo -e "${GREEN}=== Setup Complete ===${NC}\n"

echo -e "${BLUE}Summary:${NC}"
echo -e "  • Default kubeconfig: ${GREEN}${KUBE_CONFIG}${NC}"
echo -e "  • Current context: ${GREEN}$(kubectl config current-context)${NC}"
echo -e "  • API Server: ${GREEN}http://localhost:6443${NC}"
echo -e "  • Backup location: ${YELLOW}${KUBECONFIG_BACKUP}${NC}\n"

echo -e "${BLUE}Next steps:${NC}"
echo "  1. Start the API server: sirah-apiserver"
echo "  2. Run tests: kubectl get nodes, kubectl get pods, etc."
echo "  3. To restore backup: cp ${KUBECONFIG_BACKUP} ${KUBE_CONFIG}\n"

exit 0
