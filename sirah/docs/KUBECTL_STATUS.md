# kubectl Status with Sirah API Server

## Current Status

### ✅ Working Features
- `kubectl get pods` 
- `kubectl get nodes`
- `kubectl get services`
- `kubectl get configmaps`
- `kubectl api-resources`
- `kubectl cluster-info`
- All other GET operations

### ❌ Not Working
- `kubectl apply` - requires `/openapi/v2` endpoint
- `kubectl create` - requires `/openapi/v2` endpoint
- `kubectl delete` - requires `/openapi/v2` endpoint
- Write operations in general

## Why Write Operations Fail

kubectl performs a server health check before executing write commands:

1. **Step 1**: GET `/openapi/v2` (expects JSON Swagger spec)
2. **Step 2**: GET `/swagger-2.0.0.pb-v1` (fallback to Protobuf format)
3. **Step 3**: If both return 404 → **kubectl aborts all operations**

Our server currently doesn't implement these endpoints, so kubectl refuses to execute write operations.

## Quick Test

```bash
# Start the server
cd sirah
./bin/sirah-apiserver &

# These work:
kubectl get pods                    # ✅ Works
kubectl api-resources              # ✅ Works
kubectl cluster-info               # ✅ Works

# These fail:
kubectl apply -f pod.yaml          # ❌ Fails (OpenAPI 404)
kubectl create -f pod.yaml         # ❌ Fails (OpenAPI 404)
```

## Solutions

### Option 1: Use curl for Write Operations (Works Now)

```bash
curl -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H 'Content-Type: application/json' \
  -d '{
    "apiVersion":"v1",
    "kind":"Pod",
    "metadata":{"name":"my-pod"},
    "spec":{"containers":[{"name":"app","image":"nginx"}]}
  }'
```

### Option 2: Implement OpenAPI Endpoint (Todo)

To enable kubectl write operations, implement:
- `GET /openapi/v2` returning Swagger 2.0 JSON spec
- OR `GET /swagger-2.0.0.pb-v1` returning Protobuf-encoded spec

See [IMPLEMENTATION.md](../IMPLEMENTATION.md) for next steps.

## Configuration

Kubeconfig is set to use HTTP (not HTTPS):
- **Server**: `http://localhost:6443`
- **TLS**: Disabled (`insecure-skip-tls-verify: true`)
- **Auth**: Basic auth (admin/admin)

### Manual kubeconfig Setup

```bash
mkdir -p ~/.kube
cp config/kubeconfig ~/.kube/config
chmod 600 ~/.kube/config

# Test
kubectl cluster-info
```

## Known Issues

1. **GET endpoints work** - Read operations fully functional
2. **POST endpoints not tested** - Write operations blocked by OpenAPI validation
3. **OpenAPI endpoints missing** - kubectl won't attempt write operations without this
4. **No TLS** - Server runs on plain HTTP for local development

## Next Steps

1. Implement `/openapi/v2` endpoint with proper Swagger spec
2. OR implement `/swagger-2.0.0.pb-v1` with Protobuf encoding
3. Test full `kubectl apply` workflow
4. Add support for all write operations

## References

- [Kubernetes API Documentation](https://kubernetes.io/docs/concepts/overview/kubernetes-api/)
- [OpenAPI Specification](https://swagger.io/specification/v2-0/)
- kubeconfig location: [config/kubeconfig](../config/kubeconfig)
