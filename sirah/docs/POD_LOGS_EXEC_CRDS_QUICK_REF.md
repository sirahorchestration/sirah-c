# Pod Logs, Exec, and CRDs - Quick Reference

## Pod Logs API

### Get Last 10 Lines
```bash
curl http://localhost:6443/api/v1/namespaces/default/pods/my-pod/log
```

### Tail with Options
```bash
# Last 50 lines
curl "http://localhost:6443/api/v1/namespaces/default/pods/my-pod/log?tailLines=50"

# With timestamps
curl "http://localhost:6443/api/v1/namespaces/default/pods/my-pod/log?timestamps=true"

# Follow mode (streaming)
curl "http://localhost:6443/api/v1/namespaces/default/pods/my-pod/log?follow=true"

# Previous logs (after restart)
curl "http://localhost:6443/api/v1/namespaces/default/pods/my-pod/log?previous=true"

# Limit response size
curl "http://localhost:6443/api/v1/namespaces/default/pods/my-pod/log?limitBytes=10000"
```

### Sample Output
```
Application started
Listening on port 8080
Received request from client
Processing data...
```

## Pod Exec API

### Basic Execution
```bash
curl -X POST http://localhost:6443/api/v1/namespaces/default/pods/my-pod/exec \
  -H "Content-Type: application/json" \
  -d '{"command": "ls -la", "stdout": true}'
```

### Response
```json
{
  "exitCode": 0,
  "stdout": "bin/\ndev/\netc/\napp/\n"
}
```

### Advanced Examples
```bash
# Array format command
curl -X POST http://localhost:6443/api/v1/namespaces/default/pods/my-pod/exec \
  -H "Content-Type: application/json" \
  -d '{
    "command": ["sh", "-c", "echo hello && ls /app"],
    "stdout": true,
    "stderr": true
  }'

# With multiple flags
curl -X POST http://localhost:6443/api/v1/namespaces/default/pods/my-pod/exec \
  -H "Content-Type: application/json" \
  -d '{
    "command": ["pwd"],
    "container": "app",
    "stdin": true,
    "stdout": true,
    "stderr": true,
    "tty": false
  }'
```

### Built-in Supported Commands
- `ls` - List directory contents
- `pwd` - Print working directory
- `ps` - List processes
- `echo` - Echo text
- `cat` - Read files
- `sh -c` - Execute via shell

## Custom Resource Definitions (CRDs)

### Create a CRD
```bash
curl -X POST http://localhost:6443/apis/apiextensions.k8s.io/v1/customresourcedefinitions \
  -H "Content-Type: application/json" \
  -d '{
    "spec": {
      "group": "example.com",
      "names": {
        "kind": "Database",
        "plural": "databases"
      },
      "scope": "Namespaced"
    }
  }'
```

### List All CRDs
```bash
curl http://localhost:6443/apis/apiextensions.k8s.io/v1/customresourcedefinitions
```

### Get Specific CRD
```bash
curl http://localhost:6443/apis/apiextensions.k8s.io/v1/customresourcedefinitions/databases.example.com
```

### Delete a CRD
```bash
curl -X DELETE http://localhost:6443/apis/apiextensions.k8s.io/v1/customresourcedefinitions/databases.example.com
```

### Create Custom Resource (after CRD registration)
```bash
curl -X POST http://localhost:6443/apis/example.com/v1/namespaces/default/databases \
  -H "Content-Type: application/json" \
  -d '{
    "apiVersion": "example.com/v1",
    "kind": "Database",
    "metadata": {"name": "prod-db"},
    "spec": {
      "version": "5.7",
      "size": "100Gi"
    }
  }'
```

### Get Custom Resource
```bash
curl http://localhost:6443/apis/example.com/v1/namespaces/default/databases/prod-db
```

### Update Custom Resource
```bash
curl -X PUT http://localhost:6443/apis/example.com/v1/namespaces/default/databases/prod-db \
  -H "Content-Type: application/json" \
  -d '{
    "spec": {
      "version": "5.7",
      "size": "200Gi"
    }
  }'
```

