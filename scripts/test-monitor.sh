#!/bin/bash
cd /mnt/c/projects/k8s_unikernels/sirah

pkill -9 sirah-apiserver 2>/dev/null || true
sleep 1

./bin/sirah-apiserver >/tmp/server.log 2>&1 &
sleep 2

# Create pod
curl -s -X POST \
  -H "Content-Type: application/json" \
  -u admin:admin \
  -d '{"apiVersion":"v1","kind":"Pod","metadata":{"name":"test","namespace":"default"},"spec":{"containers":[{"name":"app","image":"test"}]}}' \
  http://localhost:6443/api/v1/namespaces/default/pods >/dev/null

sleep 1

# Try the monitoring script
bash scripts/view-qemu-pods.sh default 2>&1 | head -80

pkill -9 sirah-apiserver 2>/dev/null || true
