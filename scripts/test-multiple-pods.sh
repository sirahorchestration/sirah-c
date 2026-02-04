#!/bin/bash
cd /mnt/c/projects/k8s_unikernels/sirah

echo "Creating first pod (test-pod)..."
curl -s -X POST http://localhost:6443/api/v1/namespaces/default/pods -H 'Content-Type: application/json' -d @test_pod.json | python3 -m json.tool | grep -A 2 '"name"'

echo ""
echo "Creating second pod (another-test)..."
curl -s -X POST http://localhost:6443/api/v1/namespaces/default/pods -H 'Content-Type: application/json' -d '{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "another-test",
    "namespace": "default"
  },
  "spec": {
    "containers": [
      {
        "name": "test-container",
        "image": "busybox:latest"
      }
    ]
  }
}' | python3 -m json.tool | grep -A 2 '"name"'

echo ""
echo "Listing all pods..."
curl -s http://localhost:6443/api/v1/namespaces/default/pods | python3 -m json.tool | grep -A 1 '"name"'
