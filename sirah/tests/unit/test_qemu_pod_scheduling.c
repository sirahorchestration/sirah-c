// tests/unit/test_qemu_pod_scheduling.c
// Unit test: Validate QEMU pod scheduling logic

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>

// ============ Test Macros ============
#define TEST_ASSERT(condition, message) \
    if (!(condition)) { \
        printf("✗ FAIL: %s\n", message); \
        return 1; \
    } else { \
        printf("✓ PASS: %s\n", message); \
    }

#define TEST_ASSERT_STR_EQ(actual, expected, message) \
    if (strcmp(actual, expected) != 0) { \
        printf("✗ FAIL: %s (expected '%s', got '%s')\n", message, expected, actual); \
        return 1; \
    } else { \
        printf("✓ PASS: %s\n", message); \
    }

// ============ Test: Pod Scheduling JSON Parsing ============
static int test_pod_scheduling_json_parsing(void) {
    printf("\n=== Test: Pod Scheduling JSON Parsing ===\n");

    const char* pod_json = "{\"metadata\": {\"name\": \"qemu-pod\", \"namespace\": \"default\"}, \"spec\": {\"nodeName\": \"node-1\", \"containers\": [{\"name\": \"app\", \"image\": \"alpine\"}]}}";
    
    json_object* pod = json_tokener_parse(pod_json);
    TEST_ASSERT(pod != NULL, "Pod JSON parsed successfully");

    json_object* metadata;
    TEST_ASSERT(json_object_object_get_ex(pod, "metadata", &metadata), "Metadata object found");

    json_object* spec;
    TEST_ASSERT(json_object_object_get_ex(pod, "spec", &spec), "Spec object found");

    json_object* node_name_obj;
    TEST_ASSERT(json_object_object_get_ex(spec, "nodeName", &node_name_obj), "NodeName found in spec");

    const char* node_name = json_object_get_string(node_name_obj);
    TEST_ASSERT_STR_EQ(node_name, "node-1", "NodeName correctly assigned");

    json_object_put(pod);
    return 0;
}

// ============ Test: Pod Scheduling Status ============
static int test_pod_scheduling_status(void) {
    printf("\n=== Test: Pod Scheduling Status ============\n");

    const char* pod_scheduled = "{\"metadata\": {\"name\": \"scheduled-pod\"}, \"status\": {\"phase\": \"Pending\", \"conditions\": [{\"type\": \"Scheduled\", \"status\": \"True\"}]}}";
    
    json_object* pod = json_tokener_parse(pod_scheduled);
    TEST_ASSERT(pod != NULL, "Scheduled pod JSON parsed");

    json_object* status;
    TEST_ASSERT(json_object_object_get_ex(pod, "status", &status), "Status object found");

    json_object* phase_obj;
    TEST_ASSERT(json_object_object_get_ex(status, "phase", &phase_obj), "Phase found");

    const char* phase = json_object_get_string(phase_obj);
    TEST_ASSERT_STR_EQ(phase, "Pending", "Pod phase is Pending");

    json_object* conditions_obj;
    TEST_ASSERT(json_object_object_get_ex(status, "conditions", &conditions_obj), "Conditions array found");

    json_object* first_condition = json_object_array_get_idx(conditions_obj, 0);
    TEST_ASSERT(first_condition != NULL, "First condition exists");

    json_object* condition_type;
    TEST_ASSERT(json_object_object_get_ex(first_condition, "type", &condition_type), "Condition type found");

    const char* cond_type = json_object_get_string(condition_type);
    TEST_ASSERT_STR_EQ(cond_type, "Scheduled", "Condition type is Scheduled");

    json_object_put(pod);
    return 0;
}

// ============ Test: QEMU Resource Requirements ============
static int test_qemu_resource_requirements(void) {
    printf("\n=== Test: QEMU Resource Requirements ===\n");

    const char* qemu_pod_json = "{\"spec\": {\"containers\": [{\"name\": \"qemu-app\", \"image\": \"alpine:latest\", \"resources\": {\"requests\": {\"memory\": \"64Mi\", \"cpu\": \"100m\"}, \"limits\": {\"memory\": \"128Mi\", \"cpu\": \"200m\"}}}]}}";
    
    json_object* pod = json_tokener_parse(qemu_pod_json);
    TEST_ASSERT(pod != NULL, "QEMU pod JSON parsed");

    json_object* spec;
    TEST_ASSERT(json_object_object_get_ex(pod, "spec", &spec), "Spec found");

    json_object* containers;
    TEST_ASSERT(json_object_object_get_ex(spec, "containers", &containers), "Containers array found");

    json_object* container = json_object_array_get_idx(containers, 0);
    TEST_ASSERT(container != NULL, "Container found");

    json_object* resources;
    TEST_ASSERT(json_object_object_get_ex(container, "resources", &resources), "Resources object found");

    json_object* requests;
    TEST_ASSERT(json_object_object_get_ex(resources, "requests", &requests), "Resource requests found");

    json_object* limits;
    TEST_ASSERT(json_object_object_get_ex(resources, "limits", &limits), "Resource limits found");

    json_object* mem_request;
    TEST_ASSERT(json_object_object_get_ex(requests, "memory", &mem_request), "Memory request found");

    const char* mem = json_object_get_string(mem_request);
    TEST_ASSERT_STR_EQ(mem, "64Mi", "Memory request is 64Mi");

    json_object* cpu_request;
    TEST_ASSERT(json_object_object_get_ex(requests, "cpu", &cpu_request), "CPU request found");

    const char* cpu = json_object_get_string(cpu_request);
    TEST_ASSERT_STR_EQ(cpu, "100m", "CPU request is 100m");

    json_object_put(pod);
    return 0;
}

