// internal/controller/daemonset_controller.c
// DaemonSet controller implementation (Phase 5)
// Run pod on every node

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <json-c/json.h>
#include "daemonset_controller.h"
#include "../../internal/etcd/etcd_manager.h"
#include "../../pkg/types/pod.h"

#define SYNC_INTERVAL 5  // Reconciliation every 5 seconds
#define MAX_NODES 256

static const char* api_server_url = NULL;
static int controller_running = 0;

// ============================================================================
// Node Operations
// ============================================================================

static int daemonset_get_all_nodes(char** node_names, int max_nodes) {
    etcd_response_t resp = {0};
    int status = etcd_manager_list("/sirah/nodes/", &resp);
    
    int count = 0;
    if (status == ETCD_OK && resp.kvs_count > 0) {
        for (int i = 0; i < resp.kvs_count && count < max_nodes; i++) {
            // Extract node name from key: /sirah/nodes/{name}
            const char* key = resp.kvs_keys[i];
            const char* node_name = strstr(key, "/sirah/nodes/") + strlen("/sirah/nodes/");
            
            node_names[count] = malloc(256);
            strncpy(node_names[count], node_name, 255);
            count++;
        }
    }
    
    etcd_response_free(&resp);
    return count;
}

static int daemonset_node_is_tainted(const char* node_name) {
    char etcd_key[512];
    snprintf(etcd_key, sizeof(etcd_key), "/sirah/nodes/%s", node_name);
    
    etcd_response_t resp = {0};
    int status = etcd_manager_get(etcd_key, &resp);
    
    if (status != ETCD_OK || !resp.value) {
        etcd_response_free(&resp);
        return 0;  // No taints if node not found
    }
    
    json_object* node = json_tokener_parse(resp.value);
    if (!node) {
        etcd_response_free(&resp);
        return 0;
    }
    
    json_object* spec = json_object_object_get(node, "spec");
    if (spec) {
        json_object* taints_obj = json_object_object_get(spec, "taints");
        if (taints_obj && json_object_is_type(taints_obj, json_type_array)) {
            int taints_count = json_object_array_length(taints_obj);
            for (int i = 0; i < taints_count; i++) {
                json_object* taint = json_object_array_get_idx(taints_obj, i);
                if (taint) {
                    json_object* effect_obj = json_object_object_get(taint, "effect");
                    if (effect_obj) {
                        const char* effect = json_object_get_string(effect_obj);
                        if (effect && strcmp(effect, "NoSchedule") == 0) {
                            json_object_put(node);
                            etcd_response_free(&resp);
                            return 1;  // Node has NoSchedule taint
                        }
                    }
                }
            }
        }
    }
    
    json_object_put(node);
    etcd_response_free(&resp);
    return 0;
}

static int daemonset_node_matches_selector(const char* node_name, const char* selector_key, const char* selector_value) {
    if (!selector_key || !selector_value) {
        return 1;  // No selector = match all
    }
    
    char etcd_key[512];
    snprintf(etcd_key, sizeof(etcd_key), "/sirah/nodes/%s", node_name);
    
    etcd_response_t resp = {0};
    int status = etcd_manager_get(etcd_key, &resp);
    
    if (status != ETCD_OK || !resp.value) {
        etcd_response_free(&resp);
        return 0;
    }
    
    json_object* node = json_tokener_parse(resp.value);
    if (!node) {
        etcd_response_free(&resp);
        return 0;
    }
    
    int matches = 0;
    json_object* metadata = json_object_object_get(node, "metadata");
    if (metadata) {
        json_object* labels = json_object_object_get(metadata, "labels");
        if (labels) {
            json_object* label_value = json_object_object_get(labels, selector_key);
            if (label_value) {
                const char* value = json_object_get_string(label_value);
                if (value && strcmp(value, selector_value) == 0) {
                    matches = 1;
                }
            }
        }
    }
    
    json_object_put(node);
    etcd_response_free(&resp);
    return matches;
}

