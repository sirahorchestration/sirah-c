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

echo "=== Test curl -X GET ==="
PODS=$(curl -s -X GET http://localhost:6443/api/v1/namespaces/default/pods)
echo "Length: ${#PODS}"
echo "Content: $PODS"

pkill -9 sirah-apiserver 2>/dev/null || true