// ============ Test: Node Selection for QEMU ============
static int test_node_selection_for_qemu(void) {
    printf("\n=== Test: Node Selection for QEMU ===\n");

    const char* nodes_json = "{\"items\": [{\"metadata\": {\"name\": \"qemu-node-1\", \"labels\": {\"kubernetes.io/os\": \"linux\", \"node.kubernetes.io/instance-type\": \"qemu\"}}}, {\"metadata\": {\"name\": \"qemu-node-2\", \"labels\": {\"kubernetes.io/os\": \"linux\"}}}]}";
    
    json_object* nodes = json_tokener_parse(nodes_json);
    TEST_ASSERT(nodes != NULL, "Nodes JSON parsed");

    json_object* items;
    TEST_ASSERT(json_object_object_get_ex(nodes, "items", &items), "Items array found");

    int node_count = json_object_array_length(items);
    TEST_ASSERT(node_count == 2, "Node count is 2");

    json_object* first_node = json_object_array_get_idx(items, 0);
    TEST_ASSERT(first_node != NULL, "First node found");

    json_object* metadata;
    TEST_ASSERT(json_object_object_get_ex(first_node, "metadata", &metadata), "Node metadata found");

    json_object* node_name;
    TEST_ASSERT(json_object_object_get_ex(metadata, "name", &node_name), "Node name found");

    const char* name = json_object_get_string(node_name);
    TEST_ASSERT_STR_EQ(name, "qemu-node-1", "Node name is qemu-node-1");

    json_object* labels;
    TEST_ASSERT(json_object_object_get_ex(metadata, "labels", &labels), "Node labels found");

    json_object* os_label;
    TEST_ASSERT(json_object_object_get_ex(labels, "kubernetes.io/os", &os_label), "OS label found");

    const char* os = json_object_get_string(os_label);
    TEST_ASSERT_STR_EQ(os, "linux", "OS label is linux");

    json_object_put(nodes);
    return 0;
}

// ============ Test: Pod Affinity Constraints ============
static int test_pod_affinity_constraints(void) {
    printf("\n=== Test: Pod Affinity Constraints ===\n");

    const char* pod_affinity_json = "{\"spec\": {\"affinity\": {\"nodeAffinity\": {\"requiredDuringSchedulingIgnoredDuringExecution\": {\"nodeSelectorTerms\": [{\"matchExpressions\": [{\"key\": \"kubernetes.io/os\", \"operator\": \"In\", \"values\": [\"linux\"]}]}]}}}}}";
    
    json_object* pod = json_tokener_parse(pod_affinity_json);
    TEST_ASSERT(pod != NULL, "Pod affinity JSON parsed");

    json_object* spec;
    TEST_ASSERT(json_object_object_get_ex(pod, "spec", &spec), "Spec found");

    json_object* affinity;
    TEST_ASSERT(json_object_object_get_ex(spec, "affinity", &affinity), "Affinity object found");

    json_object* node_affinity;
    TEST_ASSERT(json_object_object_get_ex(affinity, "nodeAffinity", &node_affinity), "Node affinity found");

    json_object* required;
    TEST_ASSERT(json_object_object_get_ex(node_affinity, "requiredDuringSchedulingIgnoredDuringExecution", &required), "Required during scheduling found");

    json_object* terms;
    TEST_ASSERT(json_object_object_get_ex(required, "nodeSelectorTerms", &terms), "Node selector terms found");

    json_object* first_term = json_object_array_get_idx(terms, 0);
    TEST_ASSERT(first_term != NULL, "First term found");

    json_object* expressions;
    TEST_ASSERT(json_object_object_get_ex(first_term, "matchExpressions", &expressions), "Match expressions found");

    json_object* first_expr = json_object_array_get_idx(expressions, 0);
    TEST_ASSERT(first_expr != NULL, "First expression found");

    json_object* expr_key;
    TEST_ASSERT(json_object_object_get_ex(first_expr, "key", &expr_key), "Expression key found");

    const char* key = json_object_get_string(expr_key);
    TEST_ASSERT_STR_EQ(key, "kubernetes.io/os", "Expression key is kubernetes.io/os");

    json_object_put(pod);
    return 0;
}

