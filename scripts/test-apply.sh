#!/bin/bash
cd /mnt/c/projects/k8s_unikernels/sirah
make 2>&1 | grep -E '(error|✓)' || true
./bin/sirah-apiserver --port 6443 2>&1 &
SERVER_PID=$!
sleep 2
kubectl apply -f test-pod.json
RESULT=$?
echo "---"
sleep 1
kill $SERVER_PID 2>/dev/null
exit $RESULT
