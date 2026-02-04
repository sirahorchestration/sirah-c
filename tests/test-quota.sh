#!/bin/bash

# ResourceQuota Admission Control Test Suite
# Tests quota enforcement for various resources

API_URL="http://localhost:8080/api/v1"
NAMESPACE="default"
TEST_COUNT=0
PASS_COUNT=0
FAIL_COUNT=0

# Color codes
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "=========================================="
echo "ResourceQuota Admission Control Test Suite"
echo "=========================================="

# Test 1: Create ResourceQuota
test_1_create_quota() {
    TEST_COUNT=$((TEST_COUNT + 1))
    echo -e "\n${YELLOW}Test 1: Create ResourceQuota${NC}"
    
    QUOTA_JSON=$(cat <<EOF
{
  "apiVersion": "v1",
  "kind": "ResourceQuota",
  "metadata": {
    "name": "test-quota",
    "namespace": "$NAMESPACE"
  },
  "spec": {
    "hard": {
      "cpu": "4000m",
      "memory": "8Gi",
      "pods": "100",
      "services": "10",
      "configmaps": "50",
      "secrets": "50"
    }
  }
}
EOF
    )
    
    RESPONSE=$(curl -s -X POST "$API_URL/namespaces/$NAMESPACE/resourcequotas" \
        -H "Content-Type: application/json" \
        -d "$QUOTA_JSON")
    
    if echo "$RESPONSE" | grep -q '"kind":"ResourceQuota"'; then
        echo -e "${GREEN}✓ PASS${NC} - ResourceQuota created successfully"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo -e "${RED}✗ FAIL${NC} - Failed to create ResourceQuota"
        echo "Response: $RESPONSE"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

# Test 2: Get ResourceQuota
test_2_get_quota() {
    TEST_COUNT=$((TEST_COUNT + 1))
    echo -e "\n${YELLOW}Test 2: Get ResourceQuota${NC}"
    
    RESPONSE=$(curl -s -X GET "$API_URL/namespaces/$NAMESPACE/resourcequotas/test-quota")
    
    if echo "$RESPONSE" | grep -q '"name":"test-quota"'; then
        echo -e "${GREEN}✓ PASS${NC} - ResourceQuota retrieved successfully"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo -e "${RED}✗ FAIL${NC} - Failed to get ResourceQuota"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

# Test 3: List ResourceQuotas
test_3_list_quotas() {
    TEST_COUNT=$((TEST_COUNT + 1))
    echo -e "\n${YELLOW}Test 3: List ResourceQuotas${NC}"
    
    RESPONSE=$(curl -s -X GET "$API_URL/namespaces/$NAMESPACE/resourcequotas")
    
    if echo "$RESPONSE" | grep -q '"test-quota"'; then
        echo -e "${GREEN}✓ PASS${NC} - ResourceQuota list retrieved"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo -e "${RED}✗ FAIL${NC} - Failed to list ResourceQuotas"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

# Test 4: Create Pod (within quota)
test_4_create_pod_allowed() {
    TEST_COUNT=$((TEST_COUNT + 1))
    echo -e "\n${YELLOW}Test 4: Create Pod (within quota)${NC}"
    
    POD_JSON=$(cat <<EOF
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "test-pod-1",
    "namespace": "$NAMESPACE"
  },
  "spec": {
    "containers": [
      {
        "name": "nginx",
        "image": "nginx:latest",
        "resources": {
          "requests": {
            "cpu": "100m",
            "memory": "128Mi"
          }
        }
      }
    ]
  }
}
EOF
    )
    
    RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "$API_URL/namespaces/$NAMESPACE/pods" \
        -H "Content-Type: application/json" \
        -d "$POD_JSON")
    
    HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
    
    if [ "$HTTP_CODE" = "201" ]; then
        echo -e "${GREEN}✓ PASS${NC} - Pod created (HTTP $HTTP_CODE)"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo -e "${RED}✗ FAIL${NC} - Pod creation failed (HTTP $HTTP_CODE)"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

# Test 5: Update ResourceQuota (lower limits)
test_5_update_quota() {
    TEST_COUNT=$((TEST_COUNT + 1))
    echo -e "\n${YELLOW}Test 5: Update ResourceQuota (lower limits)${NC}"
    
    QUOTA_JSON=$(cat <<EOF
{
  "apiVersion": "v1",
  "kind": "ResourceQuota",
  "metadata": {
    "name": "test-quota",
    "namespace": "$NAMESPACE"
  },
  "spec": {
    "hard": {
      "cpu": "500m",
      "memory": "256Mi",
      "pods": "5",
      "services": "2",
      "configmaps": "5",
      "secrets": "5"
    }
  }
}
EOF
    )
    
    RESPONSE=$(curl -s -w "\n%{http_code}" -X PATCH "$API_URL/namespaces/$NAMESPACE/resourcequotas/test-quota" \
        -H "Content-Type: application/json" \
        -d "$QUOTA_JSON")
    
    HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
    
    if [ "$HTTP_CODE" = "200" ]; then
        echo -e "${GREEN}✓ PASS${NC} - ResourceQuota updated (HTTP $HTTP_CODE)"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo -e "${RED}✗ FAIL${NC} - Update failed (HTTP $HTTP_CODE)"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

# Test 6: Create Pod (would exceed CPU quota)
test_6_create_pod_cpu_exceeded() {
    TEST_COUNT=$((TEST_COUNT + 1))
    echo -e "\n${YELLOW}Test 6: Create Pod (exceeds CPU quota)${NC}"
    
    POD_JSON=$(cat <<EOF
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "test-pod-2",
    "namespace": "$NAMESPACE"
  },
  "spec": {
    "containers": [
      {
        "name": "nginx",
        "image": "nginx:latest",
        "resources": {
          "requests": {
            "cpu": "600m",
            "memory": "64Mi"
          }
        }
      }
    ]
  }
}
EOF
    )
    
    RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "$API_URL/namespaces/$NAMESPACE/pods" \
        -H "Content-Type: application/json" \
        -d "$POD_JSON")
    
    HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
    BODY=$(echo "$RESPONSE" | head -n-1)
    
    if [ "$HTTP_CODE" = "429" ]; then
        if echo "$BODY" | grep -q "CPU quota exceeded\|quota exceeded"; then
            echo -e "${GREEN}✓ PASS${NC} - Pod creation denied (HTTP $HTTP_CODE - CPU quota exceeded)"
            PASS_COUNT=$((PASS_COUNT + 1))
        else
            echo -e "${RED}✗ FAIL${NC} - HTTP 429 but wrong reason"
            FAIL_COUNT=$((FAIL_COUNT + 1))
        fi
    else
        echo -e "${RED}✗ FAIL${NC} - Expected HTTP 429, got $HTTP_CODE"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

