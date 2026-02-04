#!/bin/bash
cd /mnt/c/projects/k8s_unikernels/sirah

pkill -9 sirah-apiserver 2>/dev/null || true
sleep 1

./bin/sirah-apiserver >/tmp/server.log 2>&1 &
sleep 2

echo "=== Creating multiple test pods ==="

for i in 1 2 3; do
  curl -s -X POST -H 'Content-Type: application/json' -u admin:admin \
    -d "{\"apiVersion\":\"v1\",\"kind\":\"Pod\",\"metadata\":{\"name\":\"app-$i\",\"namespace\":\"default\"},\"spec\":{\"containers\":[{\"name\":\"app\",\"image\":\"nginx:latest\"}]}}" \
    http://localhost:6443/api/v1/namespaces/default/pods >/dev/null
  echo "Created pod app-$i"
done

sleep 2

echo ""
echo "=== Running monitoring script ==="
bash scripts/view-qemu-pods.sh default

pkill -9 sirah-apiserver 2>/dev/null || true
