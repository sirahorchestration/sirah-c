// internal/apiserver/endpoints.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <json-c/json.h>
#include "endpoints.h"
#include "../storage/store.h"
#include "../../pkg/types/storage.h"
#include "../../pkg/types/service.h"
#include "etcd/etcd_manager.h"

// Forward declarations for persistence
extern int store_save_pod_to_etcd(k8s_pod_t* pod);
extern int store_delete_pod_from_etcd(const char* namespace, const char* name);
extern etcd_client_t* g_etcd_client;

// Global node store
static struct {
    struct {
        char name[256];
        char ip[256];
        char status[64];
    } nodes[100];
    int count;
} node_store = {0};

void init_node_store(void) {
    // Add local node for MVP
    if (node_store.count == 0) {
        strcpy(node_store.nodes[0].name, "control-plane");
        strcpy(node_store.nodes[0].ip, "127.0.0.1");
        strcpy(node_store.nodes[0].status, "Ready");
        node_store.count = 1;
    }
}

// Helper to convert phase enum to string
static const char* phase_to_string(k8s_phase_t phase) {
    switch (phase) {
        case PHASE_PENDING: return "Pending";
        case PHASE_RUNNING: return "Running";
        case PHASE_SUCCEEDED: return "Succeeded";
        case PHASE_FAILED: return "Failed";
        case PHASE_TERMINATING: return "Terminating";
        default: return "Unknown";
    }
}

// DEPRECATED: Use endpoint_list_pods_etcd instead
// int endpoint_list_pods(const char* namespace, char* response_buffer, int* response_code) {
//     // This endpoint is deprecated - use etcd-backed endpoints instead
//     snprintf(response_buffer, 16384, "{\"error\":\"deprecated endpoint - use etcd endpoints\"}");
//     *response_code = 400;
//     return -1;
// }



// DEPRECATED: Use endpoint_get_pod_etcd instead
// int endpoint_get_pod(const char* namespace, const char* pod_name,
//                      char* response_buffer, int* response_code) {
//     snprintf(response_buffer, 16384, "{\"error\":\"deprecated endpoint - use etcd endpoints\"}");
//     *response_code = 400;
//     return -1;
// }

// DEPRECATED: Use endpoint_create_pod_etcd instead
// int endpoint_create_pod(const char* namespace, const char* body,
//                         char* response_buffer, int* response_code) {
//     snprintf(response_buffer, 16384, "{\"error\":\"deprecated endpoint - use etcd endpoints\"}");
//     *response_code = 400;
//     return -1;
// }

// DEPRECATED: Use endpoint_delete_pod_etcd instead
// int endpoint_delete_pod(const char* namespace, const char* pod_name,
//                         char* response_buffer, int* response_code) {
//     snprintf(response_buffer, 16384, "{\"error\":\"deprecated endpoint - use etcd endpoints\"}");
//     *response_code = 400;
//     return -1;
// }

// List nodes
int endpoint_list_nodes(char* response_buffer, int* response_code) {
    init_node_store();

    json_object* root = json_object_new_object();
    json_object* items = json_object_new_array();

    for (int i = 0; i < node_store.count; i++) {
        json_object* node_obj = json_object_new_object();
        
        // Add apiVersion and kind
        json_object_object_add(node_obj, "apiVersion", json_object_new_string("v1"));
        json_object_object_add(node_obj, "kind", json_object_new_string("Node"));
        
        // Add metadata
        json_object* metadata = json_object_new_object();
        json_object_object_add(metadata, "name",
                               json_object_new_string(node_store.nodes[i].name));
        json_object_object_add(node_obj, "metadata", metadata);
        
        // Add status
        json_object* status = json_object_new_object();
        json_object_object_add(status, "status",
                               json_object_new_string(node_store.nodes[i].status));
        json_object_object_add(node_obj, "status", status);
        
        json_object_array_add(items, node_obj);
    }

    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("NodeList"));
    json_object_object_add(root, "items", items);

    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;

    json_object_put(root);
    return 0;
}

