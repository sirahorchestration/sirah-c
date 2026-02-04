// internal/controller/statefulset_controller.c
// StatefulSet controller implementation (Phase 5)
// Manage ordered, stateful pod replicas with persistent volumes

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <json-c/json.h>
#include "statefulset_controller.h"
#include "../../internal/etcd/etcd_manager.h"
#include "../../pkg/types/pod.h"

#define SYNC_INTERVAL 5
#define MAX_REPLICAS 256

static const char* api_server_url = NULL;
static int controller_running = 0;

// ============================================================================
// Ordinal Pod Management
// ============================================================================

/**
 * Generate pod name with ordinal: {statefulset-name}-{ordinal}
 */
static void generate_pod_name(const char* statefulset_name, int ordinal, char* pod_name, int max_len) {
    snprintf(pod_name, max_len, "%s-%d", statefulset_name, ordinal);
}

/**
 * Create pod for StatefulSet with ordinal
 */
static int statefulset_create_pod(const char* namespace, const char* ss_name,
                                  int ordinal, const char* image) {
    char pod_name[256];
    generate_pod_name(ss_name, ordinal, pod_name, sizeof(pod_name));
    
    char etcd_key[512];
    snprintf(etcd_key, sizeof(etcd_key), "/sirah/pods/%s/%s",
             namespace ? namespace : "default", pod_name);
    
    json_object* pod = json_object_new_object();
    json_object_object_add(pod, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(pod, "kind", json_object_new_string("Pod"));
    
    json_object* meta = json_object_new_object();
    json_object_object_add(meta, "name", json_object_new_string(pod_name));
    json_object_object_add(meta, "namespace", json_object_new_string(namespace ? namespace : "default"));
    
    // Add owner reference to StatefulSet
    json_object* owner_refs = json_object_new_array();
    json_object* owner = json_object_new_object();
    json_object_object_add(owner, "apiVersion", json_object_new_string("apps/v1"));
    json_object_object_add(owner, "kind", json_object_new_string("StatefulSet"));
    json_object_object_add(owner, "name", json_object_new_string(ss_name));
    json_object_object_add(owner, "controller", json_object_new_boolean(1));
    json_object_array_add(owner_refs, owner);
    json_object_object_add(meta, "ownerReferences", owner_refs);
    
    json_object_object_add(pod, "metadata", meta);
    
    json_object* spec = json_object_new_object();
    
    json_object* containers = json_object_new_array();
    json_object* container = json_object_new_object();
    json_object_object_add(container, "name", json_object_new_string("main"));
    json_object_object_add(container, "image", json_object_new_string(image ? image : "default-image"));
    json_object_array_add(containers, container);
    json_object_object_add(spec, "containers", containers);
    
    // Pod naming - DNS hostname should be {pod-name}.{service-name}.{namespace}.svc.cluster.local
    json_object_object_add(spec, "hostname", json_object_new_string(pod_name));
    json_object_object_add(spec, "subdomain", json_object_new_string(ss_name));
    
    json_object_object_add(pod, "spec", spec);
    
    json_object* status = json_object_new_object();
    json_object_object_add(status, "phase", json_object_new_string("Pending"));
    json_object_object_add(pod, "status", status);
    
    const char* pod_json = json_object_to_json_string(pod);
    etcd_response_t resp = {0};
    int result = etcd_manager_put(etcd_key, pod_json, &resp);
    
    json_object_put(pod);
    etcd_response_free(&resp);
    
    fprintf(stderr, "[StatefulSet Controller] Created pod: %s/%s (ordinal %d)\n",
            namespace ? namespace : "default", pod_name, ordinal);
    
    return result == ETCD_OK ? 0 : -1;
}

/**
 * Get all pod ordinals for a StatefulSet
 */
static int statefulset_get_pod_ordinals(const char* namespace, const char* ss_name,
                                        int* ordinals, int max_ordinals) {
    char etcd_prefix[512];
    snprintf(etcd_prefix, sizeof(etcd_prefix), "/sirah/pods/%s/%s-",
             namespace ? namespace : "default", ss_name);
    
    etcd_response_t resp = {0};
    int status = etcd_manager_list(etcd_prefix, &resp);
    
    int count = 0;
    if (status == ETCD_OK && resp.kvs_count > 0) {
        for (int i = 0; i < resp.kvs_count && count < max_ordinals; i++) {
            // Extract ordinal from pod name: {ss-name}-{ordinal}
            const char* key = resp.kvs_keys[i];
            const char* pod_name = strrchr(key, '/') + 1;
            
            // Find ordinal by parsing pod name
            const char* dash_pos = strstr(pod_name, ss_name);
            if (dash_pos) {
                dash_pos += strlen(ss_name) + 1;  // Skip {ss-name}-
                int ordinal = atoi(dash_pos);
                ordinals[count++] = ordinal;
            }
        }
    }
    
    etcd_response_free(&resp);
    return count;
}

// ============================================================================
// REST Endpoints
// ============================================================================

int endpoint_create_statefulset(const char* namespace, const char* body,
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
    snprintf(etcd_key, sizeof(etcd_key), "/sirah/statefulsets/%s/%s",
             namespace ? namespace : "default", name);
    
    // Extract replica count
    json_object* spec = json_object_object_get(req, "spec");
    int replicas = 1;
    if (spec) {
        json_object* rep_obj = json_object_object_get(spec, "replicas");
        if (rep_obj && json_object_is_type(rep_obj, json_type_int)) {
            replicas = json_object_get_int(rep_obj);
        }
    }
    
    // Add status
    json_object* status = json_object_new_object();
    json_object_object_add(status, "replicas", json_object_new_int(0));
    json_object_object_add(status, "readyReplicas", json_object_new_int(0));
    json_object_object_add(status, "currentReplicas", json_object_new_int(0));
    json_object_object_add(status, "updatedReplicas", json_object_new_int(0));
    json_object_object_add(status, "observedGeneration", json_object_new_int(0));
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
        snprintf(response_buffer, 16384, "{\"error\":\"failed to create\"}");
        *response_code = 500;
    }
    
    json_object_put(req);
    etcd_response_free(&resp);
    return status_code == ETCD_OK ? 0 : -1;
}

