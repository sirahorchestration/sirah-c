#!/bin/bash
set -e
cd /mnt/c/projects/k8s_unikernels/sirah

echo "Stopping old processes..."
pkill -f sirah-apiserver || true
pkill -f sirah-scheduler || true
pkill -f sirah-controller || true
pkill -f sirah-kubelet || true
pkill -f etcd || true
sleep 2

echo "[1] Starting etcd..."
nohup /usr/bin/etcd --listen-client-urls=http://127.0.0.1:2379 --advertise-client-urls=http://127.0.0.1:2379 --data-dir=/tmp/sirah-etcd > /tmp/etcd.log 2>&1 &
sleep 2

echo "[2] Starting API Server..."
nohup ./bin/sirah-apiserver --port 6443 --etcd 127.0.0.1:2379 > /tmp/apiserver.log 2>&1 &
sleep 2

echo "[3] Starting Scheduler..."
nohup ./bin/sirah-scheduler > /tmp/scheduler.log 2>&1 &
sleep 1

echo "[4] Starting Controller Manager..."
nohup ./bin/sirah-controller > /tmp/controller.log 2>&1 &
sleep 1

echo "[5] Starting Kubelet..."
nohup ./bin/sirah-kubelet --node-name worker1 > /tmp/kubelet.log 2>&1 &
sleep 1

echo ""
echo "All components started! Testing API..."
sleep 1

echo "Testing healthz endpoint:"
curl -s http://localhost:6443/healthz | head -c 100
echo ""

echo "Testing pod creation..."
curl -s -X POST http://localhost:6443/api/v1/namespaces/default/pods -H 'Content-Type: application/json' -d @test_pod.json | python3 -m json.tool | head -20

echo ""
echo "Testing list pods..."
curl -s http://localhost:6443/api/v1/namespaces/default/pods | python3 -m json.tool | head -30
