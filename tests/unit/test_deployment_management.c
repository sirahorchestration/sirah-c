// tests/unit/test_deployment_management.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>

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

#define TEST_ASSERT_INT_EQ(actual, expected, message) \
    if ((actual) != (expected)) { \
        printf("FAIL: %s (expected: %d, got: %d)\n", message, expected, actual); \
        return 1; \
    } else { \
        printf("PASS: %s\n", message); \
    }

// Test deployment JSON parsing
int test_deployment_json_parsing() {
    printf("\n=== Test: Deployment JSON Parsing ===\n");
    
    const char* deployment_json = "{"
        "\"apiVersion\": \"apps/v1\","
        "\"kind\": \"Deployment\","
        "\"metadata\": {"
            "\"name\": \"nginx-deploy\","
            "\"namespace\": \"default\""
        "},"
        "\"spec\": {"
            "\"replicas\": 3,"
            "\"selector\": {\"matchLabels\": {\"app\": \"nginx\"}},"
            "\"template\": {"
                "\"metadata\": {\"labels\": {\"app\": \"nginx\"}},"
                "\"spec\": {"
                    "\"containers\": [{"
                        "\"name\": \"nginx\","
                        "\"image\": \"nginx:1.14\""
                    "}]"
                "}"
            "}"
        "}"
    "}";
    
    json_object* root = json_tokener_parse(deployment_json);
    TEST_ASSERT(root != NULL, "Deployment JSON parsed");
    
    json_object* metadata = NULL;
    json_object_object_get_ex(root, "metadata", &metadata);
    json_object* name_obj = NULL;
    json_object_object_get_ex(metadata, "name", &name_obj);
    const char* name = json_object_get_string(name_obj);
    TEST_ASSERT_STR_EQ(name, "nginx-deploy", "Deployment name extracted");
    
    json_object_put(root);
    return 0;
}

// Test deployment spec validation
int test_deployment_spec_validation() {
    printf("\n=== Test: Deployment Spec Validation ===\n");
    
    const char* deployment_json = "{"
        "\"spec\": {"
            "\"replicas\": 5,"
            "\"selector\": {\"matchLabels\": {\"app\": \"web\"}},"
            "\"template\": {"
                "\"metadata\": {\"labels\": {\"app\": \"web\"}},"
                "\"spec\": {"
                    "\"containers\": [{"
                        "\"name\": \"web\","
                        "\"image\": \"myapp:2.0\","
                        "\"ports\": [{\"containerPort\": 3000}]"
                    "}]"
                "}"
            "}"
        "}"
    "}";
    
    json_object* root = json_tokener_parse(deployment_json);
    TEST_ASSERT(root != NULL, "Deployment spec JSON parsed");
    
    json_object* spec = NULL;
    json_object_object_get_ex(root, "spec", &spec);
    
    json_object* replicas_obj = NULL;
    json_object_object_get_ex(spec, "replicas", &replicas_obj);
    int replicas = json_object_get_int(replicas_obj);
    TEST_ASSERT_INT_EQ(replicas, 5, "Replicas count extracted");
    
    json_object* selector = NULL;
    json_object_object_get_ex(spec, "selector", &selector);
    TEST_ASSERT(selector != NULL, "Selector found");
    
    json_object_put(root);
    return 0;
}

// Test replica validation
int test_replica_validation() {
    printf("\n=== Test: Replica Validation ===\n");
    
    const char* valid_replicas[] = {"1", "3", "5", "10", "100"};
    
    for (int i = 0; i < 5; i++) {
        char json[256];
        snprintf(json, sizeof(json), "{\"spec\": {\"replicas\": %s}}", valid_replicas[i]);
        
        json_object* root = json_tokener_parse(json);
        TEST_ASSERT(root != NULL, "Replica spec parsed");
        
        json_object* spec = NULL;
        json_object_object_get_ex(root, "spec", &spec);
        json_object* replicas_obj = NULL;
        json_object_object_get_ex(spec, "replicas", &replicas_obj);
        int replicas = json_object_get_int(replicas_obj);
        TEST_ASSERT(replicas > 0, "Replica count is positive");
        
        json_object_put(root);
    }
    
    return 0;
}

// Test label selector validation
int test_label_selector_validation() {
    printf("\n=== Test: Label Selector Validation ===\n");
    
    const char* selector_json = "{"
        "\"selector\": {"
            "\"matchLabels\": {"
                "\"app\": \"frontend\","
                "\"version\": \"v1\""
            "}"
        "}"
    "}";
    
    json_object* root = json_tokener_parse(selector_json);
    TEST_ASSERT(root != NULL, "Selector JSON parsed");
    
    json_object* selector = NULL;
    json_object_object_get_ex(root, "selector", &selector);
    
    json_object* match_labels = NULL;
    json_object_object_get_ex(selector, "matchLabels", &match_labels);
    TEST_ASSERT(match_labels != NULL, "MatchLabels found");
    
    json_object* app_label = NULL;
    json_object_object_get_ex(match_labels, "app", &app_label);
    const char* app = json_object_get_string(app_label);
    TEST_ASSERT_STR_EQ(app, "frontend", "App label extracted");
    
    json_object_put(root);
    return 0;
}

int main() {
    printf("========================================\n");
    printf("Running Deployment Management Unit Tests\n");
    printf("========================================\n");
    
    int failures = 0;
    
    failures += test_deployment_json_parsing();
    failures += test_deployment_spec_validation();
    failures += test_replica_validation();
    failures += test_label_selector_validation();
    
    printf("\n========================================\n");
    if (failures == 0) {
        printf("All tests passed!\n");
    } else {
        printf("Tests failed: %d\n", failures);
    }
    printf("========================================\n");
    
    return failures;
}
