// internal/apiserver/openapi.c
// OpenAPI v3.0 / Swagger specification generation

#include "openapi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static openapi_spec_t* g_openapi_spec = NULL;

// ============ Specification Management ============

openapi_spec_t* openapi_spec_new(const char* title, const char* version) {
    if (!title || !version) return NULL;
    
    openapi_spec_t* spec = (openapi_spec_t*)malloc(sizeof(openapi_spec_t));
    if (!spec) return NULL;
    
    spec->spec = json_object_new_object();
    spec->title = (char*)malloc(strlen(title) + 1);
    strcpy(spec->title, title);
    spec->version = (char*)malloc(strlen(version) + 1);
    strcpy(spec->version, version);
    spec->description = (char*)malloc(256);
    strcpy(spec->description, "Kubernetes-compatible API Server (Sirah)");
    spec->base_path = (char*)malloc(256);
    strcpy(spec->base_path, "");
    
    // Initialize OpenAPI structure
    json_object_object_add(spec->spec, "openapi", json_object_new_string("3.0.0"));
    
    json_object* info = json_object_new_object();
    json_object_object_add(info, "title", json_object_new_string(title));
    json_object_object_add(info, "version", json_object_new_string(version));
    json_object_object_add(info, "description", json_object_new_string(spec->description));
    json_object_object_add(spec->spec, "info", info);
    
    json_object* servers = json_object_new_array();
    json_object* server = json_object_new_object();
    json_object_object_add(server, "url", json_object_new_string("https://localhost:6443"));
    json_object_object_add(server, "description", json_object_new_string("API Server"));
    json_object_array_add(servers, server);
    json_object_object_add(spec->spec, "servers", servers);
    
    json_object_object_add(spec->spec, "paths", json_object_new_object());
    json_object_object_add(spec->spec, "components", json_object_new_object());
    
    json_object* components = json_object_object_get(spec->spec, "components");
    json_object_object_add(components, "schemas", json_object_new_object());
    
    return spec;
}

void openapi_spec_free(openapi_spec_t* spec) {
    if (!spec) return;
    if (spec->spec) json_object_put(spec->spec);
    free(spec->title);
    free(spec->version);
    free(spec->description);
    free(spec->base_path);
    free(spec);
}

int openapi_spec_set_server(openapi_spec_t* spec, const char* url, const char* description) {
    if (!spec || !url) return -1;
    
    json_object* servers = json_object_object_get(spec->spec, "servers");
    if (!servers) return -1;
    
    json_object* server = json_object_new_object();
    json_object_object_add(server, "url", json_object_new_string(url));
    json_object_object_add(server, "description", 
                          json_object_new_string(description ? description : "API Server"));
    json_object_array_add(servers, server);
    
    return 0;
}

int openapi_spec_add_path(openapi_spec_t* spec,
                          const char* path,
                          const char* method,
                          const char* summary,
                          const char* description,
                          const char* request_body_schema,
                          const char* response_schema) {
    if (!spec || !path || !method) return -1;
    
    json_object* paths = json_object_object_get(spec->spec, "paths");
    if (!paths) return -1;
    
    // Get or create path object
    json_object* path_obj = json_object_object_get(paths, path);
    if (!path_obj) {
        path_obj = json_object_new_object();
        json_object_object_add(paths, path, path_obj);
    }
    
    // Create operation object
    json_object* operation = json_object_new_object();
    if (summary) json_object_object_add(operation, "summary", json_object_new_string(summary));
    if (description) json_object_object_add(operation, "description", json_object_new_string(description));
    
    // Add parameters (basic - can be extended)
    json_object* parameters = json_object_new_array();
    json_object_object_add(operation, "parameters", parameters);
    
    // Add request body if schema provided
    if (request_body_schema) {
        json_object* req_body = json_object_new_object();
        json_object* content = json_object_new_object();
        json_object* media_type = json_object_new_object();
        json_object_object_add(media_type, "schema", 
                              json_object_new_string(request_body_schema));
        json_object_object_add(content, "application/json", media_type);
        json_object_object_add(req_body, "content", content);
        json_object_object_add(operation, "requestBody", req_body);
    }
    
    // Add responses
    json_object* responses = json_object_new_object();
    json_object* ok_resp = json_object_new_object();
    json_object_object_add(ok_resp, "description", json_object_new_string("Success"));
    if (response_schema) {
        json_object* content = json_object_new_object();
        json_object* media_type = json_object_new_object();
        json_object_object_add(media_type, "schema", 
                              json_object_new_string(response_schema));
        json_object_object_add(content, "application/json", media_type);
        json_object_object_add(ok_resp, "content", content);
    }
    json_object_object_add(responses, "200", ok_resp);
    json_object_object_add(operation, "responses", responses);
    
    // Add to path with method as key
    char method_lower[16];
    strcpy(method_lower, method);
    for (int i = 0; method_lower[i]; i++) method_lower[i] = tolower(method_lower[i]);
    json_object_object_add(path_obj, method_lower, operation);
    
    return 0;
}

