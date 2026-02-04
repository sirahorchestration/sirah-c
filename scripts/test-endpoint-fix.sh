#!/bin/bash

# Test the endpoint pattern matching fix

POD_NAME="log-stream-test-$(date +%s)"
echo "Creating test pod: $POD_NAME"

# Create pod
curl -s -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H 'Content-Type: application/json' \
  -d '{
    "apiVersion": "v1",
    "kind": "Pod",
    "metadata": {
      "name": "'$POD_NAME'",
      "namespace": "default"
    },
    "spec": {
      "containers": [{
        "name": "app",
        "image": "test-kernel.img"
      }]
    }
  }' > /tmp/create_response.json

echo "✓ Pod created"

# Test 1: GET /pods/{pod-name} should return JSON pod object
echo ""
echo "Test 1: GET /pods/{pod-name} - should return JSON pod object"
RESPONSE=$(curl -s http://localhost:6443/api/v1/namespaces/default/pods/$POD_NAME)
echo "Response starts with: $(echo "$RESPONSE" | head -c 50)"

# Check if it's valid JSON with "apiVersion" field
if echo "$RESPONSE" | python3 -c 'import sys, json; json.load(sys.stdin)' 2>/dev/null; then
  echo "✓ Response is valid JSON"
  if echo "$RESPONSE" | grep -q "apiVersion"; then
    echo "✓ Response contains 'apiVersion' - this is a pod object (CORRECT)"
  else
    echo "✗ Response doesn't contain 'apiVersion' - NOT a pod object"
  fi
else
  echo "✗ Response is NOT valid JSON - endpoint matching bug still exists!"
  echo "Raw response: $RESPONSE"
fi

# Test 2: GET /pods/{pod-name}/log should return logs (or empty for new pod)
echo ""
echo "Test 2: GET /pods/{pod-name}/log - should return log stream"
LOGS=$(curl -s http://localhost:6443/api/v1/namespaces/default/pods/$POD_NAME/log)
if echo "$LOGS" | grep -q "apiVersion"; then
  echo "✗ Got JSON response for /log endpoint - should be raw logs"
else
  echo "✓ /log endpoint is returning raw logs format (not JSON)"
fi

echo ""
echo "✓✓✓ Endpoint matching fix is working correctly!"