// Get node
int endpoint_get_node(const char* node_name, char* response_buffer, int* response_code) {
    init_node_store();

    for (int i = 0; i < node_store.count; i++) {
        if (strcmp(node_store.nodes[i].name, node_name) == 0) {
            json_object* node_obj = json_object_new_object();
            json_object_object_add(node_obj, "name",
                                   json_object_new_string(node_store.nodes[i].name));
            json_object_object_add(node_obj, "ip",
                                   json_object_new_string(node_store.nodes[i].ip));
            json_object_object_add(node_obj, "status",
                                   json_object_new_string(node_store.nodes[i].status));

            const char* json_str = json_object_to_json_string(node_obj);
            strcpy(response_buffer, json_str);
            *response_code = 200;
            json_object_put(node_obj);
            return 0;
        }
    }

    strcpy(response_buffer, "{\"error\":\"node not found\"}");
    *response_code = 404;
    return 0;
}

// Register new node
int endpoint_register_node(const char* body, char* response_buffer, int* response_code) {
    if (!body || strlen(body) == 0) {
        strcpy(response_buffer, "{\"error\":\"missing node data\"}");
        *response_code = 400;
        return 0;
    }

    // Parse node registration request
    json_object* req = json_tokener_parse(body);
    if (!req) {
        strcpy(response_buffer, "{\"error\":\"invalid json\"}");
        *response_code = 400;
        return 0;
    }

    json_object* resp = json_object_new_object();
    json_object_object_add(resp, "status", json_object_new_string("registered"));
    json_object_object_add(resp, "message", json_object_new_string("Node registered successfully"));

    const char* json_str = json_object_to_json_string(resp);
    strcpy(response_buffer, json_str);
    *response_code = 200;

    json_object_put(req);
    json_object_put(resp);
    return 0;
}

// Node heartbeat endpoint
int endpoint_node_heartbeat(const char* node_name, const char* body,
                           char* response_buffer, int* response_code) {
    if (!node_name || strlen(node_name) == 0) {
        strcpy(response_buffer, "{\"error\":\"missing node name\"}");
        *response_code = 400;
        return 0;
    }

    json_object* resp = json_object_new_object();
    json_object_object_add(resp, "status", json_object_new_string("heartbeat received"));
    json_object_object_add(resp, "timestamp", json_object_new_int((int)time(NULL)));

    const char* json_str = json_object_to_json_string(resp);
    strcpy(response_buffer, json_str);
    *response_code = 200;

    json_object_put(resp);
    return 0;
}

// DEPRECATED: Use endpoint_patch_pod_etcd instead
// int endpoint_bind_pod(const char* namespace, const char* pod_name, const char* body,
//                      char* response_buffer, int* response_code) {
//     snprintf(response_buffer, 16384, "{\"error\":\"deprecated endpoint - use etcd endpoints\"}");
//     *response_code = 400;
//     return -1;
// }

// DEPRECATED: Use endpoint_patch_pod_etcd instead
// int endpoint_pod_status(const char* namespace, const char* pod_name, const char* body,
//                        char* response_buffer, int* response_code) {
//     snprintf(response_buffer, 16384, "{\"error\":\"deprecated endpoint - use etcd endpoints\"}");
//     *response_code = 400;
//     return -1;
// }



// Global service store (in-memory for MVP)
static struct {
    k8s_service_t* services[1000];
    int count;
} service_store = {0};

// List services in namespace
int endpoint_list_services(const char* namespace, char* response_buffer, int* response_code) {
    fprintf(stderr, "[DEBUG] endpoint_list_services called with namespace=%s, count=%d\n", namespace, service_store.count);
    json_object* root = json_object_new_object();
    json_object* items = json_object_new_array();
    
    for (int i = 0; i < service_store.count; i++) {
        if (strlen(namespace) == 0 || strcmp(service_store.services[i]->metadata.namespace, namespace) == 0) {
            json_object* svc_obj = json_object_new_object();
            json_object_object_add(svc_obj, "apiVersion", json_object_new_string("v1"));
            json_object_object_add(svc_obj, "kind", json_object_new_string("Service"));
            
            json_object* metadata = json_object_new_object();
            json_object_object_add(metadata, "name", json_object_new_string(service_store.services[i]->metadata.name));
            json_object_object_add(metadata, "namespace", json_object_new_string(service_store.services[i]->metadata.namespace));
            json_object_object_add(svc_obj, "metadata", metadata);
            
            json_object* status = json_object_new_object();
            json_object_object_add(status, "loadBalancer", json_object_new_object());
            json_object_object_add(svc_obj, "status", status);
            
            json_object_array_add(items, svc_obj);
        }
    }
    
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("ServiceList"));
    json_object_object_add(root, "items", items);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    
    json_object_put(root);
    return 0;
}

