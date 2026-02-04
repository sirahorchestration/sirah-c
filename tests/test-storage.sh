#!/bin/bash

# Storage system test suite
# Tests PersistentVolume, PersistentVolumeClaim, and StorageClass functionality

set -e

BASE_URL="http://localhost:8080"
API_PATH="/api/v1"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# Helper function to print test titles
test_title() {
    echo -e "${YELLOW}>>> $1${NC}"
}

# Helper function to validate HTTP status
check_status() {
    local expected=$1
    local actual=$2
    local test_name=$3
    
    ((TESTS_RUN++))
    if [ "$actual" -eq "$expected" ]; then
        echo -e "${GREEN}✓ $test_name (HTTP $actual)${NC}"
        ((TESTS_PASSED++))
    else
        echo -e "${RED}✗ $test_name (expected $expected, got $actual)${NC}"
        ((TESTS_FAILED++))
    fi
}

echo "=========================================="
echo "  Storage System Test Suite"
echo "=========================================="
echo

# ===== Test 1: Create StorageClass =====
test_title "Test 1: Create StorageClass (POST /api/v1/storageclasses)"
RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "$BASE_URL$API_PATH/storageclasses" \
    -H "Content-Type: application/json" \
    -d '{
        "name": "fast-ssd",
        "provisioner": "sirah.io/hostpath",
        "reclaim_policy": "Delete",
        "binding_mode": "Immediate",
        "allow_volume_expansion": true
    }')
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | head -n-1)
check_status 201 "$HTTP_CODE" "Create StorageClass"

# ===== Test 2: Get StorageClass =====
test_title "Test 2: Get StorageClass (GET /api/v1/storageclasses/{name})"
RESPONSE=$(curl -s -w "\n%{http_code}" "$BASE_URL$API_PATH/storageclasses/fast-ssd")
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
check_status 200 "$HTTP_CODE" "Get StorageClass"

# ===== Test 3: List StorageClasses =====
test_title "Test 3: List StorageClasses (GET /api/v1/storageclasses)"
RESPONSE=$(curl -s -w "\n%{http_code}" "$BASE_URL$API_PATH/storageclasses")
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
check_status 200 "$HTTP_CODE" "List StorageClasses"

# ===== Test 4: Create PersistentVolume =====
test_title "Test 4: Create PersistentVolume (POST /api/v1/persistentvolumes)"
RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "$BASE_URL$API_PATH/persistentvolumes" \
    -H "Content-Type: application/json" \
    -d '{
        "name": "pv-manual-001",
        "capacity_bytes": 10737418240,
        "access_modes": ["ReadWriteOnce"],
        "reclaim_policy": "Retain",
        "backend": {
            "type": "hostPath",
            "hostPath": {
                "path": "/var/sirah/volumes/pv-manual-001"
            }
        }
    }')
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | head -n-1)
check_status 201 "$HTTP_CODE" "Create PersistentVolume"

# ===== Test 5: Get PersistentVolume =====
test_title "Test 5: Get PersistentVolume (GET /api/v1/persistentvolumes/{name})"
RESPONSE=$(curl -s -w "\n%{http_code}" "$BASE_URL$API_PATH/persistentvolumes/pv-manual-001")
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
check_status 200 "$HTTP_CODE" "Get PersistentVolume"

# ===== Test 6: List PersistentVolumes =====
test_title "Test 6: List PersistentVolumes (GET /api/v1/persistentvolumes)"
RESPONSE=$(curl -s -w "\n%{http_code}" "$BASE_URL$API_PATH/persistentvolumes")
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
check_status 200 "$HTTP_CODE" "List PersistentVolumes"

# ===== Test 7: Create PersistentVolumeClaim (Pending) =====
test_title "Test 7: Create PersistentVolumeClaim (POST /api/v1/ns/{ns}/persistentvolumeclaims)"
RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "$BASE_URL$API_PATH/ns/default/persistentvolumeclaims" \
    -H "Content-Type: application/json" \
    -d '{
        "name": "pvc-app-001",
        "requested_capacity_bytes": 5368709120,
        "access_modes": ["ReadWriteOnce"],
        "storage_class": "fast-ssd"
    }')
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | head -n-1)
check_status 201 "$HTTP_CODE" "Create PersistentVolumeClaim"

# ===== Test 8: Get PersistentVolumeClaim =====
test_title "Test 8: Get PersistentVolumeClaim (GET /api/v1/ns/{ns}/persistentvolumeclaims/{name})"
RESPONSE=$(curl -s -w "\n%{http_code}" "$BASE_URL$API_PATH/ns/default/persistentvolumeclaims/pvc-app-001")
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
check_status 200 "$HTTP_CODE" "Get PersistentVolumeClaim"

# ===== Test 9: List PersistentVolumeClaims =====
test_title "Test 9: List PersistentVolumeClaims (GET /api/v1/ns/{ns}/persistentvolumeclaims)"
RESPONSE=$(curl -s -w "\n%{http_code}" "$BASE_URL$API_PATH/ns/default/persistentvolumeclaims")
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
check_status 200 "$HTTP_CODE" "List PersistentVolumeClaims"

# ===== Test 10: Bind PVC to PV =====
test_title "Test 10: Bind PVC to PV (PATCH /api/v1/ns/{ns}/persistentvolumeclaims/{name}/bind)"
RESPONSE=$(curl -s -w "\n%{http_code}" -X PATCH "$BASE_URL$API_PATH/ns/default/persistentvolumeclaims/pvc-app-001/bind" \
    -H "Content-Type: application/json" \
    -d '{
        "pv_name": "pv-manual-001"
    }')
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
check_status 200 "$HTTP_CODE" "Bind PVC to PV"

