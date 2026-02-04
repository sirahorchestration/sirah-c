#!/bin/bash

# Start the API server in the background
cd /mnt/c/projects/sirah-c/sirah
./bin/sirah-apiserver > /tmp/apiserver.log 2>&1 &
API_PID=$!

echo "API Server PID: $API_PID"
sleep 3

# Test /version endpoint
echo "Testing /version endpoint..."
RESPONSE=$(curl -s http://localhost:6443/version)
echo "Response: $RESPONSE"

if [ -z "$RESPONSE" ]; then
    echo "ERROR: /version returned empty response"
    kill $API_PID 2>/dev/null
    exit 1
fi

if echo "$RESPONSE" | grep -q "v1.28.0-sirah"; then
    echo "✓ /version endpoint working correctly!"
    echo "✓ Version reported: v1.28.0-sirah"
else
    echo "ERROR: /version response doesn't contain expected version"
    kill $API_PID 2>/dev/null
    exit 1
fi

# Kill the API server
kill $API_PID 2>/dev/null
wait $API_PID 2>/dev/null

echo "✓ Test passed!"