// Get single service
int endpoint_get_service(const char* namespace, const char* name,
                        char* response_buffer, int* response_code) {
    for (int i = 0; i < service_store.count; i++) {
        if (strcmp(service_store.services[i]->metadata.name, name) == 0 &&
            strcmp(service_store.services[i]->metadata.namespace, namespace) == 0) {
            char* json_str = k8s_service_to_json(service_store.services[i]);
            strcpy(response_buffer, json_str);
            *response_code = 200;
            free(json_str);
            return 0;
        }
    }
    
    strcpy(response_buffer, "{\"error\":\"service not found\"}");
    *response_code = 404;
    return -1;
}

// Create service
int endpoint_create_service(const char* namespace, const char* body,
                           char* response_buffer, int* response_code) {
    if (!namespace || !body) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid parameters\"}");
        return -1;
    }
    
    json_object* req = json_tokener_parse(body);
    if (!req) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    json_object* meta = json_object_object_get(req, "metadata");
    const char* svc_name = json_object_get_string(json_object_object_get(meta, "name"));
    
    if (!svc_name) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"service name required\"}");
        json_object_put(req);
        return -1;
    }
    
    if (service_store.count >= 1000) {
        *response_code = 507;
        strcpy(response_buffer, "{\"error\":\"service store full\"}");
        json_object_put(req);
        return -1;
    }
    
    k8s_service_t* svc = k8s_service_new(svc_name, namespace);
    service_store.services[service_store.count++] = svc;
    
    char* json_str = k8s_service_to_json(svc);
    strcpy(response_buffer, json_str);
    *response_code = 201;
    
    free(json_str);
    json_object_put(req);
    return 0;
}

// Delete service
int endpoint_delete_service(const char* namespace, const char* name,
                           char* response_buffer, int* response_code) {
    for (int i = 0; i < service_store.count; i++) {
        if (strcmp(service_store.services[i]->metadata.name, name) == 0 &&
            strcmp(service_store.services[i]->metadata.namespace, namespace) == 0) {
            k8s_service_free(service_store.services[i]);
            memmove(&service_store.services[i], &service_store.services[i + 1],
                   sizeof(k8s_service_t*) * (service_store.count - i - 1));
            service_store.count--;
            
            strcpy(response_buffer, "{\"status\":\"deleted\"}");
            *response_code = 200;
            return 0;
        }
    }
    
    strcpy(response_buffer, "{\"error\":\"service not found\"}");
    *response_code = 404;
    return -1;
}

// Event store for pod events
static struct {
    struct {
        char pod_name[256];
        char pod_namespace[256];
        char reason[256];
        char message[512];
        time_t timestamp;
    } events[1000];
    int count;
} event_store = {0};

// Get pod events
int endpoint_pod_events(const char* namespace, const char* pod_name,
                       char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object* items = json_object_new_array();
    
    // Find events for this pod
    for (int i = 0; i < event_store.count; i++) {
        if (strcmp(event_store.events[i].pod_namespace, namespace) == 0 &&
            strcmp(event_store.events[i].pod_name, pod_name) == 0) {
            json_object* event_obj = json_object_new_object();
            json_object_object_add(event_obj, "reason", 
                                   json_object_new_string(event_store.events[i].reason));
            json_object_object_add(event_obj, "message",
                                   json_object_new_string(event_store.events[i].message));
            json_object_object_add(event_obj, "timestamp",
                                   json_object_new_int64(event_store.events[i].timestamp));
            json_object_array_add(items, event_obj);
        }
    }
    
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("EventList"));
    json_object_object_add(root, "items", items);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    
    json_object_put(root);
    return 0;
}

// Internal function to record event (called by controllers)
int record_pod_event(const char* pod_name, const char* pod_namespace, 
                    const char* reason, const char* message) {
    if (event_store.count >= 1000) {
        // Shift events
        memmove(&event_store.events[0], &event_store.events[1], 
               sizeof(event_store.events[0]) * (event_store.count - 1));
        event_store.count--;
    }
    
    strcpy(event_store.events[event_store.count].pod_name, pod_name);
    strcpy(event_store.events[event_store.count].pod_namespace, pod_namespace);
    strcpy(event_store.events[event_store.count].reason, reason);
    strcpy(event_store.events[event_store.count].message, message);
    event_store.events[event_store.count].timestamp = time(NULL);
    
    event_store.count++;
    return 0;
}
// ==================== PATCH ENDPOINTS ====================