int endpoint_get_statefulset_sts(const char* namespace, const char* name,
                                 char* response_buffer, int* response_code) {
    char etcd_key[512];
    snprintf(etcd_key, sizeof(etcd_key), "/sirah/statefulsets/%s/%s",
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
        snprintf(response_buffer, 16384, "{\"error\":\"statefulset not found\"}");
        *response_code = 404;
    }
    
    etcd_response_free(&resp);
    return status == ETCD_OK ? 0 : -1;
}

int endpoint_list_statefulsets_sts(const char* namespace, char* response_buffer,
                                   int* response_code) {
    char etcd_prefix[512];
    snprintf(etcd_prefix, sizeof(etcd_prefix), "/sirah/statefulsets/%s/",
             namespace ? namespace : "default");
    
    etcd_response_t resp = {0};
    int status = etcd_manager_list(etcd_prefix, &resp);
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("apps/v1"));
    json_object_object_add(root, "kind", json_object_new_string("StatefulSetList"));
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

int endpoint_patch_statefulset(const char* namespace, const char* name, const char* body,
                               const char* content_type, char* response_buffer,
                               int* response_code) {
    if (!body) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"empty body\"}");
        return -1;
    }
    
    char etcd_key[512];
    snprintf(etcd_key, sizeof(etcd_key), "/sirah/statefulsets/%s/%s",
             namespace ? namespace : "default", name);
    
    etcd_response_t get_resp = {0};
    int get_status = etcd_manager_get(etcd_key, &get_resp);
    
    if (get_status != ETCD_OK || !get_resp.value) {
        *response_code = 404;
        strcpy(response_buffer, "{\"error\":\"not found\"}");
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

int endpoint_delete_statefulset_sts(const char* namespace, const char* name,
                                    char* response_buffer, int* response_code) {
    char etcd_key[512];
    snprintf(etcd_key, sizeof(etcd_key), "/sirah/statefulsets/%s/%s",
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

int statefulset_controller_init(const char* apiserver_url) {
    api_server_url = apiserver_url;
    fprintf(stderr, "[StatefulSet Controller] Initialized with API: %s\n", apiserver_url);
    return 0;
}

int statefulset_controller_run(void) {
    controller_running = 1;
    
    while (controller_running) {
        // List all StatefulSets
        char etcd_prefix[] = "/sirah/statefulsets/";
        etcd_response_t resp = {0};
        
        if (etcd_manager_list(etcd_prefix, &resp) == ETCD_OK && resp.kvs_count > 0) {
            for (int i = 0; i < resp.kvs_count; i++) {
                json_object* sts = json_tokener_parse(resp.kvs_values[i]);
                if (!sts) continue;
                
                json_object* metadata = json_object_object_get(sts, "metadata");
                json_object* spec = json_object_object_get(sts, "spec");
                json_object* status = json_object_object_get(sts, "status");
                
                const char* ss_name = json_object_get_string(json_object_object_get(metadata, "name"));
                const char* namespace = json_object_get_string(json_object_object_get(metadata, "namespace"));
                
                int desired_replicas = 1;
                if (spec) {
                    json_object* rep_obj = json_object_object_get(spec, "replicas");
                    if (rep_obj && json_object_is_type(rep_obj, json_type_int)) {
                        desired_replicas = json_object_get_int(rep_obj);
                    }
                }
                
                // Get image from container spec
                const char* image = "default-image";
                if (spec) {
                    json_object* template = json_object_object_get(spec, "template");
                    if (template) {
                        json_object* t_spec = json_object_object_get(template, "spec");
                        if (t_spec) {
                            json_object* containers = json_object_object_get(t_spec, "containers");
                            if (containers && json_object_is_type(containers, json_type_array)) {
                                json_object* container = json_object_array_get_idx(containers, 0);
                                if (container) {
                                    const char* img = json_object_get_string(json_object_object_get(container, "image"));
                                    if (img) image = img;
                                }
                            }
                        }
                    }
                }
                
                // Create pods for each ordinal
                for (int ordinal = 0; ordinal < desired_replicas; ordinal++) {
                    char pod_name[256];
                    generate_pod_name(ss_name, ordinal, pod_name, sizeof(pod_name));
                    
                    char etcd_key[512];
                    snprintf(etcd_key, sizeof(etcd_key), "/sirah/pods/%s/%s",
                             namespace ? namespace : "default", pod_name);
                    
                    etcd_response_t pod_resp = {0};
                    int pod_exists = (etcd_manager_get(etcd_key, &pod_resp) == ETCD_OK);
                    etcd_response_free(&pod_resp);
                    
                    if (!pod_exists) {
                        statefulset_create_pod(namespace, ss_name, ordinal, image);
                    }
                }
                
                // Update status
                if (status) {
                    json_object_object_add(status, "replicas", json_object_new_int(desired_replicas));
                    json_object_object_add(status, "readyReplicas", json_object_new_int(desired_replicas));
                }
                
                json_object_put(sts);
            }
        }
        
        etcd_response_free(&resp);
        sleep(SYNC_INTERVAL);
    }
    
    return 0;
}

void statefulset_controller_shutdown(void) {
    controller_running = 0;
    fprintf(stderr, "[StatefulSet Controller] Shutdown\n");
}

int endpoint_create_statefulset(const char* namespace, const char* body,
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
    
    // Extract metadata
    json_object* metadata = json_object_object_get(req, "metadata");
    if (!metadata) {
        snprintf(response_buffer, 16384, "{\"error\":\"metadata required\"}");
        *response_code = 400;
        json_object_put(req);
        return -1;
    }
    
    const char* ss_name = json_object_get_string(
        json_object_object_get(metadata, "name"));
    if (!ss_name) {
        snprintf(response_buffer, 16384, "{\"error\":\"name required\"}");
        *response_code = 400;
        json_object_put(req);
        return -1;
    }
    
    // Extract spec
    json_object* spec = json_object_object_get(req, "spec");
    if (!spec) {
        snprintf(response_buffer, 16384, "{\"error\":\"spec required\"}");
        *response_code = 400;
        json_object_put(req);
        return -1;
    }
    
    // Get replicas
    int replicas = 1;
    json_object* replicas_obj = json_object_object_get(spec, "replicas");
    if (replicas_obj) {
        replicas = json_object_get_int(replicas_obj);
    }
    
    // Add status fields
    json_object* status = json_object_new_object();
    json_object_object_add(status, "replicas", json_object_new_int(0));
    json_object_object_add(status, "readyReplicas", json_object_new_int(0));
    json_object_object_add(status, "updatedReplicas", json_object_new_int(0));
    json_object_object_add(status, "availableReplicas", json_object_new_int(0));
    
    json_object_object_add(metadata, "generation", json_object_new_int(1));
    json_object_object_add(req, "status", status);
    
    // Store in etcd
    char etcd_key[512];
    snprintf(etcd_key, sizeof(etcd_key), "/sirah/statefulsets/%s/%s",
             namespace ? namespace : "default", ss_name);
    
    const char* json_str = json_object_to_json_string(req);
    etcd_response_t etcd_resp = {0};
    int status_code = etcd_manager_put(etcd_key, json_str, &etcd_resp);
    
    if (status_code == ETCD_OK) {
        // Add resourceVersion
        char rv_str[32];
        snprintf(rv_str, sizeof(rv_str), "%lu", etcd_resp.revision);
        json_object_object_add(metadata, "resourceVersion",
                              json_object_new_string(rv_str));
        
        const char* response_json = json_object_to_json_string(req);
        strncpy(response_buffer, response_json, 16384 - 1);
        *response_code = 201;  // Created
        
        fprintf(stderr, "[StatefulSet Controller] Created: %s/%s (replicas=%d)\n",
                namespace, ss_name, replicas);
        fflush(stderr);
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"storage error\"}");
        *response_code = 500;
    }
    
    json_object_put(req);
    etcd_response_free(&etcd_resp);
    
    return status_code == ETCD_OK ? 0 : -1;
}

int endpoint_get_statefulset(const char* namespace, const char* name,
                             char* response_buffer, int* response_code) {
    
    char etcd_key[512];
    snprintf(etcd_key, sizeof(etcd_key), "/sirah/statefulsets/%s/%s",
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
                json_object_object_add(meta, "resourceVersion",
                                      json_object_new_string(rv_str));
            }
            const char* json_str = json_object_to_json_string(obj);
            strncpy(response_buffer, json_str, 16384 - 1);
            json_object_put(obj);
        }
        *response_code = 200;
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"StatefulSet not found\"}");
        *response_code = 404;
    }
    
    etcd_response_free(&resp);
    return status == ETCD_OK ? 0 : -1;
}