# Test 7: Get ResourceQuota Status
test_7_quota_status() {
    TEST_COUNT=$((TEST_COUNT + 1))
    echo -e "\n${YELLOW}Test 7: Get ResourceQuota Status${NC}"
    
    RESPONSE=$(curl -s -X GET "$API_URL/namespaces/$NAMESPACE/resourcequotas/test-quota/status")
    
    if echo "$RESPONSE" | grep -q '"status"'; then
        echo -e "${GREEN}✓ PASS${NC} - ResourceQuota status retrieved"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo -e "${RED}✗ FAIL${NC} - Failed to get status"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

# Test 8: Create Pod (pod count quota check)
test_8_pod_count_quota() {
    TEST_COUNT=$((TEST_COUNT + 1))
    echo -e "\n${YELLOW}Test 8: Pod Count Quota Enforcement${NC}"
    
    # Create pods until quota is exceeded
    EXCEEDED=false
    for i in {3..10}; do
        POD_JSON=$(cat <<EOF
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "test-pod-$i",
    "namespace": "$NAMESPACE"
  },
  "spec": {
    "containers": [
      {
        "name": "nginx",
        "image": "nginx:latest",
        "resources": {
          "requests": {
            "cpu": "50m",
            "memory": "32Mi"
          }
        }
      }
    ]
  }
}
EOF
        )
        
        RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "$API_URL/namespaces/$NAMESPACE/pods" \
            -H "Content-Type: application/json" \
            -d "$POD_JSON")
        
        HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
        
        if [ "$HTTP_CODE" = "429" ]; then
            EXCEEDED=true
            break
        fi
    done
    
    if [ "$EXCEEDED" = true ]; then
        echo -e "${GREEN}✓ PASS${NC} - Pod quota properly enforced after 5 pods"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo -e "${RED}✗ FAIL${NC} - Pod quota not enforced"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

