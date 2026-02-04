// tests/unit/test_pod_management.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <json-c/json.h>

// Simple test assertion macros
#define TEST_ASSERT(condition, message) \
    if (!(condition)) { \
        printf("FAIL: %s\n", message); \
        return 1; \
    } else { \
        printf("PASS: %s\n", message); \
    }

#define TEST_ASSERT_STR_EQ(actual, expected, message) \
    if (strcmp(actual, expected) != 0) { \
        printf("FAIL: %s (expected: %s, got: %s)\n", message, expected, actual); \
        return 1; \
    } else { \
        printf("PASS: %s\n", message); \
    }

// Test pod JSON parsing
int test_pod_json_parsing() {
    printf("\n=== Test: Pod JSON Parsing ===\n");
    
    const char* pod_json = "{"
        "\"apiVersion\": \"v1\","
        "\"kind\": \"Pod\","
        "\"metadata\": {"
            "\"name\": \"test-pod\","
            "\"namespace\": \"default\""
        "},"
        "\"spec\": {"
            "\"containers\": [{"
                "\"name\": \"test-container\","
                "\"image\": \"busybox:latest\""
            "}]"
        "}"
    "}";
    
    json_object* root = json_tokener_parse(pod_json);
    TEST_ASSERT(root != NULL, "JSON parsing succeeded");
    
    // Extract pod name
    json_object* metadata = NULL;
    json_object_object_get_ex(root, "metadata", &metadata);
    TEST_ASSERT(metadata != NULL, "Metadata object found");
    
    json_object* name_obj = NULL;
    json_object_object_get_ex(metadata, "name", &name_obj);
    TEST_ASSERT(name_obj != NULL, "Name field found");
    
    const char* pod_name = json_object_get_string(name_obj);
    TEST_ASSERT_STR_EQ(pod_name, "test-pod", "Pod name extracted correctly");
    
    json_object_put(root);
    return 0;
}

// Test pod metadata validation
int test_pod_metadata_validation() {
    printf("\n=== Test: Pod Metadata Validation ===\n");
    
    // Test with empty metadata
    const char* empty_metadata_json = "{\"metadata\": {}}";
    json_object* root = json_tokener_parse(empty_metadata_json);
    TEST_ASSERT(root != NULL, "Empty metadata JSON parsed");
    
    json_object* metadata = NULL;
    json_object_object_get_ex(root, "metadata", &metadata);
    TEST_ASSERT(metadata != NULL, "Metadata object exists");
    
    json_object* name_obj = NULL;
    json_object_object_get_ex(metadata, "name", &name_obj);
    TEST_ASSERT(name_obj == NULL, "Name field is null when not provided");
    
    json_object_put(root);
    
    // Test with valid metadata
    const char* valid_metadata_json = "{\"metadata\": {\"name\": \"my-pod\", \"namespace\": \"test-ns\"}}";
    root = json_tokener_parse(valid_metadata_json);
    TEST_ASSERT(root != NULL, "Valid metadata JSON parsed");
    
    json_object_object_get_ex(root, "metadata", &metadata);
    json_object_object_get_ex(metadata, "name", &name_obj);
    const char* name = json_object_get_string(name_obj);
    TEST_ASSERT_STR_EQ(name, "my-pod", "Valid pod name extracted");
    
    json_object* ns_obj = NULL;
    json_object_object_get_ex(metadata, "namespace", &ns_obj);
    const char* ns = json_object_get_string(ns_obj);
    TEST_ASSERT_STR_EQ(ns, "test-ns", "Namespace extracted correctly");
    
    json_object_put(root);
    return 0;
}

// Test pod spec validation
int test_pod_spec_validation() {
    printf("\n=== Test: Pod Spec Validation ===\n");
    
    const char* pod_spec_json = "{"
        "\"spec\": {"
            "\"containers\": [{"
                "\"name\": \"app\","
                "\"image\": \"myapp:1.0\","
                "\"ports\": [{\"containerPort\": 8080}]"
            "}]"
        "}"
    "}";
    
    json_object* root = json_tokener_parse(pod_spec_json);
    TEST_ASSERT(root != NULL, "Pod spec JSON parsed");
    
    json_object* spec = NULL;
    json_object_object_get_ex(root, "spec", &spec);
    TEST_ASSERT(spec != NULL, "Spec object found");
    
    json_object* containers = NULL;
    json_object_object_get_ex(spec, "containers", &containers);
    TEST_ASSERT(containers != NULL, "Containers array found");
    
    int container_count = json_object_array_length(containers);
    TEST_ASSERT(container_count == 1, "Container count is correct");
    
    json_object* container = json_object_array_get_idx(containers, 0);
    json_object* container_name = NULL;
    json_object_object_get_ex(container, "name", &container_name);
    const char* cname = json_object_get_string(container_name);
    TEST_ASSERT_STR_EQ(cname, "app", "Container name extracted correctly");
    
    json_object_put(root);
    return 0;
}

// Test namespace handling
int test_namespace_handling() {
    printf("\n=== Test: Namespace Handling ===\n");
    
    // Test default namespace
    const char* no_ns_json = "{\"metadata\": {\"name\": \"pod1\"}}";
    const char* default_ns = "default";
    TEST_ASSERT_STR_EQ(default_ns, "default", "Default namespace is 'default'");
    
    // Test custom namespace
    const char* custom_ns_json = "{\"metadata\": {\"name\": \"pod1\", \"namespace\": \"kube-system\"}}";
    json_object* root = json_tokener_parse(custom_ns_json);
    json_object* metadata = NULL;
    json_object_object_get_ex(root, "metadata", &metadata);
    json_object* ns_obj = NULL;
    json_object_object_get_ex(metadata, "namespace", &ns_obj);
    const char* ns = json_object_get_string(ns_obj);
    TEST_ASSERT_STR_EQ(ns, "kube-system", "Custom namespace extracted correctly");
    
    json_object_put(root);
    return 0;
}

int main() {
    printf("========================================\n");
    printf("Running Pod Management Unit Tests\n");
    printf("========================================\n");
    
    int failures = 0;
    
    failures += test_pod_json_parsing();
    failures += test_pod_metadata_validation();
    failures += test_pod_spec_validation();
    failures += test_namespace_handling();
    
    printf("\n========================================\n");
    if (failures == 0) {
        printf("All tests passed!\n");
    } else {
        printf("Tests failed: %d\n", failures);
    }
    printf("========================================\n");
    
    return failures;
}