int openapi_spec_add_schema(openapi_spec_t* spec,
                            const char* schema_name,
                            json_object* schema_def) {
    if (!spec || !schema_name || !schema_def) return -1;
    
    json_object* components = json_object_object_get(spec->spec, "components");
    if (!components) return -1;
    
    json_object* schemas = json_object_object_get(components, "schemas");
    if (!schemas) return -1;
    
    json_object_object_add(schemas, schema_name, schema_def);
    return 0;
}

char* openapi_spec_to_json(openapi_spec_t* spec) {
    if (!spec || !spec->spec) return NULL;
    return (char*)json_object_to_json_string_ext(spec->spec, JSON_C_TO_STRING_PRETTY);
}

// ============ Schema Builders ============

static json_object* schema_object_meta(void) {
    json_object* meta = json_object_new_object();
    json_object_object_add(meta, "type", json_object_new_string("object"));
    
    json_object* props = json_object_new_object();
    json_object_object_add(props, "name", json_object_new_object());
    json_object* name_type = json_object_object_get(props, "name");
    json_object_object_add(name_type, "type", json_object_new_string("string"));
    
    json_object_object_add(props, "namespace", json_object_new_object());
    json_object* ns_type = json_object_object_get(props, "namespace");
    json_object_object_add(ns_type, "type", json_object_new_string("string"));
    
    json_object_object_add(meta, "properties", props);
    return meta;
}

static json_object* schema_container_spec(void) {
    json_object* container = json_object_new_object();
    json_object_object_add(container, "type", json_object_new_string("object"));
    
    json_object* props = json_object_new_object();
    json_object_object_add(props, "name", json_object_new_object());
    json_object* name = json_object_object_get(props, "name");
    json_object_object_add(name, "type", json_object_new_string("string"));
    
    json_object_object_add(props, "image", json_object_new_object());
    json_object* image = json_object_object_get(props, "image");
    json_object_object_add(image, "type", json_object_new_string("string"));
    
    json_object_object_add(props, "ports", json_object_new_object());
    json_object* ports = json_object_object_get(props, "ports");
    json_object_object_add(ports, "type", json_object_new_string("array"));
    
    json_object_object_add(container, "properties", props);
    return container;
}

json_object* openapi_pod_schema(void) {
    json_object* pod = json_object_new_object();
    json_object_object_add(pod, "type", json_object_new_string("object"));
    json_object_object_add(pod, "description", json_object_new_string("Pod - minimal compute unit"));
    
    json_object* props = json_object_new_object();
    json_object_object_add(props, "apiVersion", json_object_new_object());
    json_object* api = json_object_object_get(props, "apiVersion");
    json_object_object_add(api, "type", json_object_new_string("string"));
    json_object_object_add(api, "default", json_object_new_string("v1"));
    
    json_object_object_add(props, "kind", json_object_new_object());
    json_object* kind = json_object_object_get(props, "kind");
    json_object_object_add(kind, "type", json_object_new_string("string"));
    json_object_object_add(kind, "default", json_object_new_string("Pod"));
    
    json_object_object_add(props, "metadata", schema_object_meta());
    json_object_object_add(props, "spec", json_object_new_object());
    json_object* spec = json_object_object_get(props, "spec");
    json_object_object_add(spec, "type", json_object_new_string("object"));
    json_object* spec_props = json_object_new_object();
    json_object_object_add(spec_props, "containers", json_object_new_object());
    json_object* containers = json_object_object_get(spec_props, "containers");
    json_object_object_add(containers, "type", json_object_new_string("array"));
    json_object_object_add(spec, "properties", spec_props);
    
    json_object_object_add(pod, "properties", props);
    return pod;
}

json_object* openapi_deployment_schema(void) {
    json_object* deploy = json_object_new_object();
    json_object_object_add(deploy, "type", json_object_new_string("object"));
    json_object_object_add(deploy, "description", json_object_new_string("Deployment - manages replicated pods"));
    return deploy;
}

json_object* openapi_service_schema(void) {
    json_object* svc = json_object_new_object();
    json_object_object_add(svc, "type", json_object_new_string("object"));
    json_object_object_add(svc, "description", json_object_new_string("Service - exposes pods as network service"));
    return svc;
}

json_object* openapi_statefulset_schema(void) {
    json_object* ss = json_object_new_object();
    json_object_object_add(ss, "type", json_object_new_string("object"));
    json_object_object_add(ss, "description", json_object_new_string("StatefulSet - manages stateful pods"));
    return ss;
}

