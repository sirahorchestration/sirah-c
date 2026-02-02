#!/bin/bash

echo "=========================================="
echo "kubectl logs Test - Full End-to-End"
echo "=========================================="
echo ""

# Create pod with timestamp
TIMESTAMP=$(date +%s)
POD_NAME="log-stream-test-${TIMESTAMP}"

echo "[1/4] Creating pod: $POD_NAME"
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
  }' > /tmp/pod_create.json

if grep -q "apiVersion" /tmp/pod_create.json; then
  echo "✓ Pod created successfully"
else
  echo "✗ Failed to create pod"
  exit 1
fi

echo ""
echo "[2/4] Waiting 60 seconds for pod to initialize and generate logs..."
for i in {60..1}; do
  printf "\r  Remaining: %2d seconds" $i
  sleep 1
done
echo ""
echo "✓ Wait complete"

echo ""
echo "[3/4] Testing kubectl logs command..."
echo "  Command: kubectl logs $POD_NAME"
echo ""

LOGS=$(kubectl logs $POD_NAME 2>&1)
KUBECTL_EXIT=$?

echo "=========================================="
echo "RESULTS:"
echo "=========================================="
echo ""
echo "kubectl exit code: $KUBECTL_EXIT"
echo ""

if [ $KUBECTL_EXIT -eq 0 ]; then
  echo "✓✓✓ kubectl logs SUCCESS!"
  echo ""
  echo "Logs output:"
  echo "---"
  if [ -z "$LOGS" ]; then
    echo "(empty - pod may not have produced output yet)"
  else
    echo "$LOGS" | head -20
    if [ $(echo "$LOGS" | wc -l) -gt 20 ]; then
      echo "... (truncated, $(echo "$LOGS" | wc -l) total lines)"
    fi
  fi
  echo "---"
else
  echo "✗ kubectl logs FAILED with exit code $KUBECTL_EXIT"
  echo ""
  echo "Error output:"
  echo "---"
  echo "$LOGS"
  echo "---"
fi

echo ""
echo "[4/4] Verification checks..."
echo ""

# Check 1: Pod exists
POD_CHECK=$(curl -s http://localhost:6443/api/v1/namespaces/default/pods/$POD_NAME | python3 -c 'import sys, json; data=json.load(sys.stdin); print(data.get("metadata", {}).get("name", ""))' 2>/dev/null)
if [ "$POD_CHECK" = "$POD_NAME" ]; then
  echo "✓ Pod exists in API"
else
  echo "✗ Pod not found in API"
fi

# Check 2: Pod endpoint returns JSON
POD_RESPONSE=$(curl -s http://localhost:6443/api/v1/namespaces/default/pods/$POD_NAME | head -c 50)
if echo "$POD_RESPONSE" | grep -q "apiVersion"; then
  echo "✓ Pod GET endpoint returns JSON"
else
  echo "✗ Pod GET endpoint not returning JSON"
fi

# Check 3: Logs endpoint exists
LOGS_CHECK=$(curl -s -w "\n%{http_code}" http://localhost:6443/api/v1/namespaces/default/pods/$POD_NAME/log | tail -1)
if [ "$LOGS_CHECK" = "200" ]; then
  echo "✓ Logs endpoint responds with 200"
else
  echo "✗ Logs endpoint returned HTTP $LOGS_CHECK"
fi

echo ""
echo "=========================================="
echo "Test Complete"
echo "=========================================="
echo ""
echo "Summary:"
echo "  Pod created:        $POD_NAME"
echo "  kubectl logs exit:  $KUBECTL_EXIT"
echo "  Status:             $([ $KUBECTL_EXIT -eq 0 ] && echo 'SUCCESS ✓' || echo 'FAILED ✗')"