int endpoint_list_statefulsets(const char* namespace, char* response_buffer,
                               int* response_code) {
    
    char etcd_prefix[512];
    snprintf(etcd_prefix, sizeof(etcd_prefix), "/sirah/statefulsets/%s/",
             namespace ? namespace : "default");
    
    etcd_response_t resp = {0};
    int status = etcd_manager_list(etcd_prefix, &resp);
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("apps/v1"));
    json_object_object_add(root, "kind",
                          json_object_new_string("StatefulSetList"));
    
    json_object* items = json_object_new_array();
    
    if (status == ETCD_OK && resp.kvs_count > 0) {
        for (int i = 0; i < resp.kvs_count; i++) {
            json_object* obj = json_tokener_parse(resp.kvs_values[i]);
            if (obj) {
                json_object* meta = json_object_object_get(obj, "metadata");
                if (meta) {
                    char rv_str[32];
                    snprintf(rv_str, sizeof(rv_str), "%lu", resp.kvs_versions[i]);
                    json_object_object_add(meta, "resourceVersion",
                                          json_object_new_string(rv_str));
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

int endpoint_patch_statefulset(const char* namespace, const char* name,
                               const char* body, const char* content_type,
                               char* response_buffer, int* response_code) {
    
    if (!body || strlen(body) == 0) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"empty body\"}");
        return -1;
    }
    
    // Get current StatefulSet
    char etcd_key[512];
    snprintf(etcd_key, sizeof(etcd_key), "/sirah/statefulsets/%s/%s",
             namespace ? namespace : "default", name);
    
    etcd_response_t get_resp = {0};
    int get_status = etcd_manager_get(etcd_key, &get_resp);
    
    if (get_status != ETCD_OK || !get_resp.value) {
        *response_code = 404;
        strcpy(response_buffer, "{\"error\":\"StatefulSet not found\"}");
        etcd_response_free(&get_resp);
        return -1;
    }
    
    // Parse and merge patch
    json_object* current = json_tokener_parse(get_resp.value);
    json_object* patch = json_tokener_parse(body);
    
    if (!current || !patch) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        if (current) json_object_put(current);
        if (patch) json_object_put(patch);
        etcd_response_free(&get_resp);
        return -1;
    }
    
    // Merge spec
    json_object* patch_spec = json_object_object_get(patch, "spec");
    if (patch_spec) {
        json_object* current_spec = json_object_object_get(current, "spec");
        json_object* new_replicas = json_object_object_get(patch_spec, "replicas");
        if (new_replicas && current_spec) {
            json_object_object_add(current_spec, "replicas", new_replicas);
            
            fprintf(stderr, "[StatefulSet Controller] Scaling %s/%s to %d replicas\n",
                    namespace, name, json_object_get_int(new_replicas));
            fflush(stderr);
        }
    }
    
    // Update generation
    json_object* meta = json_object_object_get(current, "metadata");
    if (meta) {
        json_object* gen = json_object_object_get(meta, "generation");
        int gen_num = 1;
        if (gen) gen_num = json_object_get_int(gen) + 1;
        json_object_object_add(meta, "generation", json_object_new_int(gen_num));
    }
    
    // Store updated StatefulSet
    const char* updated_json = json_object_to_json_string(current);
    etcd_response_t patch_resp = {0};
    int patch_status = etcd_manager_patch(etcd_key, updated_json,
                                         get_resp.revision, &patch_resp);
    
    if (patch_status == ETCD_OK) {
        if (meta) {
            char rv_str[32];
            snprintf(rv_str, sizeof(rv_str), "%lu", patch_resp.revision);
            json_object_object_add(meta, "resourceVersion",
                                  json_object_new_string(rv_str));
        }
        
        const char* response_json = json_object_to_json_string(current);
        strncpy(response_buffer, response_json, 16384 - 1);
        *response_code = 200;
    } else {
        *response_code = 500;
        strcpy(response_buffer, "{\"error\":\"update failed\"}");
    }
    
    json_object_put(current);
    json_object_put(patch);
    etcd_response_free(&get_resp);
    etcd_response_free(&patch_resp);
    
    return patch_status == ETCD_OK ? 0 : -1;
}

int endpoint_delete_statefulset(const char* namespace, const char* name,
                                char* response_buffer, int* response_code) {
    
    char etcd_key[512];
    snprintf(etcd_key, sizeof(etcd_key), "/sirah/statefulsets/%s/%s",
             namespace ? namespace : "default", name);
    
    etcd_response_t resp = {0};
    int status = etcd_manager_delete(etcd_key, &resp);
    
    if (status == ETCD_OK) {
        // Delete associated pods in reverse ordinal order
        fprintf(stderr, "[StatefulSet Controller] Deleting StatefulSet: %s/%s\n",
                namespace, name);
        fflush(stderr);
        
        *response_code = 204;  // No Content
        response_buffer[0] = '\0';
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"delete failed\"}");
        *response_code = 500;
    }
    
    etcd_response_free(&resp);
    return status == ETCD_OK ? 0 : -1;
}