static int daemonset_create_pod_on_node(const char* namespace, const char* pod_name, 
                                       const char* node_name, const char* image) {
    char etcd_key[512];
    snprintf(etcd_key, sizeof(etcd_key), "/sirah/pods/%s/%s", namespace, pod_name);
    
    json_object* pod = json_object_new_object();
    json_object_object_add(pod, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(pod, "kind", json_object_new_string("Pod"));
    
    json_object* meta = json_object_new_object();
    json_object_object_add(meta, "name", json_object_new_string(pod_name));
    json_object_object_add(meta, "namespace", json_object_new_string(namespace));
    json_object_object_add(pod, "metadata", meta);
    
    json_object* spec = json_object_new_object();
    json_object_object_add(spec, "nodeName", json_object_new_string(node_name));
    
    json_object* containers = json_object_new_array();
    json_object* container = json_object_new_object();
    json_object_object_add(container, "name", json_object_new_string("daemon"));
    json_object_object_add(container, "image", json_object_new_string(image ? image : "default-image"));
    json_object_array_add(containers, container);
    json_object_object_add(spec, "containers", containers);
    json_object_object_add(pod, "spec", spec);
    
    json_object* status = json_object_new_object();
    json_object_object_add(status, "phase", json_object_new_string("Pending"));
    json_object_object_add(pod, "status", status);
    
    const char* pod_json = json_object_to_json_string(pod);
    etcd_response_t resp = {0};
    int result = etcd_manager_put(etcd_key, pod_json, &resp);
    
    json_object_put(pod);
    etcd_response_free(&resp);
    
    return result == ETCD_OK ? 0 : -1;
}

// ============================================================================
// REST Endpoints
// ============================================================================

int endpoint_create_daemonset(const char* namespace, const char* body,
                              char* response_buffer, int* response_code) {
    if (!body || strlen(body) == 0) {
        snprintf(response_buffer, 16384, "{\"error\":\"empty body\"}");
        *response_code = 400;
        return -1;
    }
    
    json_object* req = json_tokener_parse(body);
    if (!req) {
        snprintf(response_buffer, 16384, "{\"error\":\"invalid JSON\"}");
        *response_code = 400;
        return -1;
    }
    
    json_object* metadata = json_object_object_get(req, "metadata");
    if (!metadata) {
        snprintf(response_buffer, 16384, "{\"error\":\"metadata required\"}");
        *response_code = 400;
        json_object_put(req);
        return -1;
    }
    
    const char* name = json_object_get_string(json_object_object_get(metadata, "name"));
    if (!name) {
        snprintf(response_buffer, 16384, "{\"error\":\"name required\"}");
        *response_code = 400;
        json_object_put(req);
        return -1;
    }
    
    char etcd_key[512];
    snprintf(etcd_key, sizeof(etcd_key), "/sirah/daemonsets/%s/%s", 
             namespace ? namespace : "default", name);
    
    // Add status
    json_object* status = json_object_new_object();
    json_object_object_add(status, "desiredNumberScheduled", json_object_new_int(0));
    json_object_object_add(status, "currentNumberScheduled", json_object_new_int(0));
    json_object_object_add(status, "numberReady", json_object_new_int(0));
    json_object_object_add(status, "numberAvailable", json_object_new_int(0));
    json_object_object_add(status, "numberUpdated", json_object_new_int(0));
    json_object_object_add(status, "numberMisscheduled", json_object_new_int(0));
    json_object_object_add(req, "status", status);
    
    const char* json_str = json_object_to_json_string(req);
    etcd_response_t resp = {0};
    int status_code = etcd_manager_put(etcd_key, json_str, &resp);
    
    if (status_code == ETCD_OK) {
        char rv_str[32];
        snprintf(rv_str, sizeof(rv_str), "%lu", resp.revision);
        json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
        
        const char* response_json = json_object_to_json_string(req);
        strncpy(response_buffer, response_json, 16384 - 1);
        *response_code = 201;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"failed to create daemonset\"}");
        *response_code = 500;
    }
    
    json_object_put(req);
    etcd_response_free(&resp);
    return status_code == ETCD_OK ? 0 : -1;
}