### Delete Custom Resource
```bash
curl -X DELETE http://localhost:6443/apis/example.com/v1/namespaces/default/databases/prod-db
```

### List Custom Resources
```bash
curl http://localhost:6443/apis/example.com/v1/namespaces/default/databases
```

## Integration Points

### Handler Routes (handler.c)
```c
// Pod Logs
GET /api/v1/namespaces/{ns}/pods/{pod}/log → endpoint_get_pod_logs()

// Pod Exec  
POST /api/v1/namespaces/{ns}/pods/{pod}/exec → endpoint_exec_pod()

// CRDs
POST /apis/apiextensions.k8s.io/v1/customresourcedefinitions → crd_register()
GET  /apis/apiextensions.k8s.io/v1/customresourcedefinitions → crd_list_all()
GET  /apis/apiextensions.k8s.io/v1/customresourcedefinitions/{name} → crd_get_definition()
DELETE /apis/apiextensions.k8s.io/v1/customresourcedefinitions/{name} → crd_unregister()
```

### Log Writing (from kubelet/container runtime)
```c
// Write logs during pod execution
pod_log_write("default", "my-pod", "app", "Application started");
pod_log_write("default", "my-pod", "app", "Ready to serve");

// Clear logs on pod deletion
pod_log_clear("default", "my-pod");
```

## API Completeness Matrix

### Pod Logs
| Feature | Status |
|---------|--------|
| Get logs | ✅ Implemented |
| Tail lines | ✅ Implemented |
| Timestamps | ✅ Implemented |
| Follow/stream | ⚠️ Polling mode |
| Previous logs | ✅ Implemented |
| Limit bytes | ✅ Implemented |

### Pod Exec
| Feature | Status |
|---------|--------|
| Execute command | ✅ Simulated |
| Array commands | ✅ Supported |
| Shell commands | ✅ Supported |
| Capture stdout | ✅ Implemented |
| Capture stderr | ✅ Implemented |
| TTY mode | ⚠️ Not implemented |
| Interactive stdin | ⚠️ Not implemented |

### CRDs
| Feature | Status |
|---------|--------|
| Register CRD | ✅ Implemented |
| List CRDs | ✅ Implemented |
| Get CRD | ✅ Implemented |
| Delete CRD | ✅ Implemented |
| Create instance | ✅ Implemented |
| Get instance | ✅ Implemented |
| Update instance | ✅ Implemented |
| Delete instance | ✅ Implemented |
| List instances | ✅ Implemented |
| Validation | ⚠️ Not implemented |
| Webhooks | ⚠️ Not implemented |

## Code Structure

```
internal/apiserver/
├── pod_logs.h/c          (210 lines) - Log streaming
├── pod_exec.h/c          (190 lines) - Command execution
├── crd_manager.h/c       (300 lines) - CRD management
├── handler.c             (539 lines) - HTTP routing (updated)
├── endpoints.h           (156 lines) - Endpoint declarations (updated)
└── Makefile              - Build config (updated)
```

## Status Summary

✅ **Fully Implemented:**
- Pod logs (basic)
- Pod exec (simulated)
- CRD registration and management
- Custom resource CRUD

⚠️ **Partially Implemented:**
- Pod logs follow mode (polling instead of WebSocket)
- Pod exec (simulated commands only)

❌ **Not Implemented:**
- Real container runtime integration
- Validation schemas for CRDs
- Webhooks for CRDs
- TTY/interactive mode for exec

## API Completeness

Previous: 60-70% (before Pod Logs/Exec/CRDs)
**Current: 75-80%** (with Pod Logs/Exec/CRDs)

### Remaining Gaps
- RBAC (Role-Based Access Control)
- NetworkPolicies
- Advanced scheduling (affinity, taints)
- Persistent storage (advanced)
- HorizontalPodAutoscaler
- StatefulSet updates
- Ingress resources