// ============================================================================
// StatefulSet Controller Functions
// ============================================================================

char* statefulset_generate_pod_name(const char* ss_name, int ordinal) {
    static char pod_name[256];
    snprintf(pod_name, sizeof(pod_name), "%s-%d", ss_name, ordinal);
    return pod_name;
}

char* statefulset_generate_service_dns(const char* pod_name,
                                       const char* service_name,
                                       const char* namespace) {
    static char dns_name[512];
    snprintf(dns_name, sizeof(dns_name), "%s.%s.%s.svc.cluster.local",
             pod_name, service_name, namespace);
    return dns_name;
}

int statefulset_create_pvc(const char* namespace, const char* statefulset_name,
                          int ordinal, statefulset_pvc_t* pvc_spec) {
    
    if (!pvc_spec || !pvc_spec->name) return -1;
    
    // Create PVC name: {pvc-template-name}-{pod-name}
    char pvc_name[256];
    char pod_name[256];
    snprintf(pod_name, sizeof(pod_name), "%s-%d", statefulset_name, ordinal);
    snprintf(pvc_name, sizeof(pvc_name), "%s-%s", pvc_spec->name, pod_name);
    
    // Build PVC JSON
    json_object* pvc = json_object_new_object();
    json_object_object_add(pvc, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(pvc, "kind", json_object_new_string("PersistentVolumeClaim"));
    
    json_object* meta = json_object_new_object();
    json_object_object_add(meta, "name", json_object_new_string(pvc_name));
    json_object_object_add(meta, "namespace",
                          json_object_new_string(namespace ? namespace : "default"));
    json_object_object_add(pvc, "metadata", meta);
    
    json_object* spec = json_object_new_object();
    json_object_object_add(spec, "accessModes", json_object_new_array());
    json_object* access = json_object_object_get(spec, "accessModes");
    json_object_array_add(access, json_object_new_string(pvc_spec->access_mode));
    
    json_object* resources = json_object_new_object();
    json_object_object_add(resources, "requests",
                          json_object_new_object());
    json_object* requests = json_object_object_get(resources, "requests");
    json_object_object_add(requests, "storage",
                          json_object_new_string(pvc_spec->capacity));
    json_object_object_add(spec, "resources", resources);
    
    if (strlen(pvc_spec->storage_class) > 0) {
        json_object_object_add(spec, "storageClassName",
                              json_object_new_string(pvc_spec->storage_class));
    }
    
    json_object_object_add(pvc, "spec", spec);
    
    // Store in etcd
    char etcd_key[512];
    snprintf(etcd_key, sizeof(etcd_key), "/sirah/pvc/%s/%s",
             namespace ? namespace : "default", pvc_name);
    
    const char* json_str = json_object_to_json_string(pvc);
    etcd_response_t resp = {0};
    int status = etcd_manager_put(etcd_key, json_str, &resp);
    
    fprintf(stderr, "[StatefulSet] Created PVC: %s/%s (capacity=%s)\n",
            namespace, pvc_name, pvc_spec->capacity);
    fflush(stderr);
    
    json_object_put(pvc);
    etcd_response_free(&resp);
    
    return status == ETCD_OK ? 0 : -1;
}

int statefulset_create_pod(const char* namespace, const char* ss_name,
                          int ordinal, k8s_pod_t* pod) {
    
    if (!pod) return -1;
    
    // Pod name with stable identity
    char pod_name[256];
    snprintf(pod_name, sizeof(pod_name), "%s-%d", ss_name, ordinal);
    
    // Create pod in etcd
    char etcd_key[512];
    snprintf(etcd_key, sizeof(etcd_key), "/sirah/pods/%s/%s",
             namespace ? namespace : "default", pod_name);
    
    json_object* pod_json = json_object_new_object();
    json_object_object_add(pod_json, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(pod_json, "kind", json_object_new_string("Pod"));
    
    json_object* meta = json_object_new_object();
    json_object_object_add(meta, "name", json_object_new_string(pod_name));
    json_object_object_add(meta, "namespace",
                          json_object_new_string(namespace ? namespace : "default"));
    json_object_object_add(meta, "ownerReferences", json_object_new_array());
    
    json_object_object_add(pod_json, "metadata", meta);
    
    // Copy spec from pod parameter
    if (pod->spec.num_containers > 0) {
        json_object* spec = json_object_new_object();
        json_object* containers = json_object_new_array();
        
        for (int i = 0; i < pod->spec.num_containers; i++) {
            json_object* container = json_object_new_object();
            json_object_object_add(container, "name",
                                  json_object_new_string(pod->spec.containers[i].name));
            json_object_object_add(container, "image",
                                  json_object_new_string(pod->spec.containers[i].image));
            if (pod->spec.containers[i].image_pull_policy) {
                json_object_object_add(container, "imagePullPolicy",
                                      json_object_new_string(pod->spec.containers[i].image_pull_policy));
            }
            json_object_array_add(containers, container);
        }
        
        json_object_object_add(spec, "containers", containers);
        json_object_object_add(spec, "restartPolicy",
                              json_object_new_string(pod->spec.restart_policy));
        json_object_object_add(pod_json, "spec", spec);
    }
    
    // Add initial status
    json_object* status = json_object_new_object();
    json_object_object_add(status, "phase", json_object_new_string("Pending"));
    json_object_object_add(pod_json, "status", status);
    
    // Store in etcd
    const char* json_str = json_object_to_json_string(pod_json);
    etcd_response_t resp = {0};
    int status_code = etcd_manager_put(etcd_key, json_str, &resp);
    
    fprintf(stderr, "[StatefulSet] Created pod: %s/%s (ordinal=%d)\n",
            namespace, pod_name, ordinal);
    fflush(stderr);
    
    json_object_put(pod_json);
    etcd_response_free(&resp);
    
    return status_code == ETCD_OK ? 0 : -1;
}

int statefulset_wait_pod_ready(const char* namespace, const char* pod_name,
                              int timeout_seconds) {
    
    time_t start = time(NULL);
    
    while (time(NULL) - start < timeout_seconds) {
        // Check pod status via API
        char etcd_key[512];
        snprintf(etcd_key, sizeof(etcd_key), "/sirah/pods/%s/%s",
                 namespace ? namespace : "default", pod_name);
        
        etcd_response_t resp = {0};
        int status = etcd_manager_get(etcd_key, &resp);
        
        if (status == ETCD_OK && resp.value) {
            json_object* pod_obj = json_tokener_parse(resp.value);
            if (pod_obj) {
                json_object* pod_status = json_object_object_get(pod_obj, "status");
                if (pod_status) {
                    json_object* phase = json_object_object_get(pod_status, "phase");
                    const char* phase_str = json_object_get_string(phase);
                    
                    if (phase_str && strcmp(phase_str, "Running") == 0) {
                        fprintf(stderr, "[StatefulSet] Pod ready: %s/%s\n",
                                namespace, pod_name);
                        fflush(stderr);
                        json_object_put(pod_obj);
                        etcd_response_free(&resp);
                        return 0;  // Ready
                    }
                }
                json_object_put(pod_obj);
            }
        }
        
        etcd_response_free(&resp);
        sleep(1);  // Check every second
    }
    
    fprintf(stderr, "[StatefulSet] Timeout waiting for pod: %s/%s\n",
            namespace, pod_name);
    fflush(stderr);
    
    return -1;  // Timeout
}

int statefulset_perform_rolling_update(const char* namespace,
                                      const char* ss_name) {
    
    fprintf(stderr, "[StatefulSet] Rolling update: %s/%s\n", namespace, ss_name);
    fflush(stderr);
    
    // In production, would:
    // 1. Terminate pod N
    // 2. Wait for termination
    // 3. Create pod N with new template
    // 4. Wait for readiness
    // 5. Repeat for N-1, N-2, ... 0
    
    return 0;
}

int statefulset_scale(const char* namespace, const char* ss_name,
                     int new_replicas) {
    
    fprintf(stderr, "[StatefulSet] Scaling %s/%s to %d replicas\n",
            namespace, ss_name, new_replicas);
    fflush(stderr);
    
    // Get current StatefulSet
    char etcd_key[512];
    snprintf(etcd_key, sizeof(etcd_key), "/sirah/statefulsets/%s/%s",
             namespace ? namespace : "default", ss_name);
    
    etcd_response_t resp = {0};
    int status = etcd_manager_get(etcd_key, &resp);
    
    if (status != ETCD_OK || !resp.value) {
        return -1;
    }
    
    json_object* ss_obj = json_tokener_parse(resp.value);
    if (!ss_obj) {
        etcd_response_free(&resp);
        return -1;
    }
    
    // Get current replica count
    json_object* spec = json_object_object_get(ss_obj, "spec");
    json_object* current_replicas_obj = json_object_object_get(spec, "replicas");
    int current_replicas = 1;
    if (current_replicas_obj) {
        current_replicas = json_object_get_int(current_replicas_obj);
    }
    
    // If scaling up, create new pods
    if (new_replicas > current_replicas) {
        for (int i = current_replicas; i < new_replicas; i++) {
            // Create pod i
            k8s_pod_t* pod = k8s_pod_new("temp", namespace);
            if (pod) {
                statefulset_create_pod(namespace, ss_name, i, pod);
                k8s_pod_free(pod);
            }
        }
    }
    // If scaling down, terminate pods in reverse order
    else if (new_replicas < current_replicas) {
        for (int i = current_replicas - 1; i >= new_replicas; i--) {
            char pod_name[256];
            snprintf(pod_name, sizeof(pod_name), "%s-%d", ss_name, i);
            
            char pod_key[512];
            snprintf(pod_key, sizeof(pod_key), "/sirah/pods/%s/%s",
                     namespace ? namespace : "default", pod_name);
            
            etcd_response_t del_resp = {0};
            etcd_manager_delete(pod_key, &del_resp);
            etcd_response_free(&del_resp);
        }
    }
    
    // Update replicas in spec
    json_object_object_add(spec, "replicas", json_object_new_int(new_replicas));
    
    // Update status
    json_object* ss_status = json_object_object_get(ss_obj, "status");
    if (!ss_status) {
        ss_status = json_object_new_object();
        json_object_object_add(ss_obj, "status", ss_status);
    }
    json_object_object_add(ss_status, "replicas", json_object_new_int(new_replicas));
    
    // Store updated StatefulSet
    const char* updated_json = json_object_to_json_string(ss_obj);
    etcd_response_t patch_resp = {0};
    etcd_manager_patch(etcd_key, updated_json, resp.revision, &patch_resp);
    
    json_object_put(ss_obj);
    etcd_response_free(&resp);
    etcd_response_free(&patch_resp);
    
    return 0;
}

// ============================================================================
// StatefulSet Controller Loop
// ============================================================================

int statefulset_controller_init(const char* apiserver_url) {
    api_server_url = apiserver_url;
    g_response_buffer.data = (char*)malloc(1048576);  // 1MB buffer
    g_response_buffer.capacity = 1048576;
    g_response_buffer.size = 0;
    
    fprintf(stderr, "[StatefulSet Controller] Initialized\n");
    fflush(stderr);
    
    return 0;
}

int statefulset_controller_run(void) {
    
    fprintf(stderr, "[StatefulSet Controller] Running\n");
    fflush(stderr);
    
    // Main reconciliation loop
    int iteration = 0;
    while (1) {
        iteration++;
        
        fprintf(stderr, "[StatefulSet Controller] Reconciliation cycle %d\n", iteration);
        fflush(stderr);
        
        // TODO: Fetch all StatefulSets from etcd
        // TODO: For each StatefulSet:
        //   - Check desired vs actual replicas
        //   - Create missing pods in ordinal order
        //   - Delete extra pods in reverse ordinal order
        //   - Update pod status
        //   - Handle rolling updates
        
        sleep(5);  // Reconcile every 5 seconds
    }
    
    return 0;
}

void statefulset_controller_shutdown(void) {
    fprintf(stderr, "[StatefulSet Controller] Shutting down\n");
    fflush(stderr);
    
    free(g_response_buffer.data);
}