int endpoint_get_daemonset(const char* namespace, const char* name,
                           char* response_buffer, int* response_code) {
    char etcd_key[512];
    snprintf(etcd_key, sizeof(etcd_key), "/sirah/daemonsets/%s/%s", 
             namespace ? namespace : "default", name);
    
    etcd_response_t resp = {0};
    int status = etcd_manager_get(etcd_key, &resp);
    
    if (status == ETCD_OK && resp.value) {
        json_object* obj = json_tokener_parse(resp.value);
        if (obj) {
            json_object* meta = json_object_object_get(obj, "metadata");
            if (meta) {
                char rv_str[32];
                snprintf(rv_str, sizeof(rv_str), "%lu", resp.revision);
                json_object_object_add(meta, "resourceVersion", json_object_new_string(rv_str));
            }
            const char* json_str = json_object_to_json_string(obj);
            strncpy(response_buffer, json_str, 16384 - 1);
            json_object_put(obj);
        }
        *response_code = 200;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"daemonset not found\"}");
        *response_code = 404;
    }
    
    etcd_response_free(&resp);
    return status == ETCD_OK ? 0 : -1;
}

int endpoint_list_daemonsets(const char* namespace, char* response_buffer, int* response_code) {
    char etcd_prefix[512];
    snprintf(etcd_prefix, sizeof(etcd_prefix), "/sirah/daemonsets/%s/", 
             namespace ? namespace : "default");
    
    etcd_response_t resp = {0};
    int status = etcd_manager_list(etcd_prefix, &resp);
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("apps/v1"));
    json_object_object_add(root, "kind", json_object_new_string("DaemonSetList"));
    json_object* items = json_object_new_array();
    
    if (status == ETCD_OK && resp.kvs_count > 0) {
        for (int i = 0; i < resp.kvs_count; i++) {
            json_object* obj = json_tokener_parse(resp.kvs_values[i]);
            if (obj) {
                json_object* meta = json_object_object_get(obj, "metadata");
                if (meta) {
                    char rv_str[32];
                    snprintf(rv_str, sizeof(rv_str), "%lu", resp.kvs_versions[i]);
                    json_object_object_add(meta, "resourceVersion", json_object_new_string(rv_str));
                }
                json_object_array_add(items, obj);
            }
        }
    }
    
    json_object_object_add(root, "items", items);
    const char* json_str = json_object_to_json_string(root);
    strncpy(response_buffer, json_str, 16384 - 1);
    *response_code = 200;
    
    json_object_put(root);
    etcd_response_free(&resp);
    return 0;
}

int endpoint_patch_daemonset(const char* namespace, const char* name, const char* body,
                             const char* content_type, char* response_buffer, int* response_code) {
    if (!body) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"empty body\"}");
        return -1;
    }
    
    char etcd_key[512];
    snprintf(etcd_key, sizeof(etcd_key), "/sirah/daemonsets/%s/%s", 
             namespace ? namespace : "default", name);
    
    etcd_response_t get_resp = {0};
    int get_status = etcd_manager_get(etcd_key, &get_resp);
    
    if (get_status != ETCD_OK || !get_resp.value) {
        *response_code = 404;
        strcpy(response_buffer, "{\"error\":\"daemonset not found\"}");
        etcd_response_free(&get_resp);
        return -1;
    }
    
    json_object* current = json_tokener_parse(get_resp.value);
    json_object* patch = json_tokener_parse(body);
    
    if (!current || !patch) {
        if (current) json_object_put(current);
        if (patch) json_object_put(patch);
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        etcd_response_free(&get_resp);
        return -1;
    }
    
    // Merge patch
    json_object* patch_spec = json_object_object_get(patch, "spec");
    if (patch_spec) {
        json_object_object_add(current, "spec", patch_spec);
    }
    
    const char* merged = json_object_to_json_string(current);
    etcd_response_t patch_resp = {0};
    int patch_status = etcd_manager_patch(etcd_key, merged, get_resp.revision, &patch_resp);
    
    if (patch_status == ETCD_OK) {
        const char* response_json = json_object_to_json_string(current);
        strncpy(response_buffer, response_json, 16384 - 1);
        *response_code = 200;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"patch failed\"}");
        *response_code = 500;
    }
    
    json_object_put(current);
    json_object_put(patch);
    etcd_response_free(&get_resp);
    etcd_response_free(&patch_resp);
    
    return patch_status == ETCD_OK ? 0 : -1;
}

