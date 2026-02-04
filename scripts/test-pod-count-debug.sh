#!/bin/bash
cd /mnt/c/projects/k8s_unikernels/sirah

pkill -9 sirah-apiserver 2>/dev/null || true
sleep 1

./bin/sirah-apiserver >/tmp/test.log 2>&1 &
sleep 2

# Create pod
curl -s -X POST -H 'Content-Type: application/json' -u admin:admin \
  -d '{"apiVersion":"v1","kind":"Pod","metadata":{"name":"test","namespace":"default"},"spec":{"containers":[{"name":"app","image":"test"}]}}' \
  http://localhost:6443/api/v1/namespaces/default/pods >/dev/null

sleep 1

API_URL="http://localhost:6443"
NAMESPACE="default"

# Exactly like the script
PODS=$(curl -s -X GET "$API_URL/api/v1/namespaces/$NAMESPACE/pods")

echo "PODS variable set"
echo "PODS length: ${#PODS}"

POD_COUNT=$(echo "$PODS" | python3 -c "import sys, json; print(len(json.load(sys.stdin).get('items', [])))" 2>/dev/null)

echo "POD_COUNT: '$POD_COUNT'"
echo "POD_COUNT length: ${#POD_COUNT}"

if [ "$POD_COUNT" -eq 0 ]; then
    echo "Pods is 0, exiting"
else
    echo "Pods is not 0"
    echo "Will now try to parse PODS..."
    echo "PODS content:"
    echo "$PODS"
fi

pkill -9 sirah-apiserver 2>/dev/null || true
