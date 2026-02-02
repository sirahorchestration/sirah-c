# API Completeness - Quick Reference Guide

## Quick Start Examples

### PATCH Operations

#### Strategic Merge Patch (Recommended)
```bash
# Update labels on a pod
curl -X PATCH http://localhost:6443/api/v1/namespaces/default/pods/my-pod \
  -H 'Content-Type: application/merge-patch+json' \
  -d '{"metadata":{"labels":{"env":"prod"}}}'

# Scale a deployment
curl -X PATCH http://localhost:6443/apis/apps/v1/namespaces/default/deployments/my-app \
  -H 'Content-Type: application/merge-patch+json' \
  -d '{"spec":{"replicas":5}}'

# Delete a label
curl -X PATCH http://localhost:6443/api/v1/namespaces/default/pods/my-pod \
  -H 'Content-Type: application/merge-patch+json' \
  -d '{"metadata":{"labels":{"temp":null}}}'
```

#### JSON Patch (RFC 6902)
```bash
# Add annotation
curl -X PATCH http://localhost:6443/api/v1/namespaces/default/pods/my-pod \
  -H 'Content-Type: application/json-patch+json' \
  -d '[{"op":"add","path":"/metadata/annotations/owner","value":"alice"}]'

# Replace image
curl -X PATCH http://localhost:6443/apis/apps/v1/namespaces/default/deployments/my-app \
  -H 'Content-Type: application/json-patch+json' \
  -d '[{"op":"replace","path":"/spec/template/spec/containers/0/image","value":"myapp:v2"}]'

# Remove field
curl -X PATCH http://localhost:6443/api/v1/namespaces/default/configmaps/my-config \
  -H 'Content-Type: application/json-patch+json' \
  -d '[{"op":"remove","path":"/data/temp_key"}]'
```

### Watch API

```bash
# Watch pods in real-time
curl http://localhost:6443/api/v1/namespaces/default/pods?watch=true

# Watch with label filter
curl 'http://localhost:6443/api/v1/namespaces/default/pods?watch=true&labelSelector=app=web'

# Watch deployments
curl 'http://localhost:6443/apis/apps/v1/namespaces/default/deployments?watch=true'

# Watch with timeout
curl 'http://localhost:6443/api/v1/namespaces/default/pods?watch=true&timeoutSeconds=60'
```

Watch output format:
```json
{"type":"ADDED","object":{"apiVersion":"v1","kind":"Pod","metadata":{"name":"pod-1"}}}
{"type":"MODIFIED","object":{"apiVersion":"v1","kind":"Pod","metadata":{"name":"pod-1"}}}
{"type":"DELETED","object":{"apiVersion":"v1","kind":"Pod","metadata":{"name":"pod-1"}}}
```

### List with Filtering

```bash
# Filter by label
curl 'http://localhost:6443/api/v1/namespaces/default/pods?labelSelector=app=web'

# Filter by multiple labels
curl 'http://localhost:6443/api/v1/namespaces/default/pods?labelSelector=app=web,env=prod'

# Exclude with !=
curl 'http://localhost:6443/api/v1/namespaces/default/pods?labelSelector=env!=dev'

# Filter by field
curl 'http://localhost:6443/api/v1/namespaces/default/pods?fieldSelector=status.phase=Running'

# Combine label and field filters
curl 'http://localhost:6443/api/v1/namespaces/default/pods?labelSelector=app=web&fieldSelector=status.phase=Running'

# Pagination
curl 'http://localhost:6443/api/v1/namespaces/default/pods?limit=10'
curl 'http://localhost:6443/api/v1/namespaces/default/pods?limit=10&continue=10'
```

### CRUD Operations

```bash
# Create deployment
POST /apis/apps/v1/namespaces/default/deployments
{
  "apiVersion":"apps/v1",
  "kind":"Deployment",
  "metadata":{"name":"my-app"},
  "spec":{"replicas":3,"selector":{"matchLabels":{"app":"my-app"}}}
}

# Read deployment
GET /apis/apps/v1/namespaces/default/deployments/my-app

# List deployments
GET /apis/apps/v1/namespaces/default/deployments

# Update deployment (replace)
PUT /apis/apps/v1/namespaces/default/deployments/my-app
{full deployment spec}

# Patch deployment
PATCH /apis/apps/v1/namespaces/default/deployments/my-app
{"spec":{"replicas":5}}

# Delete deployment
DELETE /apis/apps/v1/namespaces/default/deployments/my-app
```

## File Reference

| Component | File | Lines | Purpose |
|-----------|------|-------|---------|
| Patching | `patch_handler.c` | 200 | Strategic Merge + JSON Patch |
| Watching | `watch.c` | 250 | Real-time event streaming |
| Filtering | `query_parser.c` | 180 | Label/field selector parsing |
| Routing | `handler.c` | 500+ | HTTP method routing |
| Endpoints | `endpoints.c` | 1300+ | Resource CRUD implementations |
| Tests | `test-api-completeness.sh` | 100 | Comprehensive test suite |
| Docs | `API_COMPLETENESS.md` | 400 | Full documentation |

## HTTP Methods Supported

