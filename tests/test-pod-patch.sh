#!/bin/bash

# Test Pod CRUD operations and PATCH functionality

API_URL="http://localhost:6443"
NAMESPACE="default"
POD_NAME="patch-test-$(date +%s)"

echo "=== Pod CRUD and PATCH Test ==="
echo ""

# [1] Create a pod
echo "[1] Creating pod..."
POD_JSON=$(cat <<EOF
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "$POD_NAME",
    "namespace": "$NAMESPACE"
  },
  "spec": {
    "containers": [{
      "name": "app",
      "image": "/tmp/sirah-unikernels/test-kernel.img"
    }]
  }
}
EOF
)

CREATE_RESP=$(curl -s -X POST \
  "$API_URL/api/v1/namespaces/$NAMESPACE/pods" \
  -H "Content-Type: application/json" \
  -d "$POD_JSON")

echo "Response: $(echo "$CREATE_RESP" | jq -c .)"
POD_EXISTS=$(echo "$CREATE_RESP" | jq '.metadata.name' 2>/dev/null)
if [ "$POD_EXISTS" == "\"$POD_NAME\"" ]; then
  echo "✓ Pod created successfully"
else
  echo "✗ Pod creation failed"
  exit 1
fi
echo ""

# [2] Get the pod to see initial state
echo "[2] Getting pod (initial state)..."
GET_RESP=$(curl -s -X GET \
  "$API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_NAME" \
  -H "Content-Type: application/json")

INITIAL_PHASE=$(echo "$GET_RESP" | jq -r '.status.phase' 2>/dev/null)
echo "Initial phase: $INITIAL_PHASE"
echo "Full status: $(echo "$GET_RESP" | jq '.status' 2>/dev/null)"
echo ""

# [3] PATCH the pod status to Running
echo "[3] PATCHing pod status to Running..."
PATCH_BODY=$(cat <<EOF
{
  "status": {
    "phase": "Running"
  }
}
EOF
)

PATCH_RESP=$(curl -s -X PATCH \
  "$API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_NAME/status" \
  -H "Content-Type: application/json" \
  -d "$PATCH_BODY")

echo "PATCH Response: $(echo "$PATCH_RESP" | jq -c . 2>/dev/null || echo "$PATCH_RESP")"
echo ""

# [4] Get the pod again to verify status changed
echo "[4] Getting pod (after PATCH)..."
GET_RESP_AFTER=$(curl -s -X GET \
  "$API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_NAME" \
  -H "Content-Type: application/json")

AFTER_PHASE=$(echo "$GET_RESP_AFTER" | jq -r '.status.phase' 2>/dev/null)
echo "After PATCH phase: $AFTER_PHASE"
echo "Full status: $(echo "$GET_RESP_AFTER" | jq '.status' 2>/dev/null)"
echo ""

# [5] Verify the update worked
echo "[5] Verification:"
if [ "$AFTER_PHASE" == "Running" ]; then
  echo "✓ PATCH successfully updated pod status to Running"
else
  echo "✗ PATCH did not update status. Still in phase: $AFTER_PHASE"
  exit 1
fi
echo ""

# [6] PATCH to Succeeded
echo "[6] PATCHing pod status to Succeeded..."
PATCH_BODY2=$(cat <<EOF
{
  "status": {
    "phase": "Succeeded"
  }
}
EOF
)

PATCH_RESP2=$(curl -s -X PATCH \
  "$API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_NAME/status" \
  -H "Content-Type: application/json" \
  -d "$PATCH_BODY2")

echo "PATCH Response: $(echo "$PATCH_RESP2" | jq -c . 2>/dev/null || echo "$PATCH_RESP2")"
echo ""

# [7] Get and verify final state
echo "[7] Getting pod (final state)..."
GET_RESP_FINAL=$(curl -s -X GET \
  "$API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_NAME" \
  -H "Content-Type: application/json")

FINAL_PHASE=$(echo "$GET_RESP_FINAL" | jq -r '.status.phase' 2>/dev/null)
echo "Final phase: $FINAL_PHASE"
echo "Full status: $(echo "$GET_RESP_FINAL" | jq '.status' 2>/dev/null)"
echo ""

if [ "$FINAL_PHASE" == "Succeeded" ]; then
  echo "✓ Pod status successfully transitioned through states"
  echo ""
  echo "=== Test Summary ==="
  echo "Pod: $POD_NAME"
  echo "Transitions: Pending → Running → Succeeded"
  echo "Status: ✓ PASS"
else
  echo "✗ Final status not Succeeded. Got: $FINAL_PHASE"
  exit 1
fi

# [8] Cleanup
echo ""
echo "[8] Cleaning up..."
DELETE_RESP=$(curl -s -X DELETE \
  "$API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_NAME")
echo "Pod deleted"
