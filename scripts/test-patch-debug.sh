#!/bin/bash

POD_NAME="qemu-test-1769992727691098965"
POD_NS="default"
API="http://localhost:6443"

echo "Testing PATCH endpoint..."
echo ""

# Test the PATCH request
echo "Request:"
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
curl -s "$API/api/v1/namespaces/$POD_NS/pods/$POD_NAME" | jq '.status.phase'