int endpoint_delete_daemonset(const char* namespace, const char* name,
                              char* response_buffer, int* response_code) {
    char etcd_key[512];
    snprintf(etcd_key, sizeof(etcd_key), "/sirah/daemonsets/%s/%s", 
             namespace ? namespace : "default", name);
    
    etcd_response_t resp = {0};
    int status = etcd_manager_delete(etcd_key, &resp);
    
    if (status == ETCD_OK) {
        *response_code = 204;
        response_buffer[0] = '\0';
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"failed to delete\"}");
        *response_code = 500;
    }
    
    etcd_response_free(&resp);
    return status == ETCD_OK ? 0 : -1;
}

// ============================================================================
// Controller Lifecycle
// ============================================================================

int daemonset_controller_init(const char* apiserver_url) {
    api_server_url = apiserver_url;
    fprintf(stderr, "[DaemonSet Controller] Initialized with API: %s\n", apiserver_url);
    return 0;
}

int daemonset_controller_run(void) {
    controller_running = 1;
    
    while (controller_running) {
        // List all DaemonSets
        char etcd_prefix[] = "/sirah/daemonsets/";
        etcd_response_t resp = {0};
        
        if (etcd_manager_list(etcd_prefix, &resp) == ETCD_OK && resp.kvs_count > 0) {
            for (int i = 0; i < resp.kvs_count; i++) {
                json_object* ds = json_tokener_parse(resp.kvs_values[i]);
                if (ds) {
                    json_object* spec = json_object_object_get(ds, "spec");
                    
                    // Get all nodes
                    char* node_names[MAX_NODES] = {0};
                    int node_count = daemonset_get_all_nodes(node_names, MAX_NODES);
                    
                    // For each node, ensure pod exists
                    json_object* status = json_object_object_get(ds, "status");
                    int desired = 0;
                    int scheduled = 0;
                    
                    for (int n = 0; n < node_count; n++) {
                        int is_tainted = daemonset_node_is_tainted(node_names[n]);
                        
                        if (!is_tainted) {
                            desired++;
                            
                            // Create pod name from DaemonSet and node
                            json_object* metadata = json_object_object_get(ds, "metadata");
                            const char* ds_name = json_object_get_string(json_object_object_get(metadata, "name"));
                            
                            char pod_name[256];
                            snprintf(pod_name, sizeof(pod_name), "%s-%s", ds_name, node_names[n]);
                            
                            const char* image = "default-image";
                            json_object* container = json_object_array_get_idx(
                                json_object_object_get(spec, "containers"), 0);
                            if (container) {
                                const char* img = json_object_get_string(json_object_object_get(container, "image"));
                                if (img) image = img;
                            }
                            
                            // Check if pod exists
                            char etcd_key[512];
                            snprintf(etcd_key, sizeof(etcd_key), "/sirah/pods/default/%s", pod_name);
                            
                            etcd_response_t pod_resp = {0};
                            int pod_exists = (etcd_manager_get(etcd_key, &pod_resp) == ETCD_OK);
                            etcd_response_free(&pod_resp);
                            
                            if (!pod_exists) {
                                daemonset_create_pod_on_node("default", pod_name, node_names[n], image);
                            }
                            scheduled++;
                        }
                        
                        free(node_names[n]);
                    }
                    
                    // Update status
                    if (status) {
                        json_object_object_add(status, "desiredNumberScheduled", json_object_new_int(desired));
                        json_object_object_add(status, "currentNumberScheduled", json_object_new_int(scheduled));
                        json_object_object_add(status, "numberReady", json_object_new_int(scheduled));
                    }
                    
                    json_object_put(ds);
                }
            }
        }
        
        etcd_response_free(&resp);
        sleep(SYNC_INTERVAL);
    }
    
    return 0;
}

void daemonset_controller_shutdown(void) {
    controller_running = 0;
    fprintf(stderr, "[DaemonSet Controller] Shutdown\n");
}