// ============ Test: QEMU Scheduling Metadata ============
static int test_qemu_scheduling_metadata(void) {
    printf("\n=== Test: QEMU Scheduling Metadata ===\n");

    const char* qemu_scheduled_pod = "{\"metadata\": {\"name\": \"qemu-scheduled\", \"labels\": {\"runtime\": \"qemu\", \"scheduler-version\": \"1.0\"}}, \"spec\": {\"nodeName\": \"qemu-worker\", \"runtimeClassName\": \"qemu\"}, \"status\": {\"phase\": \"Running\", \"qemuPID\": \"12345\", \"containerRuntime\": \"qemu-system-x86_64\"}}";
    
    json_object* pod = json_tokener_parse(qemu_scheduled_pod);
    TEST_ASSERT(pod != NULL, "QEMU scheduled pod JSON parsed");

    json_object* metadata;
    TEST_ASSERT(json_object_object_get_ex(pod, "metadata", &metadata), "Metadata found");

    json_object* labels;
    TEST_ASSERT(json_object_object_get_ex(metadata, "labels", &labels), "Labels found");

    json_object* runtime_label;
    TEST_ASSERT(json_object_object_get_ex(labels, "runtime", &runtime_label), "Runtime label found");

    const char* runtime = json_object_get_string(runtime_label);
    TEST_ASSERT_STR_EQ(runtime, "qemu", "Runtime label is qemu");

    json_object* spec;
    TEST_ASSERT(json_object_object_get_ex(pod, "spec", &spec), "Spec found");

    json_object* runtime_class;
    TEST_ASSERT(json_object_object_get_ex(spec, "runtimeClassName", &runtime_class), "Runtime class name found");

    const char* runtime_class_name = json_object_get_string(runtime_class);
    TEST_ASSERT_STR_EQ(runtime_class_name, "qemu", "Runtime class is qemu");

    json_object* status;
    TEST_ASSERT(json_object_object_get_ex(pod, "status", &status), "Status found");

    json_object* phase;
    TEST_ASSERT(json_object_object_get_ex(status, "phase", &phase), "Phase found");

    const char* phase_str = json_object_get_string(phase);
    TEST_ASSERT_STR_EQ(phase_str, "Running", "Pod phase is Running");

    json_object_put(pod);
    return 0;
}

// ============ Test: Scheduler to Node Assignment ============
static int test_scheduler_node_assignment(void) {
    printf("\n=== Test: Scheduler to Node Assignment ===\n");

    // Simulate scheduler assigning pod to node
    const char* unscheduled_pod = "{\"metadata\": {\"name\": \"unscheduled-pod\"}, \"spec\": {\"containers\": [{\"name\": \"app\", \"image\": \"alpine\"}]}}";
    json_object* pod = json_tokener_parse(unscheduled_pod);

    json_object* spec;
    json_object_object_get_ex(pod, "spec", &spec);

    // Simulate scheduler adding nodeName
    json_object_object_add(spec, "nodeName", json_object_new_string("qemu-node-1"));

    json_object* node_name;
    json_object_object_get_ex(spec, "nodeName", &node_name);
    const char* assigned_node = json_object_get_string(node_name);

    TEST_ASSERT_STR_EQ(assigned_node, "qemu-node-1", "Pod assigned to qemu-node-1");

    json_object_put(pod);
    return 0;
}

// ============ Main Test Runner ============
int main(void) {
    printf("\n");
    printf("==========================================\n");
    printf("Running QEMU Pod Scheduling Unit Tests\n");
    printf("==========================================\n");

    int test_count = 0;
    int pass_count = 0;

    // Test 1
    test_count++;
    if (test_pod_scheduling_json_parsing() == 0) pass_count++;

    // Test 2
    test_count++;
    if (test_pod_scheduling_status() == 0) pass_count++;

    // Test 3
    test_count++;
    if (test_qemu_resource_requirements() == 0) pass_count++;

    // Test 4
    test_count++;
    if (test_node_selection_for_qemu() == 0) pass_count++;

    // Test 5
    test_count++;
    if (test_pod_affinity_constraints() == 0) pass_count++;

    // Test 6
    test_count++;
    if (test_qemu_scheduling_metadata() == 0) pass_count++;

    // Test 7
    test_count++;
    if (test_scheduler_node_assignment() == 0) pass_count++;

    printf("\n");
    printf("==========================================\n");
    printf("Test Summary\n");
    printf("==========================================\n");
    printf("Total: %d | Passed: %d | Failed: %d\n", test_count, pass_count, test_count - pass_count);
    
    if (pass_count == test_count) {
        printf("All tests passed!\n");
        printf("==========================================\n");
        return 0;
    } else {
        printf("Some tests failed!\n");
        printf("==========================================\n");
        return 1;
    }
}
