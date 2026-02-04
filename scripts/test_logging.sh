#!/bin/bash

# Start kubelet if not already running
if ! pgrep -f "sirah-kubelet.*control-plane" > /dev/null; then
    echo "Starting kubelet for control-plane node..."
    cd /mnt/c/projects/sirah-c/sirah
    ./bin/sirah-kubelet --node-name control-plane > /tmp/kubelet.log 2>&1 &
    sleep 2
fi

# Simple test for pod log streaming
API_URL="http://localhost:6443"
ADMIN_USER="admin:admin"

POD_NAME="test-pod-$(date +%s)"
NAMESPACE="default"

echo "Creating pod: $POD_NAME"

POD_MANIFEST=$(cat << 'EOF'
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "POD_NAME_PLACEHOLDER",
    "namespace": "default"
  },
  "spec": {
    "containers": [{
      "name": "app",
      "image": "/tmp/sirah-unikernels/test-kernel.img",
      "resources": {
        "limits": {
          "memory": "128Mi",
          "cpu": "1"
        }
      }
    }]
  }
}
EOF
)

# Replace placeholder
POD_MANIFEST="${POD_MANIFEST//POD_NAME_PLACEHOLDER/$POD_NAME}"

echo "Pod manifest:"
echo "$POD_MANIFEST" | jq .

# Create pod
echo ""
echo "Creating pod via API..."
RESPONSE=$(curl -s -X POST "$API_URL/api/v1/namespaces/$NAMESPACE/pods" \
  -H "Content-Type: application/json" \
  -u "$ADMIN_USER" \
  -d "$POD_MANIFEST")

echo "Response:"
echo "$RESPONSE" | jq . 2>/dev/null || echo "Raw response: $RESPONSE"

CREATED_NAME=$(echo "$RESPONSE" | jq -r '.metadata.name' 2>/dev/null)
echo "Created name extracted: [$CREATED_NAME]"
if [ "$CREATED_NAME" = "$POD_NAME" ]; then
    echo "✓ Pod created successfully!"
else
    echo "✗ Pod creation failed"
    echo "Expected name: [$POD_NAME]"
    echo "Got name: [$CREATED_NAME]"
fi

# Wait for pod to be scheduled and running
echo ""
echo "Waiting for pod to be scheduled..."
for i in {1..60}; do
    POD_DATA=$(curl -s -u "$ADMIN_USER" "$API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_NAME")
    PHASE=$(echo "$POD_DATA" | jq -r '.status.phase' 2>/dev/null)
    NODE=$(echo "$POD_DATA" | jq -r '.spec.nodeName' 2>/dev/null)
    
    echo "  Attempt $i: Phase=$PHASE, Node=$NODE"
    
    if [ "$PHASE" = "Running" ]; then
        echo "✓ Pod is Running on node $NODE"
        break
    fi
    
    sleep 1
done

# Try to get logs
echo ""
echo "Checking for log file..."
LOG_DIR="/tmp/sirah-logs/pods/$NAMESPACE/$POD_NAME"
LOG_FILE="$LOG_DIR/app.log"

if [ -d "$LOG_DIR" ]; then
    echo "✓ Pod log directory exists: $LOG_DIR"
    ls -la "$LOG_DIR"
    
    if [ -f "$LOG_FILE" ]; then
        echo "✓ Log file exists: $LOG_FILE"
        LINES=$(wc -l < "$LOG_FILE")
        echo "  Size: $(du -h "$LOG_FILE" | awk '{print $1}'), Lines: $LINES"
        echo ""
        echo "First 20 lines:"
        head -20 "$LOG_FILE"
    else
        echo "⚠ Log file not found yet: $LOG_FILE"
    fi
else
    echo "⚠ Pod log directory doesn't exist: $LOG_DIR"
fi

# Try to get logs via API
echo ""
echo "Getting logs via API..."
API_LOGS=$(curl -s -u "$ADMIN_USER" "$API_URL/api/v1/namespaces/$NAMESPACE/pods/$POD_NAME/log?tailLines=10")
if [ -n "$API_LOGS" ]; then
    echo "✓ Got logs from API:"
    echo "$API_LOGS"
else
    echo "⚠ No logs from API endpoint"
fi
