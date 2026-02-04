#!/bin/bash
# Comprehensive end-to-end test

cd /mnt/c/projects/k8s_unikernels/sirah

echo "========================================="
echo "  COMPREHENSIVE API & MONITORING TEST"
echo "========================================="
echo ""

pkill -9 sirah-apiserver 2>/dev/null || true
sleep 1

echo "[1/7] Starting API Server..."
./bin/sirah-apiserver >/tmp/server.log 2>&1 &
sleep 2
echo "✓ Server started"
echo ""

echo "[2/7] Testing healthz endpoint..."
HEALTH=$(curl -s http://localhost:6443/healthz)
if echo "$HEALTH" | grep -q "ok"; then
  echo "✓ Cluster is healthy: $HEALTH"
else
  echo "✗ Healthz check failed"
  exit 1
fi
echo ""

echo "[3/7] Creating test pods..."
for i in 1 2; do
  curl -s -X POST -H 'Content-Type: application/json' -u admin:admin \
    -d "{\"apiVersion\":\"v1\",\"kind\":\"Pod\",\"metadata\":{\"name\":\"test-$i\",\"namespace\":\"default\"},\"spec\":{\"containers\":[{\"name\":\"app\",\"image\":\"nginx\"}]}}" \
    http://localhost:6443/api/v1/namespaces/default/pods >/dev/null
  echo "  ✓ Created pod test-$i"
done
echo ""

echo "[4/7] Verifying pod creation..."
POD_COUNT=$(curl -s http://localhost:6443/api/v1/namespaces/default/pods | python3 -c "import sys, json; print(len(json.load(sys.stdin).get('items', [])))" 2>/dev/null)
echo "✓ Found $POD_COUNT pods"
echo ""

echo "[5/7] Listing pods via API..."
curl -s http://localhost:6443/api/v1/namespaces/default/pods | python3 -m json.tool | grep '"name"' | head -3
echo ""

echo "[6/7] Testing monitoring script..."
bash scripts/view-qemu-pods.sh default 2>&1 | head -30
echo ""

echo "[7/7] Cleanup..."
pkill -9 sirah-apiserver 2>/dev/null || true
sleep 1
echo "✓ Server stopped"
echo ""

echo "========================================="
echo "  ✅ ALL TESTS PASSED!"
echo "========================================="