json_object* openapi_job_schema(void) {
    json_object* job = json_object_new_object();
    json_object_object_add(job, "type", json_object_new_string("object"));
    json_object_object_add(job, "description", json_object_new_string("Job - runs one-off workloads"));
    return job;
}

json_object* openapi_cronjob_schema(void) {
    json_object* cj = json_object_new_object();
    json_object_object_add(cj, "type", json_object_new_string("object"));
    json_object_object_add(cj, "description", json_object_new_string("CronJob - runs scheduled jobs"));
    return cj;
}

json_object* openapi_configmap_schema(void) {
    json_object* cm = json_object_new_object();
    json_object_object_add(cm, "type", json_object_new_string("object"));
    json_object_object_add(cm, "description", json_object_new_string("ConfigMap - stores configuration data"));
    return cm;
}

json_object* openapi_secret_schema(void) {
    json_object* secret = json_object_new_object();
    json_object_object_add(secret, "type", json_object_new_string("object"));
    json_object_object_add(secret, "description", json_object_new_string("Secret - stores sensitive data"));
    return secret;
}

json_object* openapi_node_schema(void) {
    json_object* node = json_object_new_object();
    json_object_object_add(node, "type", json_object_new_string("object"));
    json_object_object_add(node, "description", json_object_new_string("Node - worker machine"));
    return node;
}

json_object* openapi_namespace_schema(void) {
    json_object* ns = json_object_new_object();
    json_object_object_add(ns, "type", json_object_new_string("object"));
    json_object_object_add(ns, "description", json_object_new_string("Namespace - logical cluster partitioning"));
    return ns;
}

// ============ Global OpenAPI Spec ============

openapi_spec_t* openapi_spec_global(void) {
    if (!g_openapi_spec) {
        g_openapi_spec = openapi_spec_new("Sirah Kubernetes API", "v1.28.0");
    }
    return g_openapi_spec;
}

int openapi_spec_rebuild(void) {
    if (!g_openapi_spec) {
        openapi_spec_global();
    }
    
    // Add all schemas
    openapi_spec_add_schema(g_openapi_spec, "Pod", openapi_pod_schema());
    openapi_spec_add_schema(g_openapi_spec, "Deployment", openapi_deployment_schema());
    openapi_spec_add_schema(g_openapi_spec, "Service", openapi_service_schema());
    openapi_spec_add_schema(g_openapi_spec, "StatefulSet", openapi_statefulset_schema());
    openapi_spec_add_schema(g_openapi_spec, "Job", openapi_job_schema());
    openapi_spec_add_schema(g_openapi_spec, "CronJob", openapi_cronjob_schema());
    openapi_spec_add_schema(g_openapi_spec, "ConfigMap", openapi_configmap_schema());
    openapi_spec_add_schema(g_openapi_spec, "Secret", openapi_secret_schema());
    openapi_spec_add_schema(g_openapi_spec, "Node", openapi_node_schema());
    openapi_spec_add_schema(g_openapi_spec, "Namespace", openapi_namespace_schema());
    
    // Add major API paths
    openapi_spec_add_path(g_openapi_spec, "/api/v1/namespaces/{namespace}/pods", "GET",
                         "List pods", "List all pods in a namespace",
                         NULL, "Pod");
    openapi_spec_add_path(g_openapi_spec, "/api/v1/namespaces/{namespace}/pods", "POST",
                         "Create pod", "Create a new pod",
                         "Pod", "Pod");
    openapi_spec_add_path(g_openapi_spec, "/api/v1/namespaces/{namespace}/pods/{name}", "GET",
                         "Get pod", "Retrieve a specific pod",
                         NULL, "Pod");
    openapi_spec_add_path(g_openapi_spec, "/api/v1/namespaces/{namespace}/pods/{name}", "DELETE",
                         "Delete pod", "Delete a specific pod",
                         NULL, "Pod");
    
    return 0;
}

