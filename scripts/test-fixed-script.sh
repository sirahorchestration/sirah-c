#!/bin/bash
cd /mnt/c/projects/k8s_unikernels/sirah

pkill -9 sirah-apiserver 2>/dev/null || true
sleep 1

./bin/sirah-apiserver >/tmp/server.log 2>&1 &
sleep 2

# Create pod
curl -s -X POST -H 'Content-Type: application/json' -u admin:admin \
  -d '{"apiVersion":"v1","kind":"Pod","metadata":{"name":"test-app","namespace":"default"},"spec":{"containers":[{"name":"app","image":"test:latest"}]}}' \
  http://localhost:6443/api/v1/namespaces/default/pods >/dev/null

sleep 1

# Run the fixed script
bash scripts/view-qemu-pods.sh default

pkill -9 sirah-apiserver 2>/dev/null || true
