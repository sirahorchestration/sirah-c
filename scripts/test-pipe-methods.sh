#!/bin/bash

TEST_VAR='{"items": [{"name": "test"}]}'

echo "=== Method 1: Pipe echo to python3 directly ==="
echo "$TEST_VAR" | python3 -c "import sys, json; data = json.load(sys.stdin); print(f\"Items: {data.get('items', [])}\")"

echo ""
echo "=== Method 2: Pipe to python3 with stdin redirect ==="
python3 -c "import sys, json; data = json.load(sys.stdin); print(f\"Items: {data.get('items', [])}\")" <<< "$TEST_VAR"

echo ""
echo "=== Method 3: Write to temp file first ==="
echo "$TEST_VAR" > /tmp/test.json
python3 -c "import sys, json; data = json.load(open('/tmp/test.json')); print(f\"Items: {data.get('items', [])}\")"

echo ""
echo "=== Method 4: Use printf to avoid echo issues ==="
printf '%s' "$TEST_VAR" | python3 -c "import sys, json; data = json.load(sys.stdin); print(f\"Items: {data.get('items', [])}\")"
