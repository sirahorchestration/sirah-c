#!/bin/bash
# tests/integration/test_pod_lifecycle.sh
# Integration test: Pod creation, listing, and deletion

set -e

API_URL="http://localhost:6443"
NAMESPACE="default"
POD_NAME="integration-test-pod"

echo "=========================================="
echo "Integration Test: Pod Lifecycle"
echo "=========================================="

# Test 1: Create pod
echo ""
echo "[1] Testing pod creation..."
POD_JSON="{
  \"apiVersion\": \"v1\",
  \"kind\": \"Pod\",
  \"metadata\": {
    \"name\": \"$POD_NAME\",
    \"namespace\": \"$NAMESPACE\"
  },
  \"spec\": {
    \"containers\": [
      {
        \"name\": \"test-container\",
        \"image\": \"busybox:latest\"
      }
    ]
  }
}"

RESPONSE=$(curl -s -X POST $API_URL/api/v1/namespaces/$NAMESPACE/pods \
  -H 'Content-Type: application/json' \
  -d "$POD_JSON")

POD_NAME_RESPONSE=$(echo "$RESPONSE" | python3 -c "import sys, json; print(json.load(sys.stdin)['metadata']['name'])" 2>/dev/null)

if [ "$POD_NAME_RESPONSE" = "$POD_NAME" ]; then
    echo "✓ PASS: Pod created with correct name: $POD_NAME"
else
    echo "✗ FAIL: Pod name mismatch. Expected: $POD_NAME, Got: $POD_NAME_RESPONSE"
    exit 1
fi

# Test 2: List pods
echo ""
echo "[2] Testing pod listing..."
PODS_RESPONSE=$(curl -s -X GET $API_URL/api/v1/namespaces/$NAMESPACE/pods)

POD_COUNT=$(echo "$PODS_RESPONSE" | python3 -c "import sys, json; items = json.load(sys.stdin).get('items', []); print(len(items))" 2>/dev/null)

if [ "$POD_COUNT" -gt 0 ]; then
    echo "✓ PASS: Found $POD_COUNT pod(s)"
else
    echo "✗ FAIL: No pods found in listing"
    exit 1
fi

# Test 3: Verify pod is in list
echo ""
echo "[3] Verifying pod in list..."
POD_IN_LIST=$(echo "$PODS_RESPONSE" | python3 -c "
import sys, json
pods = json.load(sys.stdin).get('items', [])
for pod in pods:
    if pod['name'] == '$POD_NAME':
        print('found')
        break
else:
    print('notfound')
" 2>/dev/null)

if [ "$POD_IN_LIST" = "found" ]; then
    echo "✓ PASS: Pod $POD_NAME found in pod list"
else
    echo "✗ FAIL: Pod $POD_NAME not found in list"
    exit 1
fi

# Test 4: Get single pod
echo ""
echo "[4] Testing get single pod..."
SINGLE_POD=$(curl -s -X GET $API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_NAME)
SINGLE_POD_NAME=$(echo "$SINGLE_POD" | python3 -c "import sys, json; print(json.load(sys.stdin)['name'])" 2>/dev/null)

if [ "$SINGLE_POD_NAME" = "$POD_NAME" ]; then
    echo "✓ PASS: Retrieved single pod: $POD_NAME"
else
    echo "✗ FAIL: Single pod retrieval failed"
    exit 1
fi

# Test 5: Delete pod
echo ""
echo "[5] Testing pod deletion..."
DELETE_RESPONSE=$(curl -s -X DELETE $API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_NAME)

PODS_AFTER_DELETE=$(curl -s -X GET $API_URL/api/v1/namespaces/$NAMESPACE/pods)
POD_AFTER_DELETE=$(echo "$PODS_AFTER_DELETE" | python3 -c "
import sys, json
pods = json.load(sys.stdin).get('items', [])
for pod in pods:
    if pod['name'] == '$POD_NAME':
        print('found')
        break
else:
    print('notfound')
" 2>/dev/null)

if [ "$POD_AFTER_DELETE" = "notfound" ]; then
    echo "✓ PASS: Pod successfully deleted"
else
    echo "✗ FAIL: Pod still exists after deletion"
    exit 1
fi

echo ""
echo "=========================================="
echo "All pod lifecycle tests passed!"
echo "=========================================="
exit 0