int openapi_handle_request(const char* path, char* response_buffer, int* response_code) {
    if (!path || !response_buffer) return -1;
    
    // GET /openapi/v2 - Return Swagger 2.0 spec (for kubectl compatibility)
    // Note: kubectl asks for protobuf format, but we only serve JSON
    // kubectl will fallback to discovery if we return 406
    if (strcmp(path, "/openapi/v2") == 0) {
        // Return JSON - kubectl will fall back to discovery if it needs protobuf
        // Minimal Swagger 2.0 spec that kubectl can use
        const char* swagger_v2 = "{"
            "\"swagger\":\"2.0\","
            "\"info\":{"
                "\"title\":\"Sirah Kubernetes API\","
                "\"version\":\"v1.28.0\""
            "},"
            "\"paths\":{"
                "\"/api/v1/namespaces/{namespace}/pods\":{"
                    "\"get\":{\"operationId\":\"list_pods\",\"responses\":{\"200\":{\"description\":\"OK\"}}},"
                    "\"post\":{\"operationId\":\"create_pod\",\"responses\":{\"201\":{\"description\":\"Created\"}}}"
                "},"
                "\"/api/v1/namespaces/{namespace}/pods/{name}\":{"
                    "\"get\":{\"operationId\":\"get_pod\",\"responses\":{\"200\":{\"description\":\"OK\"}}},"
                    "\"patch\":{\"operationId\":\"patch_pod\",\"responses\":{\"200\":{\"description\":\"OK\"}}},"
                    "\"delete\":{\"operationId\":\"delete_pod\",\"responses\":{\"204\":{\"description\":\"No Content\"}}}"
                "},"
                "\"/api/v1/namespaces/{namespace}/services\":{"
                    "\"get\":{\"operationId\":\"list_services\",\"responses\":{\"200\":{\"description\":\"OK\"}}},"
                    "\"post\":{\"operationId\":\"create_service\",\"responses\":{\"201\":{\"description\":\"Created\"}}}"
                "},"
                "\"/api/v1/namespaces/{namespace}/services/{name}\":{"
                    "\"get\":{\"operationId\":\"get_service\",\"responses\":{\"200\":{\"description\":\"OK\"}}},"
                    "\"patch\":{\"operationId\":\"patch_service\",\"responses\":{\"200\":{\"description\":\"OK\"}}},"
                    "\"delete\":{\"operationId\":\"delete_service\",\"responses\":{\"204\":{\"description\":\"No Content\"}}}"
                "},"
                "\"/api/v1/namespaces/{namespace}/configmaps\":{"
                    "\"get\":{\"operationId\":\"list_configmaps\",\"responses\":{\"200\":{\"description\":\"OK\"}}},"
                    "\"post\":{\"operationId\":\"create_configmap\",\"responses\":{\"201\":{\"description\":\"Created\"}}}"
                "},"
                "\"/api/v1/namespaces/{namespace}/configmaps/{name}\":{"
                    "\"get\":{\"operationId\":\"get_configmap\",\"responses\":{\"200\":{\"description\":\"OK\"}}},"
                    "\"patch\":{\"operationId\":\"patch_configmap\",\"responses\":{\"200\":{\"description\":\"OK\"}}},"
                    "\"delete\":{\"operationId\":\"delete_configmap\",\"responses\":{\"204\":{\"description\":\"No Content\"}}}"
                "}"
            "},"
            "\"definitions\":{},"
            "\"securityDefinitions\":{}"
        "}";
        
        strncpy(response_buffer, swagger_v2, 16383);
        *response_code = 200;
        return 0;
    }
    
    // GET /openapi/v3 - Return full OpenAPI spec
    if (strcmp(path, "/openapi/v3") == 0) {
        openapi_spec_t* spec = openapi_spec_global();
        if (!spec) {
            strcpy(response_buffer, "{\"error\":\"OpenAPI spec not initialized\"}");
            *response_code = 500;
            return -1;
        }
        
        char* json_str = openapi_spec_to_json(spec);
        if (!json_str) {
            strcpy(response_buffer, "{\"error\":\"Failed to serialize OpenAPI spec\"}");
            *response_code = 500;
            return -1;
        }
        
        strncpy(response_buffer, json_str, 16383);
        *response_code = 200;
        return 0;
    }
    
    // GET /docs - Return Swagger UI HTML
    if (strcmp(path, "/docs") == 0 || strcmp(path, "/docs/") == 0) {
        const char* swagger_ui = R"(
<!DOCTYPE html>
<html>
<head>
    <title>Sirah API Documentation</title>
    <meta charset="utf-8"/>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <link rel="stylesheet" href="https://fonts.googleapis.com/css?family=Roboto:300,400,500,700,400italic">
    <style>
        body { margin: 0; padding: 0; font-family: Roboto, sans-serif; }
        .swagger-ui { max-width: 100%; }
    </style>
</head>
<body>
    <div id="swagger-ui"></div>
    <script src="https://cdn.jsdelivr.net/npm/swagger-ui-dist@3/swagger-ui-bundle.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/swagger-ui-dist@3/swagger-ui-standalone-preset.js"></script>
    <script>
        SwaggerUIBundle({
            url: "/openapi/v3",
            dom_id: '#swagger-ui',
            presets: [
                SwaggerUIBundle.presets.apis,
                SwaggerUIStandalonePreset
            ],
            layout: "StandaloneLayout"
        });
    </script>
</body>
</html>
        )";
        strncpy(response_buffer, swagger_ui, 16383);
        *response_code = 200;
        return 0;
    }
    
    *response_code = 404;
    strcpy(response_buffer, "{\"error\":\"OpenAPI endpoint not found\"}");
    return -1;
}
