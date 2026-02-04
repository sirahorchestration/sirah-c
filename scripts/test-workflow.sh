#!/bin/bash

API_SERVER="http://localhost:6443"

echo "Creating test deployment..."
curl -s -X POST \
  -H 'Content-Type: application/json' \
  -d '{"metadata":{"name":"test-deploy","namespace":"default"},"spec":{"replicas":2}}' \
  "$API_SERVER/api/v1/namespaces/default/deployments"

echo ""
echo ""
echo "Waiting 10 seconds for controller to create pods..."
sleep 10

echo "Listing pods..."
curl -s "$API_SERVER/api/v1/namespaces/default/pods"

echo ""
echo "Waiting 5 more seconds for scheduler to bind pods..."
sleep 5

echo "Checking pod status..."
curl -s "$API_SERVER/api/v1/namespaces/default/pods" | head -50
