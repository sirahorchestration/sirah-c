#!/bin/bash

TEST_VAR='{"test": "value"}'

echo "=== With single quotes (heredoc) ==="
python3 << 'PYTHON_HEREDOC'
import sys, json
data = sys.stdin.read()
print(f"Received: {data}")
PYTHON_HEREDOC

echo ""
echo "=== With double quotes (heredoc) ==="
python3 << "PYTHON_HEREDOC"
import sys, json
data = sys.stdin.read()
print(f"Received: {data}")
PYTHON_HEREDOC

echo ""
echo "=== Using echo with pipe (single quotes) ==="
echo "$TEST_VAR" | python3 << 'PYTHON_HEREDOC'
import sys, json
data = sys.stdin.read()
print(f"Received: {data}")
PYTHON_HEREDOC

echo ""
echo "=== Using echo with pipe (double quotes) ==="
echo "$TEST_VAR" | python3 << "PYTHON_HEREDOC"
import sys, json
data = sys.stdin.read()
print(f"Received: {data}")
PYTHON_HEREDOC