| Resource | GET | POST | PUT | PATCH | DELETE |
|----------|-----|------|-----|-------|--------|
| Pod | ✅ | ✅ | ❌ | ✅ | ✅ |
| Service | ✅ | ✅ | ❌ | ✅ | ✅ |
| Deployment | ✅ | ✅ | ✅ | ✅ | ✅ |
| DaemonSet | ✅ | ✅ | ✅ | ✅ | ✅ |
| Job | ✅ | ✅ | ✅ | ✅ | ✅ |
| CronJob | ✅ | ✅ | ✅ | ✅ | ✅ |
| Namespace | ✅ | ✅ | ❌ | ❌ | ✅ |
| Event | ✅ | ✅ | ❌ | ❌ | ❌ |

## Query Parameters

| Parameter | Example | Applies To |
|-----------|---------|-----------|
| `labelSelector` | `app=web` | All list operations |
| `fieldSelector` | `status.phase=Running` | All list operations |
| `limit` | `50` | All list operations |
| `continue` | `token` | All list operations |
| `watch` | `true` | GET operations |
| `timeoutSeconds` | `30` | Watch operations |
| `allowWatchBookmarks` | `true` | Watch operations |

## Content-Type Headers

| Operation | Content-Type | Example |
|-----------|--------------|---------|
| JSON Patch | `application/json-patch+json` | `[{"op":"add"...}]` |
| Merge Patch | `application/merge-patch+json` | `{"spec":{"replicas":3}}` |
| Standard JSON | `application/json` | `{full object}` |

## Error Codes

| Code | Meaning | Example |
|------|---------|---------|
| 200 | OK | GET, PATCH, PUT succeeded |
| 201 | Created | POST succeeded |
| 204 | No Content | DELETE succeeded |
| 400 | Bad Request | Invalid JSON/parameters |
| 404 | Not Found | Resource doesn't exist |
| 500 | Server Error | Internal error |

## Building

```bash
# Build with new components
cd sirah
make clean
make

# Run API server
./bin/sirah-apiserver --port 6443

# Run tests
bash test-api-completeness.sh
```

## Performance Tips

1. **Use Watch instead of polling**
   - Watch is real-time and efficient
   - Polling: `curl http://localhost:6443/api/v1/pods` (repeated)
   - Watch: `curl http://localhost:6443/api/v1/pods?watch=true`

2. **Use Field Selectors for simple filtering**
   - Fast: `fieldSelector=metadata.name=my-pod`
   - Slower: `labelSelector=...` (more complex matching)

3. **Limit results with pagination**
   - Don't fetch all resources: `limit=10`
   - Use continue tokens for large datasets

4. **Combine filters efficiently**
   - One labelSelector: `app=web`
   - Multiple: `app=web,env=prod` (comma-separated)
   - Don't over-filter on client side

## Debugging

### Check if Watch is working
```bash
curl -v 'http://localhost:6443/api/v1/namespaces/default/pods?watch=true'
# Should return streaming responses
```

### Test PATCH
```bash
curl -X PATCH http://localhost:6443/api/v1/namespaces/default/pods/my-pod \
  -H 'Content-Type: application/merge-patch+json' \
  -d '{"metadata":{"labels":{"test":"true"}}}' -v
# Check response code (should be 200)
```

### Test filtering
```bash
curl 'http://localhost:6443/api/v1/namespaces/default/pods?labelSelector=app=web' -v
# Check items count in response
```

## Common Errors

**Error: "invalid JSON"**
- Solution: Verify JSON syntax with `jq` or online validator

**Error: "pod not found"**
- Solution: Check pod name and namespace
- Verify with: `GET /api/v1/namespaces/default/pods`

**Error: "invalid patch"**
- Solution: Check patch format matches header:
  - `application/merge-patch+json`: object format
  - `application/json-patch+json`: array of operations

**Watch returns nothing**
- Solution: Stream reads character-by-character, use proper HTTP client
- Test with: `curl ... | jq -s .` (collect all events)

## Integration Examples

### With kubectl
```bash
# These all use our API endpoints
kubectl get pods --watch
kubectl patch pod my-pod -p '{"metadata":{"labels":{"env":"prod"}}}'
kubectl get pods -l app=web
kubectl get pods --field-selector status.phase=Running
```

### With client-go (Go)
```go
import "k8s.io/client-go/kubernetes"

clientset, _ := kubernetes.NewForConfig(config)
// All CRUD operations now support PATCH
pods := clientset.CoreV1().Pods("default")
patches := []byte(`{"metadata":{"labels":{"env":"prod"}}}`)
pod, _ := pods.Patch(ctx, "my-pod", types.StrategicMergePatchType, patches, metav1.PatchOptions{})
```

### With Python client
```python
from kubernetes import client, config

config.load_incluster_config()
v1 = client.CoreV1Api()

# Watch pods
for event in v1.list_namespaced_pod("default", watch=True):
    print(event['type'], event['object'].metadata.name)

# Patch pod
body = {"metadata": {"labels": {"env": "prod"}}}
v1.patch_namespaced_pod("my-pod", "default", body)
```

## Useful Links

- [Kubernetes API Documentation](https://kubernetes.io/docs/reference/generated/kubernetes-api/)
- [JSON Patch RFC 6902](https://tools.ietf.org/html/rfc6902)
- [Kubernetes Strategic Merge Patch](https://kubernetes.io/docs/tasks/manage-kubernetes-objects/declarative-config/)
- [kubectl Patch Guide](https://kubernetes.io/docs/tasks/run-application/rolling-updates-with-resource-quotas/)