# ===== Test 11: PVC Phase should be Bound =====
test_title "Test 11: Verify PVC Phase is Bound"
RESPONSE=$(curl -s -w "\n%{http_code}" "$BASE_URL$API_PATH/ns/default/persistentvolumeclaims/pvc-app-001")
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | head -n-1)
if [ "$HTTP_CODE" -eq 200 ] && echo "$BODY" | grep -q '"phase":"Bound"'; then
    echo -e "${GREEN}✓ PVC Phase is Bound${NC}"
    ((TESTS_PASSED++))
else
    echo -e "${RED}✗ PVC Phase is not Bound${NC}"
    ((TESTS_FAILED++))
fi
((TESTS_RUN++))

# ===== Test 12: PV Phase should be Bound =====
test_title "Test 12: Verify PV Phase is Bound"
RESPONSE=$(curl -s -w "\n%{http_code}" "$BASE_URL$API_PATH/persistentvolumes/pv-manual-001")
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | head -n-1)
if [ "$HTTP_CODE" -eq 200 ] && echo "$BODY" | grep -q '"phase":"Bound"'; then
    echo -e "${GREEN}✓ PV Phase is Bound${NC}"
    ((TESTS_PASSED++))
else
    echo -e "${RED}✗ PV Phase is not Bound${NC}"
    ((TESTS_FAILED++))
fi
((TESTS_RUN++))

# ===== Test 13: Create Pod with Volume Mount =====
test_title "Test 13: Create Pod with Volume Mount"
RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "$BASE_URL$API_PATH/pods" \
    -H "Content-Type: application/json" \
    -d '{
        "name": "app-with-storage",
        "namespace": "default",
        "spec": {
            "containers": [
                {
                    "name": "app",
                    "image": "myapp:v1",
                    "volumeMounts": [
                        {
                            "name": "data",
                            "mountPath": "/data"
                        }
                    ]
                }
            ],
            "volumes": [
                {
                    "name": "data",
                    "persistentVolumeClaim": {
                        "claimName": "pvc-app-001"
                    }
                }
            ]
        }
    }')
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
check_status 201 "$HTTP_CODE" "Create Pod with Volume Mount"

# ===== Test 14: Provision PV from StorageClass =====
test_title "Test 14: Provision PV from StorageClass"
RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "$BASE_URL$API_PATH/persistentvolumes/provision" \
    -H "Content-Type: application/json" \
    -d '{
        "storage_class": "fast-ssd",
        "capacity_bytes": 5368709120,
        "access_modes": ["ReadWriteOnce"],
        "pvc_namespace": "default",
        "pvc_name": "pvc-provision-001"
    }')
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | head -n-1)
check_status 201 "$HTTP_CODE" "Provision PV from StorageClass"

# ===== Test 15: Create PVC with Capacity > Available =====
test_title "Test 15: Reject PVC with Excessive Capacity"
RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "$BASE_URL$API_PATH/ns/default/persistentvolumeclaims" \
    -H "Content-Type: application/json" \
    -d '{
        "name": "pvc-too-large",
        "requested_capacity_bytes": 1099511627776,
        "access_modes": ["ReadWriteOnce"],
        "storage_class": "fast-ssd"
    }')
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
check_status 400 "$HTTP_CODE" "Reject PVC with Excessive Capacity"

# ===== Test 16: Delete PVC (should handle PV reclaim) =====
test_title "Test 16: Delete PersistentVolumeClaim"
RESPONSE=$(curl -s -w "\n%{http_code}" -X DELETE "$BASE_URL$API_PATH/ns/default/persistentvolumeclaims/pvc-app-001")
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
check_status 204 "$HTTP_CODE" "Delete PersistentVolumeClaim"

# ===== Test 17: Update StorageClass =====
test_title "Test 17: Update StorageClass"
RESPONSE=$(curl -s -w "\n%{http_code}" -X PATCH "$BASE_URL$API_PATH/storageclasses/fast-ssd" \
    -H "Content-Type: application/json" \
    -d '{
        "allow_volume_expansion": false
    }')
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
check_status 200 "$HTTP_CODE" "Update StorageClass"

# ===== Test 18: Delete PersistentVolume =====
test_title "Test 18: Delete PersistentVolume"
RESPONSE=$(curl -s -w "\n%{http_code}" -X DELETE "$BASE_URL$API_PATH/persistentvolumes/pv-manual-001")
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
check_status 204 "$HTTP_CODE" "Delete PersistentVolume"

# ===== Test 19: Delete StorageClass =====
test_title "Test 19: Delete StorageClass"
RESPONSE=$(curl -s -w "\n%{http_code}" -X DELETE "$BASE_URL$API_PATH/storageclasses/fast-ssd")
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
check_status 204 "$HTTP_CODE" "Delete StorageClass"

# ===== Test 20: Get Default StorageClass =====
test_title "Test 20: Get Default StorageClass"
RESPONSE=$(curl -s -w "\n%{http_code}" "$BASE_URL$API_PATH/storageclasses/default")
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
check_status 200 "$HTTP_CODE" "Get Default StorageClass"

echo
echo "=========================================="
echo "  Test Summary"
echo "=========================================="
echo "Total Tests: $TESTS_RUN"
echo -e "${GREEN}Passed: $TESTS_PASSED${NC}"
if [ $TESTS_FAILED -gt 0 ]; then
    echo -e "${RED}Failed: $TESTS_FAILED${NC}"
else
    echo -e "${GREEN}Failed: 0${NC}"
fi

PASS_RATE=$((TESTS_PASSED * 100 / TESTS_RUN))
echo "Pass Rate: $PASS_RATE%"
echo

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed.${NC}"
    exit 1
fi
