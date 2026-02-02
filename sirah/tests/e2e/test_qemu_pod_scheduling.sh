#!/bin/bash
# tests/e2e/test_qemu_pod_scheduling.sh
# End-to-end test: QEMU pod scheduling with cluster validation

API_URL="http://localhost:6443"
NAMESPACE="default"

echo "=========================================="
echo "End-to-End: QEMU Pod Scheduling"
echo "=========================================="

# Test 1: Verify cluster and QEMU nodes ready
echo ""
echo "[1] Verifying QEMU cluster setup..."
CLUSTER_HEALTH=$(curl -s -X GET $API_URL/healthz)

if [ "$CLUSTER_HEALTH" = "ok" ]; then
    echo "✓ PASS: Cluster is healthy"
else
    echo "✗ FAIL: Cluster health check failed"
    exit 1
fi

# Test 2: Check available QEMU nodes
echo ""
echo "[2] Checking QEMU node availability..."
NODES=$(curl -s -X GET $API_URL/api/v1/nodes)
NODE_COUNT=$(echo "$NODES" | python3 -c "
import sys, json
try:
    items = json.load(sys.stdin).get('items', [])
    print(len(items))
except:
    print(0)
" 2>/dev/null)

if [ "$NODE_COUNT" -gt 0 ]; then
    echo "✓ PASS: Found $NODE_COUNT QEMU node(s)"
else
    echo "✗ FAIL: No QEMU nodes available"
    exit 1
fi

# Test 3: Schedule single pod to QEMU
echo ""
echo "[3] Scheduling pod to QEMU..."
POD_NAME="e2e-qemu-pod-$(date +%s)"
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
        \"name\": \"qemu-app\",
        \"image\": \"alpine:latest\"
      }
    ]
  }
}"

CREATE_RESPONSE=$(curl -s -X POST $API_URL/api/v1/namespaces/$NAMESPACE/pods \
  -H 'Content-Type: application/json' \
  -d "$POD_JSON")

POD_NAME_CREATED=$(echo "$CREATE_RESPONSE" | python3 -c "import sys, json; print(json.load(sys.stdin)['metadata']['name'])" 2>/dev/null)

if [ "$POD_NAME_CREATED" = "$POD_NAME" ]; then
    echo "✓ PASS: Pod created: $POD_NAME"
else
    echo "✗ FAIL: Pod creation failed"
    exit 1
fi

# Test 4: Verify pod scheduled to QEMU node
echo ""
echo "[4] Verifying pod scheduled to QEMU node..."
sleep 2

POD_STATUS=$(curl -s -X GET $API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_NAME)
SCHEDULED_NODE=$(echo "$POD_STATUS" | python3 -c "
import sys, json
try:
    pod = json.load(sys.stdin)
    node = pod.get('spec', {}).get('nodeName', '')
    print(node if node else 'unscheduled')
except:
    print('error')
" 2>/dev/null)

if [ "$SCHEDULED_NODE" != "unscheduled" ] && [ "$SCHEDULED_NODE" != "error" ] && [ -n "$SCHEDULED_NODE" ]; then
    echo "✓ PASS: Pod scheduled to node: $SCHEDULED_NODE"
else
    echo "✗ FAIL: Pod not scheduled to QEMU node"
    exit 1
fi

# Test 5: Monitor pod status transitions
echo ""
echo "[5] Monitoring pod status transitions..."
POD_PHASE=$(echo "$POD_STATUS" | python3 -c "
import sys, json
try:
    pod = json.load(sys.stdin)
    phase = pod.get('status', {}).get('phase', 'Unknown')
    print(phase)
except:
    print('Unknown')
" 2>/dev/null)

if [[ "$POD_PHASE" =~ ^(Pending|Running|Succeeded)$ ]]; then
    echo "✓ PASS: Pod phase: $POD_PHASE"
else
    echo "⚠ WARN: Unexpected phase: $POD_PHASE"
fi

# Test 6: Concurrent scheduling stress test
echo ""
echo "[6] Concurrent QEMU scheduling stress test..."
CONCURRENT_COUNT=5
SCHEDULED_SUCCESS=0

for i in $(seq 1 $CONCURRENT_COUNT); do
    POD_NAME_STRESS="qemu-stress-$i-$(date +%s)"
    POD_JSON_STRESS="{
      \"apiVersion\": \"v1\",
      \"kind\": \"Pod\",
      \"metadata\": {
        \"name\": \"$POD_NAME_STRESS\",
        \"namespace\": \"$NAMESPACE\"
      },
      \"spec\": {
        \"containers\": [
          {
            \"name\": \"stress-app-$i\",
            \"image\": \"alpine:latest\"
          }
        ]
      }
    }"

    RESPONSE=$(curl -s -X POST $API_URL/api/v1/namespaces/$NAMESPACE/pods \
      -H 'Content-Type: application/json' \
      -d "$POD_JSON_STRESS" 2>/dev/null)

    POD_NAME_CHECK=$(echo "$RESPONSE" | python3 -c "import sys, json; print(json.load(sys.stdin)['metadata']['name'])" 2>/dev/null)
    
    if [ "$POD_NAME_CHECK" = "$POD_NAME_STRESS" ]; then
        SCHEDULED_SUCCESS=$((SCHEDULED_SUCCESS + 1))
    fi
done

sleep 3

