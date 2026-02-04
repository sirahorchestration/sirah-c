#!/bin/bash
# Helper script to convert YAML fixtures to JSON for the Sirah API
# The Sirah API server requires JSON, not YAML

YAML_FILE="$1"

# Use yq if available, otherwise use a Python fallback
if command -v yq &> /dev/null; then
    yq -o json "$YAML_FILE"
elif command -v python3 &> /dev/null; then
    python3 << PYTHON
import yaml
import json
import sys

with open('$YAML_FILE', 'r') as f:
    data = yaml.safe_load(f)
    print(json.dumps(data, indent=2))
PYTHON
else
    echo "Error: Neither yq nor python3 found. Cannot convert YAML to JSON" >&2
    exit 1
fi
