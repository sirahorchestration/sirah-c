#!/bin/bash

# Stop all Sirah components (tmux sessions)

echo "Stopping Sirah components..."

# Kill tmux sessions
tmux kill-session -t apiserver 2>/dev/null && echo "  ✓ API Server stopped" || echo "  - API Server not running"
tmux kill-session -t scheduler 2>/dev/null && echo "  ✓ Scheduler stopped" || echo "  - Scheduler not running"
tmux kill-session -t controller 2>/dev/null && echo "  ✓ Controller Manager stopped" || echo "  - Controller not running"
tmux kill-session -t kubelet 2>/dev/null && echo "  ✓ Kubelet stopped" || echo "  - Kubelet not running"
tmux kill-session -t etcd 2>/dev/null && echo "  ✓ etcd stopped" || echo "  - etcd not running"

# Also kill any remaining processes
pkill -f "sirah-apiserver" 2>/dev/null
pkill -f "sirah-scheduler" 2>/dev/null
pkill -f "sirah-controller" 2>/dev/null
pkill -f "sirah-kubelet" 2>/dev/null
pkill -f "etcd --listen-client-urls" 2>/dev/null

sleep 1

echo ""
echo "All components stopped."
echo ""
echo "To view tmux sessions:"
echo "  tmux list-sessions"
echo ""
echo "To start again:"
echo "  ./setup-and-run.sh"
echo ""
