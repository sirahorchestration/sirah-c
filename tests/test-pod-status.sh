#!/bin/bash

# Create pod JSON
cat > /tmp/test-pod.json << 'JSONEOF'
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "test-status-update",
    "namespace": "default"
  },
  "spec": {
    "containers": [{
      "name": "app",
      "image": "/tmp/sirah-unikernels/test-kernel.img",
      "resources": {
        "limits": {"memory": "256Mi", "cpu": "1"}
      }
    }]
  }
}
JSONEOF

echo "[TEST] Creating pod..."
curl -s -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H 'Content-Type: application/json' \
  -d @/tmp/test-pod.json | jq '.metadata.name'

echo ""
echo "[TEST] Status at T+0 (should be Pending):"
curl -s http://localhost:6443/api/v1/namespaces/default/pods/test-status-update | jq '.status.phase'

echo ""
echo "[TEST] Waiting 5 seconds for controller to spawn QEMU and update status..."
sleep 5

echo ""
echo "[TEST] Status at T+5 (should be Running now):"
curl -s http://localhost:6443/api/v1/namespaces/default/pods/test-status-update | jq '.status.phase'

echo ""
echo "[TEST] Full status response:"
curl -s http://localhost:6443/api/v1/namespaces/default/pods/test-status-update | jq '.status'
