#!/bin/bash
cd /mnt/c/projects/k8s_unikernels/sirah

# Kill and restart API server
tmux kill-session -t apiserver 2>/dev/null || true
sleep 1
tmux new-session -d -s apiserver './bin/sirah-apiserver --port 6443'
sleep 2

# Send test POST request
curl -X POST http://localhost:6443/test -H 'Content-Type: application/json' -d '{"test":"data"}' -s
echo ""

# Show tmux output
sleep 1
tmux capture-pane -t apiserver -p | tail -30
