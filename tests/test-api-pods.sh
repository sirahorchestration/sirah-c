#!/bin/bash
pkill -9 sirah qemu 2>/dev/null || true
sleep 1

mkdir -p /tmp/sirah-unikernels
dd if=/dev/zero of=/tmp/sirah-unikernels/test-kernel bs=1M count=10 2>/dev/null

/mnt/c/projects/k8s_unikernels/sirah/bin/sirah-apiserver > /tmp/api.log 2>&1 &
APIPID=$!
sleep 3

echo "Creating pod..."
curl -s -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H "Content-Type: application/json" \
  -d '{"apiVersion":"v1","kind":"Pod","metadata":{"name":"test1","namespace":"default"},"spec":{"containers":[{"name":"c1","image":"unikernel.img","command":["/bin/sh","-c","sleep 300"],"resources":{"requests":{"cpu":"100m","memory":"64Mi"}}}]}}'

echo ""
echo ""
echo "Retrieving pod..."
curl -s http://localhost:6443/api/v1/namespaces/default/pods/test1

echo ""
echo ""
echo "Listing all pods..."
curl -s http://localhost:6443/api/v1/namespaces/default/pods

kill $APIPID 2>/dev/null || true