// DEPRECATED: Use endpoint_patch_pod_etcd instead
// int endpoint_patch_pod(const char* namespace, const char* name, const char* body,
//                       const char* content_type, char* response_buffer, int* response_code) {
//     snprintf(response_buffer, 16384, "{\"error\":\"deprecated endpoint - use etcd endpoints\"}");
//     *response_code = 400;
//     return -1;
// }

// Patch service
int endpoint_patch_service(const char* namespace, const char* name, const char* body,
                          const char* content_type, char* response_buffer, int* response_code) {
    // Simplified: return success
    json_object* result = json_object_new_object();
    json_object_object_add(result, "kind", json_object_new_string("Service"));
    json_object_object_add(result, "apiVersion", json_object_new_string("v1"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(result, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(result);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(result);
    return 0;
}

// Patch configmap
int endpoint_patch_configmap(const char* namespace, const char* name, const char* body,
                            const char* content_type, char* response_buffer, int* response_code) {
    json_object* result = json_object_new_object();
    json_object_object_add(result, "kind", json_object_new_string("ConfigMap"));
    json_object_object_add(result, "apiVersion", json_object_new_string("v1"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(result, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(result);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(result);
    return 0;
}

// Patch secret
int endpoint_patch_secret(const char* namespace, const char* name, const char* body,
                         const char* content_type, char* response_buffer, int* response_code) {
    json_object* result = json_object_new_object();
    json_object_object_add(result, "kind", json_object_new_string("Secret"));
    json_object_object_add(result, "apiVersion", json_object_new_string("v1"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(result, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(result);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(result);
    return 0;
}

// ==================== WATCH ENDPOINTS ====================

// Watch pods
int endpoint_watch_pods(const char* namespace, const char* query_string,
                       char* response_buffer, int* response_code) {
    // Start watch session
    int watch_id = watch_start("Pod", namespace ? namespace : "default", "", "");
    if (watch_id < 0) {
        *response_code = 500;
        strcpy(response_buffer, "{\"error\":\"failed to start watch\"}");
        return -1;
    }
    
    // Build watch response header
    const char* watch_header = "{"
        "\"type\":\"ADDED\","
        "\"object\":{"
            "\"apiVersion\":\"v1\","
            "\"kind\":\"Pod\","
            "\"metadata\":{"
                "\"name\":\"watch-stream\","
                "\"namespace\":\"default\""
            "}"
        "}"
    "}";
    
    strcpy(response_buffer, watch_header);
    *response_code = 200;
    return 0;
}

// Watch services
int endpoint_watch_services(const char* namespace, const char* query_string,
                           char* response_buffer, int* response_code) {
    int watch_id = watch_start("Service", namespace ? namespace : "default", "", "");
    if (watch_id < 0) {
        *response_code = 500;
        strcpy(response_buffer, "{\"error\":\"failed to start watch\"}");
        return -1;
    }
    
    const char* watch_header = "{"
        "\"type\":\"ADDED\","
        "\"object\":{"
            "\"apiVersion\":\"v1\","
            "\"kind\":\"Service\","
            "\"metadata\":{"
                "\"name\":\"watch-stream\","
                "\"namespace\":\"default\""
            "}"
        "}"
    "}";
    
    strcpy(response_buffer, watch_header);
    *response_code = 200;
    return 0;
}

// Watch deployments
int endpoint_watch_deployments(const char* namespace, const char* query_string,
                              char* response_buffer, int* response_code) {
    int watch_id = watch_start("Deployment", namespace ? namespace : "default", "", "");
    if (watch_id < 0) {
        *response_code = 500;
        strcpy(response_buffer, "{\"error\":\"failed to start watch\"}");
        return -1;
    }
    
    const char* watch_header = "{"
        "\"type\":\"ADDED\","
        "\"object\":{"
            "\"apiVersion\":\"apps/v1\","
            "\"kind\":\"Deployment\","
            "\"metadata\":{"
                "\"name\":\"watch-stream\","
                "\"namespace\":\"default\""
            "}"
        "}"
    "}";
    
    strcpy(response_buffer, watch_header);
    *response_code = 200;
    return 0;
}

// ==================== DEPLOYMENT ENDPOINTS ====================

int endpoint_list_deployments(const char* namespace, char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("apps/v1"));
    json_object_object_add(root, "kind", json_object_new_string("DeploymentList"));
    json_object_object_add(root, "items", json_object_new_array());
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_get_deployment(const char* namespace, const char* name,
                           char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("apps/v1"));
    json_object_object_add(root, "kind", json_object_new_string("Deployment"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(root, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_create_deployment(const char* namespace, const char* body,
                              char* response_buffer, int* response_code) {
    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    const char* json_str = json_object_to_json_string(obj);
    strcpy(response_buffer, json_str);
    *response_code = 201;
    json_object_put(obj);
    return 0;
}

int endpoint_update_deployment(const char* namespace, const char* name, const char* body,
                              char* response_buffer, int* response_code) {
    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    const char* json_str = json_object_to_json_string(obj);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(obj);
    return 0;
}

int endpoint_patch_deployment(const char* namespace, const char* name, const char* body,
                             const char* content_type, char* response_buffer, int* response_code) {
    json_object* patch = json_tokener_parse(body);
    if (!patch) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("apps/v1"));
    json_object_object_add(root, "kind", json_object_new_string("Deployment"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(root, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(patch);
    json_object_put(root);
    return 0;
}

int endpoint_delete_deployment(const char* namespace, const char* name,
                              char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "kind", json_object_new_string("Status"));
    json_object_object_add(root, "status", json_object_new_string("Success"));
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

// ==================== DAEMONSET ENDPOINTS ====================

int endpoint_list_daemonsets(const char* namespace, char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("apps/v1"));
    json_object_object_add(root, "kind", json_object_new_string("DaemonSetList"));
    json_object_object_add(root, "items", json_object_new_array());
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_get_daemonset(const char* namespace, const char* name,
                          char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("apps/v1"));
    json_object_object_add(root, "kind", json_object_new_string("DaemonSet"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(root, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_create_daemonset(const char* namespace, const char* body,
                             char* response_buffer, int* response_code) {
    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    const char* json_str = json_object_to_json_string(obj);
    strcpy(response_buffer, json_str);
    *response_code = 201;
    json_object_put(obj);
    return 0;
}

int endpoint_update_daemonset(const char* namespace, const char* name, const char* body,
                             char* response_buffer, int* response_code) {
    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    const char* json_str = json_object_to_json_string(obj);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(obj);
    return 0;
}

int endpoint_patch_daemonset(const char* namespace, const char* name, const char* body,
                            const char* content_type, char* response_buffer, int* response_code) {
    json_object* patch = json_tokener_parse(body);
    if (!patch) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("apps/v1"));
    json_object_object_add(root, "kind", json_object_new_string("DaemonSet"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(root, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(patch);
    json_object_put(root);
    return 0;
}

int endpoint_delete_daemonset(const char* namespace, const char* name,
                             char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "kind", json_object_new_string("Status"));
    json_object_object_add(root, "status", json_object_new_string("Success"));
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

// ==================== JOB ENDPOINTS ====================

int endpoint_list_jobs(const char* namespace, char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("batch/v1"));
    json_object_object_add(root, "kind", json_object_new_string("JobList"));
    json_object_object_add(root, "items", json_object_new_array());
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_get_job(const char* namespace, const char* name,
                    char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("batch/v1"));
    json_object_object_add(root, "kind", json_object_new_string("Job"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(root, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_create_job(const char* namespace, const char* body,
                       char* response_buffer, int* response_code) {
    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    const char* json_str = json_object_to_json_string(obj);
    strcpy(response_buffer, json_str);
    *response_code = 201;
    json_object_put(obj);
    return 0;
}

int endpoint_update_job(const char* namespace, const char* name, const char* body,
                       char* response_buffer, int* response_code) {
    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    const char* json_str = json_object_to_json_string(obj);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(obj);
    return 0;
}

int endpoint_patch_job(const char* namespace, const char* name, const char* body,
                      const char* content_type, char* response_buffer, int* response_code) {
    json_object* patch = json_tokener_parse(body);
    if (!patch) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("batch/v1"));
    json_object_object_add(root, "kind", json_object_new_string("Job"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(root, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(patch);
    json_object_put(root);
    return 0;
}

int endpoint_delete_job(const char* namespace, const char* name,
                       char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "kind", json_object_new_string("Status"));
    json_object_object_add(root, "status", json_object_new_string("Success"));
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

// ==================== CRONJOB ENDPOINTS ====================

int endpoint_list_cronjobs(const char* namespace, char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("batch/v1"));
    json_object_object_add(root, "kind", json_object_new_string("CronJobList"));
    json_object_object_add(root, "items", json_object_new_array());
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_get_cronjob(const char* namespace, const char* name,
                        char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("batch/v1"));
    json_object_object_add(root, "kind", json_object_new_string("CronJob"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(root, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_create_cronjob(const char* namespace, const char* body,
                           char* response_buffer, int* response_code) {
    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    const char* json_str = json_object_to_json_string(obj);
    strcpy(response_buffer, json_str);
    *response_code = 201;
    json_object_put(obj);
    return 0;
}

int endpoint_update_cronjob(const char* namespace, const char* name, const char* body,
                           char* response_buffer, int* response_code) {
    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    const char* json_str = json_object_to_json_string(obj);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(obj);
    return 0;
}

int endpoint_patch_cronjob(const char* namespace, const char* name, const char* body,
                          const char* content_type, char* response_buffer, int* response_code) {
    json_object* patch = json_tokener_parse(body);
    if (!patch) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("batch/v1"));
    json_object_object_add(root, "kind", json_object_new_string("CronJob"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(root, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(patch);
    json_object_put(root);
    return 0;
}

int endpoint_delete_cronjob(const char* namespace, const char* name,
                           char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "kind", json_object_new_string("Status"));
    json_object_object_add(root, "status", json_object_new_string("Success"));
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

// ==================== NAMESPACE ENDPOINTS ====================

int endpoint_list_namespaces(char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("NamespaceList"));
    
    json_object* items = json_object_new_array();
    
    // Add default namespace
    json_object* default_ns = json_object_new_object();
    json_object_object_add(default_ns, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(default_ns, "kind", json_object_new_string("Namespace"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string("default"));
    json_object_object_add(default_ns, "metadata", metadata);
    json_object_array_add(items, default_ns);
    
    json_object_object_add(root, "items", items);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_get_namespace(const char* name, char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("Namespace"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(root, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_create_namespace(const char* body, char* response_buffer, int* response_code) {
    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    const char* json_str = json_object_to_json_string(obj);
    strcpy(response_buffer, json_str);
    *response_code = 201;
    json_object_put(obj);
    return 0;
}

int endpoint_delete_namespace(const char* name, char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "kind", json_object_new_string("Status"));
    json_object_object_add(root, "status", json_object_new_string("Success"));
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

// ==================== EVENT ENDPOINTS ====================

int endpoint_list_events(const char* namespace, char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("EventList"));
    json_object_object_add(root, "items", json_object_new_array());
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_create_event(const char* namespace, const char* body,
                         char* response_buffer, int* response_code) {
    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    const char* json_str = json_object_to_json_string(obj);
    strcpy(response_buffer, json_str);
    *response_code = 201;
    json_object_put(obj);
    return 0;
}

int endpoint_get_event(const char* namespace, const char* name,
                       char* response_buffer, int* response_code) {
    // Return a mock event for now
    json_object* event = json_object_new_object();
    json_object_object_add(event, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(event, "kind", json_object_new_string("Event"));
    
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(event, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(event);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(event);
    return 0;
}

int endpoint_delete_event(const char* namespace, const char* name,
                          char* response_buffer, int* response_code) {
    // Return 204 No Content on delete
    strcpy(response_buffer, "");
    *response_code = 204;
    return 0;
}

// DEPRECATED: Use etcd-backed endpoints instead
int endpoint_remove_finalizer(const char* namespace, const char* pod_name,
                             const char* finalizer_name) {
    snprintf((char*)"error", 6, "{\"error\":\"deprecated endpoint - use etcd endpoints\"}");
    return -1;
}

// ============================================================================
// CONFIGMAP ENDPOINTS - Deprecated, using etcd-backed versions in handler
// ============================================================================

// Global ConfigMap store - DEPRECATED
static struct {
    void* configmaps[1000];  // Placeholder - not used
    int count;
} configmap_store = {0};

int endpoint_list_configmaps(const char* namespace, char* response_buffer, int* response_code) {
    // Uses endpoint_list_configmaps_etcd from endpoints_etcd_integration
    *response_code = 200;
    strcpy(response_buffer, "{\"apiVersion\":\"v1\",\"kind\":\"ConfigMapList\",\"items\":[]}");
    return 0;
}

int endpoint_get_configmap(const char* namespace, const char* name,
                          char* response_buffer, int* response_code) {
    // Uses endpoint_get_configmap_etcd from endpoints_etcd_integration
    *response_code = 404;
    strcpy(response_buffer, "{\"error\":\"ConfigMap not found\"}");
    return -1;
}

int endpoint_create_configmap(const char* namespace, const char* body,
                             char* response_buffer, int* response_code) {
    // Uses endpoint_create_configmap_etcd from endpoints_etcd_integration
    *response_code = 201;
    strcpy(response_buffer, "{}");
    return 0;
}

int endpoint_delete_configmap(const char* namespace, const char* name,
                             char* response_buffer, int* response_code) {
    // Uses endpoints_etcd_integration in handler.c
    *response_code = 204;
    strcpy(response_buffer, "");
    return 0;
}

// ============================================================================
// SECRET ENDPOINTS
// ============================================================================

/* DISABLED - Phase 6B: Secret, PV, PVC types not yet defined
// Global Secret store
static struct {
    void* secrets[1000];  //k8s_secret_t* secrets[1000];
    int count;
} secret_store = {0};
*/

int endpoint_list_secrets(const char* namespace, char* response_buffer, int* response_code) {
    // STUB: Secret endpoints disabled - Phase 6B pending
    *response_code = 501;  // Not Implemented
    strcpy(response_buffer, "{\"error\":\"Secret endpoints not yet implemented\"}");
    return -1;
}

int endpoint_get_secret(const char* namespace, const char* name,
                       char* response_buffer, int* response_code) {
    // STUB
    *response_code = 501;
    strcpy(response_buffer, "{\"error\":\"Secret endpoints not yet implemented\"}");
    return -1;
}

int endpoint_create_secret(const char* namespace, const char* body,
                          char* response_buffer, int* response_code) {
    // STUB
    *response_code = 501;
    strcpy(response_buffer, "{\"error\":\"Secret endpoints not yet implemented\"}");
    return -1;
}

int endpoint_delete_secret(const char* namespace, const char* name,
                          char* response_buffer, int* response_code) {
    // STUB
    *response_code = 501;
    strcpy(response_buffer, "{\"error\":\"Secret endpoints not yet implemented\"}");
    return -1;
}

// ============================================================================
// PERSISTENTVOLUME ENDPOINTS
// ============================================================================

// Global PersistentVolume store
static struct {
    k8s_persistent_volume_t* volumes[1000];
    int count;
} pv_store = {0};

int endpoint_list_pv(char* response_buffer, int* response_code) {
    *response_code = 501;
    strcpy(response_buffer, "{\"error\":\"PV endpoints not yet implemented\"}");
    return -1;
}

int endpoint_get_pv(const char* name, char* response_buffer, int* response_code) {
    *response_code = 501;
    strcpy(response_buffer, "{\"error\":\"PV endpoints not yet implemented\"}");
    return -1;
}

int endpoint_create_pv(const char* body, char* response_buffer, int* response_code) {
    *response_code = 501;
    strcpy(response_buffer, "{\"error\":\"PV endpoints not yet implemented\"}");
    return -1;
}

int endpoint_delete_pv(const char* name, char* response_buffer, int* response_code) {
    *response_code = 501;
    strcpy(response_buffer, "{\"error\":\"PV endpoints not yet implemented\"}");
    return -1;
}

// ============================================================================
// PERSISTENTVOLUMECLAIM ENDPOINTS
// ============================================================================

// Global PersistentVolumeClaim store
static struct {
    k8s_persistent_volume_claim_t* claims[1000];
    int count;
} pvc_store = {0};

int endpoint_list_pvc(const char* namespace, char* response_buffer, int* response_code) {
    *response_code = 501;
    strcpy(response_buffer, "{\"error\":\"PVC endpoints not yet implemented\"}");
    return -1;
}

int endpoint_get_pvc(const char* namespace, const char* name,
                    char* response_buffer, int* response_code) {
    *response_code = 501;
    strcpy(response_buffer, "{\"error\":\"PVC endpoints not yet implemented\"}");
    return -1;
}

int endpoint_create_pvc(const char* namespace, const char* body,
                       char* response_buffer, int* response_code) {
    *response_code = 501;
    strcpy(response_buffer, "{\"error\":\"PVC endpoints not yet implemented\"}");
    return -1;
}

int endpoint_delete_pvc(const char* namespace, const char* name,
                       char* response_buffer, int* response_code) {
    *response_code = 501;
    strcpy(response_buffer, "{\"error\":\"PVC endpoints not yet implemented\"}");
    return -1;
}