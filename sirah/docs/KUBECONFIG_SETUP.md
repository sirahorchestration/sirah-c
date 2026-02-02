# Kubeconfig Setup for Sirah

## Quick Setup

```bash
# 1. Start the API Server
cd sirah
./bin/sirah-apiserver &
sleep 2

# 2. Install kubeconfig
mkdir -p ~/.kube
cp config/kubeconfig ~/.kube/config
chmod 600 ~/.kube/config

# 3. Test the connection
kubectl cluster-info
kubectl api-resources
kubectl get nodes
```

## Configuration

The kubeconfig file points to:
- **API Server**: `http://localhost:6443`
- **Context**: `sirah-local`
- **Cluster**: `sirah-local`
- **User**: `sirah-admin` (admin/admin)

## Verification

```bash
# Check cluster info
kubectl cluster-info

# List available resources (should return all K8s resources)
kubectl api-resources

# List nodes
kubectl get nodes

# List all namespaces
kubectl get namespaces

# List pods in default namespace
kubectl get pods

# List deployments
kubectl get deployments

# List services
kubectl get services
```

## Environment Setup

### Option 1: Global Setup (Recommended)
```bash
# Copy to default kubeconfig location
mkdir -p ~/.kube
cp config/kubeconfig ~/.kube/config
chmod 600 ~/.kube/config

# Now kubectl will use it automatically
kubectl get nodes
```

### Option 2: Per-Session Setup
```bash
# Set for current shell session only
export KUBECONFIG=/path/to/sirah/config/kubeconfig
kubectl get nodes
```

### Option 3: Explicit Usage
```bash
# Specify kubeconfig for each command
kubectl --kubeconfig=config/kubeconfig get nodes
```

## Troubleshooting

### Connection Refused
```bash
# Check if API server is running
ps aux | grep sirah-apiserver

# Restart the API server
pkill -f sirah-apiserver
./bin/sirah-apiserver &
sleep 2

# Test connectivity
curl http://localhost:6443/api/v1
```

### Authentication Issues
```bash
# Check kubeconfig is correct
cat ~/.kube/config

# Verify credentials in the file:
# - server: https://localhost:6443
# - username: admin
# - password: admin
```

### Connection Issues
```bash
# The API server runs on HTTP (not HTTPS)
# Make sure kubeconfig uses: http://localhost:6443

# For now, TLS is not required for local development
# If you need HTTPS, the API server needs to be modified to support TLS
```

## Testing Different Resources

```bash
# Core API resources
kubectl api-resources

# Get pods
kubectl get pods -A

# Get deployments
kubectl get deployments -A

# Get jobs
kubectl get jobs -A

# Get cronjobs
kubectl get cronjobs -A

# Get services
kubectl get services -A

# Get configmaps
kubectl get configmaps -A

# Get secrets
kubectl get secrets -A
```

## Configuration Merge

If you already have a kubeconfig file, you can merge the Sirah context:

```bash
# Backup your existing kubeconfig
cp ~/.kube/config ~/.kube/config.backup

# Merge the Sirah kubeconfig
KUBECONFIG=~/.kube/config:config/kubeconfig kubectl config view --merge > /tmp/merged-config
mv /tmp/merged-config ~/.kube/config
chmod 600 ~/.kube/config

# Switch to Sirah context
kubectl config use-context sirah-local
```

## Create Resources with kubectl

```bash
# Create a pod
kubectl apply -f - <<EOF
apiVersion: v1
kind: Pod
metadata:
  name: test-pod
spec:
  containers:
  - name: nginx
    image: nginx:latest
    ports:
    - containerPort: 80
EOF

# Verify it was created
kubectl get pods

# Create a deployment
kubectl apply -f - <<EOF
apiVersion: apps/v1
kind: Deployment
metadata:
  name: test-deployment
spec:
  replicas: 3
  selector:
    matchLabels:
      app: test
  template:
    metadata:
      labels:
        app: test
    spec:
      containers:
      - name: nginx
        image: nginx:latest
        ports:
        - containerPort: 80
EOF

# Check deployment status
kubectl get deployments
kubectl get pods
```

## Notes

- The Sirah API server listens on `0.0.0.0:6443` using HTTP (not HTTPS)
- Kubeconfig uses `http://localhost:6443` for local access
- Authentication is basic auth (admin:admin)
- TLS is not configured for local development (can be added later)
- All resources are namespaced or cluster-scoped as per Kubernetes standards
