#!/bin/bash

# Wait for server to start
sleep 5

echo "Creating pod..."
POD_JSON='{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "my-unikernel",
    "namespace": "default"
  },
  "spec": {
    "containers": [
      {
        "name": "app",
        "image": "unikernel.img"
      }
    ]
  }
}'

echo "POD_JSON=$POD_JSON"

RESPONSE=$(curl -s -X POST http://localhost:6443/api/v1/namespaces/default/pods \
  -H 'Content-Type: application/json' \
  -d "$POD_JSON")

echo "Raw response:"
echo "$RESPONSE"

echo ""
echo "Pretty JSON response:"
echo "$RESPONSE" | python3 -m json.tool

echo ""
echo "Getting pod..."
curl -s http://localhost:6443/api/v1/namespaces/default/pods/my-unikernel | python3 -m json.tool

echo ""
echo "Testing kubectl logs..."
export KUBECONFIG=/tmp/kubeconfig.yaml
cat > /tmp/kubeconfig.yaml <<EOF
apiVersion: v1
kind: Config
clusters:
- cluster:
    server: http://localhost:6443
  name: sirah-local
contexts:
- context:
    cluster: sirah-local
    user: test-user
  name: default
current-context: default
users:
- name: test-user
  user:
    username: test
EOF

kubectl logs my-unikernel 2>&1
