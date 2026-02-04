// internal/apiserver/openapi.h
// OpenAPI v3.0 / Swagger specification generation

#ifndef K8S_OPENAPI_H
#define K8S_OPENAPI_H

#include <json-c/json.h>

// OpenAPI schema generator
typedef struct {
    json_object* spec;
    char* title;
    char* version;
    char* description;
    char* base_path;
} openapi_spec_t;

// ============ Specification Generation ============

openapi_spec_t* openapi_spec_new(const char* title, const char* version);
void openapi_spec_free(openapi_spec_t* spec);

// Set server information
int openapi_spec_set_server(openapi_spec_t* spec, const char* url, const char* description);

// Add path definition
int openapi_spec_add_path(openapi_spec_t* spec,
                          const char* path,
                          const char* method,
                          const char* summary,
                          const char* description,
                          const char* request_body_schema,
                          const char* response_schema);

// Add schema definition
int openapi_spec_add_schema(openapi_spec_t* spec,
                            const char* schema_name,
                            json_object* schema_def);

// Serialize to JSON string
char* openapi_spec_to_json(openapi_spec_t* spec);

// ============ Kubernetes API Schema Definitions ============

// Generate Pod schema
json_object* openapi_pod_schema(void);

// Generate Deployment schema
json_object* openapi_deployment_schema(void);

// Generate Service schema
json_object* openapi_service_schema(void);

// Generate StatefulSet schema
json_object* openapi_statefulset_schema(void);

// Generate Job schema
json_object* openapi_job_schema(void);

// Generate CronJob schema
json_object* openapi_cronjob_schema(void);

// Generate ConfigMap schema
json_object* openapi_configmap_schema(void);

// Generate Secret schema
json_object* openapi_secret_schema(void);

// Generate Node schema
json_object* openapi_node_schema(void);

// Generate Namespace schema
json_object* openapi_namespace_schema(void);

// ============ Global OpenAPI Spec ============

// Get or create global OpenAPI spec
openapi_spec_t* openapi_spec_global(void);

// Regenerate spec (should be called during startup)
int openapi_spec_rebuild(void);

// Serve OpenAPI spec at /openapi/v3
int openapi_handle_request(const char* path, char* response_buffer, int* response_code);

#endif
