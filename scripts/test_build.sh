#!/bin/bash
cd /mnt/c/projects/k8s_unikernels/sirah
echo "Starting compilation test..."
make clean
echo "---"
make 2>&1 | tail -30
echo "---"
if [ -f bin/apiserver ]; then echo "✓ apiserver built"; fi
if [ -f bin/scheduler ]; then echo "✓ scheduler built"; fi
if [ -f bin/controller ]; then echo "✓ controller built"; fi
if [ -f bin/kubelet ]; then echo "✓ kubelet built"; fi
