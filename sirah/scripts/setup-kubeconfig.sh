#!/bin/bash
# Setup kubeconfig for Sirah Kubernetes cluster

set -e

KUBECONFIG_DIR="${HOME}/.kube"
KUBECONFIG_FILE="${KUBECONFIG_DIR}/config"
API_SERVER="https://localhost:6443"
CLUSTER_NAME="sirah-local"
CONTEXT_NAME="sirah-local"
USER_NAME="sirah-admin"

# Ensure .kube directory exists
mkdir -p "$KUBECONFIG_DIR"

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}Setting up kubeconfig for Sirah...${NC}"

# Create or merge kubeconfig
if [ -f "$KUBECONFIG_FILE" ]; then
    echo "Updating existing kubeconfig at $KUBECONFIG_FILE"
    KUBECONFIG_BACKUP="${KUBECONFIG_FILE}.backup.$(date +%s)"
    cp "$KUBECONFIG_FILE" "$KUBECONFIG_BACKUP"
    echo "Backup created at: $KUBECONFIG_BACKUP"
else
    echo "Creating new kubeconfig at $KUBECONFIG_FILE"
fi

# Create the kubeconfig using kubectl
kubectl config set-cluster "$CLUSTER_NAME" \
    --server="$API_SERVER" \
    --insecure-skip-tls-verify=true \
    --kubeconfig="$KUBECONFIG_FILE" 2>/dev/null || true

kubectl config set-credentials "$USER_NAME" \
    --username=admin \
    --password=admin \
    --kubeconfig="$KUBECONFIG_FILE" 2>/dev/null || true

kubectl config set-context "$CONTEXT_NAME" \
    --cluster="$CLUSTER_NAME" \
    --user="$USER_NAME" \
    --namespace=default \
    --kubeconfig="$KUBECONFIG_FILE" 2>/dev/null || true

kubectl config use-context "$CONTEXT_NAME" \
    --kubeconfig="$KUBECONFIG_FILE" 2>/dev/null || true

# If kubectl is not available, create kubeconfig manually
if ! command -v kubectl &> /dev/null; then
    echo "kubectl not found, creating kubeconfig manually..."
    
    cat > "$KUBECONFIG_FILE" << 'EOF'
apiVersion: v1
kind: Config
clusters:
- cluster:
    insecure-skip-tls-verify: true
    server: https://localhost:6443
  name: sirah-local
contexts:
- context:
    cluster: sirah-local
    user: sirah-admin
    namespace: default
  name: sirah-local
current-context: sirah-local
preferences: {}
users:
- name: sirah-admin
  user:
    username: admin
    password: admin
EOF
fi

# Set proper permissions
chmod 600 "$KUBECONFIG_FILE"

echo -e "${GREEN}✓ Kubeconfig setup complete!${NC}"
echo ""
echo "Configuration Details:"
echo "  API Server: $API_SERVER"
echo "  Cluster Name: $CLUSTER_NAME"
echo "  Context: $CONTEXT_NAME"
echo "  User: $USER_NAME"
echo "  Kubeconfig: $KUBECONFIG_FILE"
echo ""
echo "Test the connection with:"
echo "  kubectl --kubeconfig=$KUBECONFIG_FILE get nodes"
echo "  kubectl --kubeconfig=$KUBECONFIG_FILE api-resources"
echo ""
echo "Or set KUBECONFIG environment variable:"
echo "  export KUBECONFIG=$KUBECONFIG_FILE"
echo "  kubectl get nodes"
