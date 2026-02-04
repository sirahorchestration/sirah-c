#!/bin/bash

TEST_VAR='{"items": [{"name": "test"}]}'

echo "=== Piping to heredoc with single quotes ==="
echo "$TEST_VAR" | python3 << 'PYTHON_SCRIPT'
import sys, json
try:
    data = json.load(sys.stdin)
    print(f"Items: {data.get('items', [])}")
except Exception as e:
    print(f"Error: {e}")
PYTHON_SCRIPT

echo ""
echo "=== Piping to heredoc with double quotes ==="
echo "$TEST_VAR" | python3 << "PYTHON_SCRIPT"
import sys, json
try:
    data = json.load(sys.stdin)
    print(f"Items: {data.get('items', [])}")
except Exception as e:
    print(f"Error: {e}")
PYTHON_SCRIPT