# Test 9: Create Service (within quota)
test_9_create_service_allowed() {
    TEST_COUNT=$((TEST_COUNT + 1))
    echo -e "\n${YELLOW}Test 9: Create Service (within quota)${NC}"
    
    SERVICE_JSON=$(cat <<EOF
{
  "apiVersion": "v1",
  "kind": "Service",
  "metadata": {
    "name": "test-service-1",
    "namespace": "$NAMESPACE"
  },
  "spec": {
    "selector": {
      "app": "test"
    },
    "ports": [
      {
        "port": 80,
        "targetPort": 8080
      }
    ]
  }
}
EOF
    )
    
    RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "$API_URL/namespaces/$NAMESPACE/services" \
        -H "Content-Type: application/json" \
        -d "$SERVICE_JSON")
    
    HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
    
    if [ "$HTTP_CODE" = "201" ]; then
        echo -e "${GREEN}✓ PASS${NC} - Service created (HTTP $HTTP_CODE)"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo -e "${RED}✗ FAIL${NC} - Service creation failed (HTTP $HTTP_CODE)"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

# Test 10: Service quota exceeded
test_10_service_quota_exceeded() {
    TEST_COUNT=$((TEST_COUNT + 1))
    echo -e "\n${YELLOW}Test 10: Service Quota Exceeded${NC}"
    
    # Try to create 3rd service (quota is 2)
    SERVICE_JSON=$(cat <<EOF
{
  "apiVersion": "v1",
  "kind": "Service",
  "metadata": {
    "name": "test-service-3",
    "namespace": "$NAMESPACE"
  },
  "spec": {
    "selector": {
      "app": "test"
    },
    "ports": [
      {
        "port": 80,
        "targetPort": 8080
      }
    ]
  }
}
EOF
    )
    
    RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "$API_URL/namespaces/$NAMESPACE/services" \
        -H "Content-Type: application/json" \
        -d "$SERVICE_JSON")
    
    HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
    
    if [ "$HTTP_CODE" = "429" ]; then
        echo -e "${GREEN}✓ PASS${NC} - Service quota properly enforced (HTTP 429)"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo -e "${RED}✗ FAIL${NC} - Expected HTTP 429, got $HTTP_CODE"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

# Test 11: Create ConfigMap (within quota)
test_11_create_configmap() {
    TEST_COUNT=$((TEST_COUNT + 1))
    echo -e "\n${YELLOW}Test 11: Create ConfigMap${NC}"
    
    CM_JSON=$(cat <<EOF
{
  "apiVersion": "v1",
  "kind": "ConfigMap",
  "metadata": {
    "name": "test-config-1",
    "namespace": "$NAMESPACE"
  },
  "data": {
    "key": "value"
  }
}
EOF
    )
    
    RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "$API_URL/namespaces/$NAMESPACE/configmaps" \
        -H "Content-Type: application/json" \
        -d "$CM_JSON")
    
    HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
    
    if [ "$HTTP_CODE" = "201" ]; then
        echo -e "${GREEN}✓ PASS${NC} - ConfigMap created (HTTP $HTTP_CODE)"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo -e "${RED}✗ FAIL${NC} - ConfigMap creation failed (HTTP $HTTP_CODE)"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

