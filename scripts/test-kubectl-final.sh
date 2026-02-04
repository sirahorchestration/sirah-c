#!/bin/bash
set -e

POD="test-kubectl-$(date +%s)"

echo "Creating pod: $POD"
curl -s -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H "Content-Type: application/json" \
  -u admin:admin \
  -d '{
    "apiVersion": "v1",
    "kind": "Pod",
    "metadata": {
      "name": "'$POD'",
      "namespace": "default"
    },
    "spec": {
      "containers": [{
        "name": "app",
        "image": "/tmp/sirah-unikernels/test-kernel.img",
        "resources": {
          "limits": {
            "memory": "128Mi",
            "cpu": "1"
          }
        }
      }]
    }
  }' > /dev/null

echo "Waiting for logs to be written..."
sleep 5

echo ""
echo "=== Testing kubectl logs ==="
echo "Pod name: $POD"
echo ""
kubectl logs "$POD" -n default 2>&1 | head -20