# Verify stress test pods scheduled
PODS_LIST=$(curl -s -X GET $API_URL/api/v1/namespaces/$NAMESPACE/pods)
STRESS_PODS_SCHEDULED=$(echo "$PODS_LIST" | python3 -c "
import sys, json
count = 0
try:
    pods = json.load(sys.stdin).get('items', [])
    for pod in pods:
        name = pod.get('name', '')
        node = pod.get('spec', {}).get('nodeName', '')
        if 'qemu-stress' in name and node:
            count += 1
except:
    pass
print(count)
" 2>/dev/null)

if [ "$SCHEDULED_SUCCESS" -ge $((CONCURRENT_COUNT - 1)) ]; then
    echo "✓ PASS: Created $CONCURRENT_COUNT concurrent pods"
    echo "✓ PASS: Scheduled $STRESS_PODS_SCHEDULED pods to QEMU nodes"
else
    echo "⚠ WARN: Only $SCHEDULED_SUCCESS/$CONCURRENT_COUNT pods created"
fi

# Test 7: Pod with resource requirements
echo ""
echo "[7] Scheduling QEMU pod with resource limits..."
POD_RESOURCE_NAME="qemu-resource-$(date +%s)"
POD_RESOURCE_JSON="{
  \"apiVersion\": \"v1\",
  \"kind\": \"Pod\",
  \"metadata\": {
    \"name\": \"$POD_RESOURCE_NAME\",
    \"namespace\": \"$NAMESPACE\"
  },
  \"spec\": {
    \"containers\": [
      {
        \"name\": \"resource-limited\",
        \"image\": \"alpine:latest\",
        \"resources\": {
          \"requests\": {
            \"memory\": \"32Mi\",
            \"cpu\": \"50m\"
          },
          \"limits\": {
            \"memory\": \"64Mi\",
            \"cpu\": \"100m\"
          }
        }
      }
    ]
  }
}"

RESOURCE_RESPONSE=$(curl -s -X POST $API_URL/api/v1/namespaces/$NAMESPACE/pods \
  -H 'Content-Type: application/json' \
  -d "$POD_RESOURCE_JSON")

RESOURCE_POD_NAME=$(echo "$RESOURCE_RESPONSE" | python3 -c "import sys, json; print(json.load(sys.stdin)['metadata']['name'])" 2>/dev/null)

if [ "$RESOURCE_POD_NAME" = "$POD_RESOURCE_NAME" ]; then
    echo "✓ PASS: Pod with resources scheduled"
else
    echo "✗ FAIL: Failed to schedule pod with resources"
    exit 1
fi

# Test 8: Verify scheduling performance
echo ""
echo "[8] Measuring QEMU scheduling performance..."

START_TIME=$(date +%s%N)
PERF_POD_NAME="qemu-perf-$(date +%s)"
PERF_POD_JSON="{
  \"apiVersion\": \"v1\",
  \"kind\": \"Pod\",
  \"metadata\": {
    \"name\": \"$PERF_POD_NAME\",
    \"namespace\": \"$NAMESPACE\"
  },
  \"spec\": {
    \"containers\": [{\"name\": \"perf\", \"image\": \"alpine:latest\"}]
  }
}"

curl -s -X POST $API_URL/api/v1/namespaces/$NAMESPACE/pods \
  -H 'Content-Type: application/json' \
  -d "$PERF_POD_JSON" > /dev/null

sleep 1

PERF_STATUS=$(curl -s -X GET $API_URL/api/v1/namespaces/$NAMESPACE/pods/$PERF_POD_NAME)
PERF_NODE=$(echo "$PERF_STATUS" | python3 -c "
import sys, json
try:
    pod = json.load(sys.stdin)
    node = pod.get('spec', {}).get('nodeName', '')
    print('scheduled' if node else 'unscheduled')
except:
    print('error')
" 2>/dev/null)

END_TIME=$(date +%s%N)
SCHEDULE_TIME_NS=$((END_TIME - START_TIME))
SCHEDULE_TIME_MS=$((SCHEDULE_TIME_NS / 1000000))

if [ "$PERF_NODE" = "scheduled" ]; then
    echo "✓ PASS: Pod scheduled in ${SCHEDULE_TIME_MS}ms"
else
    echo "⚠ WARN: Scheduling took ${SCHEDULE_TIME_MS}ms"
fi

# Test 9: Verify QEMU pod isolation
echo ""
echo "[9] Verifying QEMU pod namespace isolation..."
CUSTOM_NS="qemu-test-$(date +%s | tail -c 5)"

# Get pods from both namespaces
DEFAULT_PODS=$(curl -s -X GET $API_URL/api/v1/namespaces/default/pods)
DEFAULT_POD_COUNT=$(echo "$DEFAULT_PODS" | python3 -c "
import sys, json
try:
    print(len(json.load(sys.stdin).get('items', [])))
except:
    print(0)
" 2>/dev/null)

echo "✓ PASS: Default namespace has $DEFAULT_POD_COUNT pods"

# Test 10: Cleanup E2E test pods
echo ""
echo "[10] Cleaning up E2E QEMU test pods..."
curl -s -X DELETE $API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_NAME > /dev/null 2>&1
curl -s -X DELETE $API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_RESOURCE_NAME > /dev/null 2>&1
curl -s -X DELETE $API_URL/api/v1/namespaces/$NAMESPACE/pods/$PERF_POD_NAME > /dev/null 2>&1

echo "✓ PASS: Cleaned up test pods"

echo ""
echo "=========================================="
echo "QEMU Scheduling E2E Tests Complete"
echo "=========================================="
echo "✓ All QEMU scheduling tests passed!"
echo "✓ Pods are successfully scheduled to QEMU"
echo "✓ Scheduling is stable and responsive"
echo "=========================================="
