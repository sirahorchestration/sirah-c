#!/bin/bash
# Test pod status JSON structure

cat > /tmp/test_pod_status.json << 'EOF'
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "test-pod",
    "namespace": "default"
  },
  "status": {
    "phase": "Pending",
    "containerStatuses": [
      {
        "name": "app",
        "ready": false,
        "restartCount": 0,
        "state": {
          "waiting": {
            "reason": "ContainerCreating"
          }
        }
      }
    ]
  }
}
EOF

echo "=== Original Issue (Missing reason field) ==="
echo "Before fix, the state was:"
echo '  "state": { "waiting": null }'
echo ""

echo "=== After Fix (With reason field) ==="
echo "Now the state is:"
jq '.status.containerStatuses[0].state' /tmp/test_pod_status.json
echo ""

echo "=== Expected Kubernetes Structure ==="
echo "Waiting state with reason:"
jq '.status.containerStatuses[0].state' /tmp/test_pod_status.json
echo ""

echo "=== Running state structure ==="
cat > /tmp/test_pod_running.json << 'EOF'
{
  "status": {
    "containerStatuses": [
      {
        "name": "app",
        "ready": true,
        "state": {
          "running": {}
        }
      }
    ]
  }
}
EOF

jq '.status.containerStatuses[0].state' /tmp/test_pod_running.json
