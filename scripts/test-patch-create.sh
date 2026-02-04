#!/bin/bash

POD_NAME="test-patch-pod"
POD_NS="default"
API="http://localhost:6443"

echo "Creating test pod..."
curl -s -X POST \
  -H "Content-Type: application/json" \
  -d '{
    "apiVersion": "v1",
    "kind": "Pod",
    "metadata": {
      "name": "'$POD_NAME'",
      "namespace": "'$POD_NS'"
    },
    "spec": {
      "containers": [
        {
          "name": "app",
          "image": "/tmp/sirah-unikernels/test-kernel.img",
          "resources": {
            "limits": {
              "memory": "128Mi",
              "cpu": "1"
            }
          }
        }
      ]
    }
  }' \
  "$API/api/v1/namespaces/$POD_NS/pods"  | jq '.metadata.name, .status.phase'

echo ""
echo "Waiting 2 seconds for pod to be available..."
sleep 2

echo ""
echo "Testing PATCH endpoint..."
echo "  Method: PATCH"
echo "  URL: $API/api/v1/namespaces/$POD_NS/pods/$POD_NAME/status"
echo "  Body: {\"status\":{\"phase\":\"Running\"}}"
echo ""

echo "Response:"
curl -s -X PATCH \
  -H "Content-Type: application/json" \
  -d '{"status":{"phase":"Running"}}' \
  "$API/api/v1/namespaces/$POD_NS/pods/$POD_NAME/status" | jq .

echo ""
echo "Getting pod to verify status:"
curl -s "$API/api/v1/namespaces/$POD_NS/pods/$POD_NAME" | jq '.status | {phase, containerStatuses: [.containerStatuses[].state]}'

echo ""
echo "Cleanup:"
curl -s -X DELETE "$API/api/v1/namespaces/$POD_NS/pods/$POD_NAME"