# Test 12: Create Secret (within quota)
test_12_create_secret() {
    TEST_COUNT=$((TEST_COUNT + 1))
    echo -e "\n${YELLOW}Test 12: Create Secret${NC}"
    
    SECRET_JSON=$(cat <<EOF
{
  "apiVersion": "v1",
  "kind": "Secret",
  "metadata": {
    "name": "test-secret-1",
    "namespace": "$NAMESPACE"
  },
  "type": "Opaque",
  "data": {
    "password": "c2VjcmV0"
  }
}
EOF
    )
    
    RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "$API_URL/namespaces/$NAMESPACE/secrets" \
        -H "Content-Type: application/json" \
        -d "$SECRET_JSON")
    
    HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
    
    if [ "$HTTP_CODE" = "201" ]; then
        echo -e "${GREEN}✓ PASS${NC} - Secret created (HTTP $HTTP_CODE)"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo -e "${RED}✗ FAIL${NC} - Secret creation failed (HTTP $HTTP_CODE)"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

# Test 13: Delete Pod (usage update)
test_13_delete_pod() {
    TEST_COUNT=$((TEST_COUNT + 1))
    echo -e "\n${YELLOW}Test 13: Delete Pod (usage tracking)${NC}"
    
    RESPONSE=$(curl -s -w "\n%{http_code}" -X DELETE "$API_URL/namespaces/$NAMESPACE/pods/test-pod-1")
    
    HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
    
    if [ "$HTTP_CODE" = "204" ]; then
        echo -e "${GREEN}✓ PASS${NC} - Pod deleted (HTTP $HTTP_CODE)"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo -e "${RED}✗ FAIL${NC} - Delete failed (HTTP $HTTP_CODE)"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

# Test 14: Delete ResourceQuota
test_14_delete_quota() {
    TEST_COUNT=$((TEST_COUNT + 1))
    echo -e "\n${YELLOW}Test 14: Delete ResourceQuota${NC}"
    
    RESPONSE=$(curl -s -w "\n%{http_code}" -X DELETE "$API_URL/namespaces/$NAMESPACE/resourcequotas/test-quota")
    
    HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
    
    if [ "$HTTP_CODE" = "204" ]; then
        echo -e "${GREEN}✓ PASS${NC} - ResourceQuota deleted (HTTP $HTTP_CODE)"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo -e "${RED}✗ FAIL${NC} - Delete failed (HTTP $HTTP_CODE)"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

# Test 15: Create Pod (no quota = allowed)
test_15_no_quota_allowed() {
    TEST_COUNT=$((TEST_COUNT + 1))
    echo -e "\n${YELLOW}Test 15: Create Pod (no quota defined = allowed)${NC}"
    
    POD_JSON=$(cat <<EOF
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "test-pod-noquota",
    "namespace": "$NAMESPACE"
  },
  "spec": {
    "containers": [
      {
        "name": "nginx",
        "image": "nginx:latest"
      }
    ]
  }
}
EOF
    )
    
    RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "$API_URL/namespaces/$NAMESPACE/pods" \
        -H "Content-Type: application/json" \
        -d "$POD_JSON")
    
    HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
    
    if [ "$HTTP_CODE" = "201" ]; then
        echo -e "${GREEN}✓ PASS${NC} - Pod created (no quota - HTTP $HTTP_CODE)"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo -e "${RED}✗ FAIL${NC} - Pod creation failed (HTTP $HTTP_CODE)"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

# Run all tests
test_1_create_quota
test_2_get_quota
test_3_list_quotas
test_4_create_pod_allowed
test_5_update_quota
test_6_create_pod_cpu_exceeded
test_7_quota_status
test_8_pod_count_quota
test_9_create_service_allowed
test_10_service_quota_exceeded
test_11_create_configmap
test_12_create_secret
test_13_delete_pod
test_14_delete_quota
test_15_no_quota_allowed

# Summary
echo -e "\n=========================================="
echo -e "Test Summary"
echo -e "=========================================="
echo "Total Tests:  $TEST_COUNT"
echo -e "Passed:      ${GREEN}$PASS_COUNT${NC}"
echo -e "Failed:      ${RED}$FAIL_COUNT${NC}"
echo -e "Pass Rate:   $((PASS_COUNT * 100 / TEST_COUNT))%"
echo -e "=========================================="

if [ $FAIL_COUNT -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed${NC}"
    exit 1
fi
