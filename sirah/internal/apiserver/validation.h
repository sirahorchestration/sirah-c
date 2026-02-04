// internal/apiserver/validation.h
// Resource validation rules enforcement

#ifndef K8S_API_VALIDATION_H
#define K8S_API_VALIDATION_H

#include <stdbool.h>
#include <json-c/json.h>

// Validation error
typedef struct {
    char* field;
    char* message;
} validation_error_t;

// Validation result
typedef struct {
    bool valid;
    validation_error_t** errors;
    int error_count;
} validation_result_t;

// ============ Pod Validation ============

validation_result_t* validate_pod_spec(json_object* pod_spec);
validation_result_t* validate_pod_metadata(json_object* metadata);
validation_result_t* validate_pod_containers(json_object* containers);

// ============ Service Validation ============

validation_result_t* validate_service_spec(json_object* service_spec);
validation_result_t* validate_service_selector(json_object* selector);
validation_result_t* validate_service_ports(json_object* ports);

// ============ Deployment Validation ============

validation_result_t* validate_deployment_spec(json_object* deployment_spec);
validation_result_t* validate_replica_count(json_object* replicas);

// ============ Generic Validation ============

bool validate_name(const char* name);
bool validate_namespace(const char* namespace);
bool validate_label(const char* key, const char* value);
bool validate_resource_quantity(const char* quantity);

// ============ Result Management ============

validation_result_t* validation_result_new(void);
void validation_result_free(validation_result_t* result);

int validation_result_add_error(validation_result_t* result,
                               const char* field, const char* message);

int validation_result_to_json(validation_result_t* result,
                             char* output_buffer, int buffer_size);

#endif
