// internal/apiserver/endpoints_controllers.c
// HTTP endpoint implementations for Phase 5 controller resources

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include <uuid/uuid.h>
#include "endpoints_controllers.h"
#include "../../../internal/etcd/etcd_manager.h"

#define RESPONSE_BUFFER_SIZE 16384

// ============================================================================
// Helper Functions
// ============================================================================

static void build_statefulset_etcd_key(const char* namespace, const char* name,
                                       char* key, int key_len) {
    snprintf(key, key_len, "/sirah/statefulsets/%s/%s",
             namespace ? namespace : "default",
             name ? name : "unknown");
}

static void build_job_etcd_key(const char* namespace, const char* name,
                               char* key, int key_len) {
    snprintf(key, key_len, "/sirah/jobs/%s/%s",
             namespace ? namespace : "default",
             name ? name : "unknown");
}

static void build_limitrange_etcd_key(const char* namespace, const char* name,
                                      char* key, int key_len) {
    snprintf(key, key_len, "/sirah/limitranges/%s/%s",
             namespace ? namespace : "default",
             name ? name : "unknown");
}

// ============================================================================
// StatefulSet Endpoints
// ============================================================================

int endpoint_create_statefulset(const char* namespace, const char* body,
                                char* response_buffer, int* response_code) {
    if (!body || strlen(body) == 0) {
        snprintf(response_buffer, RESPONSE_BUFFER_SIZE, "{\"error\":\"empty body\"}");
        *response_code = 400;
        return -1;
    }
    
    json_object* req = json_tokener_parse(body);
    if (!req) {
        snprintf(response_buffer, RESPONSE_BUFFER_SIZE, "{\"error\":\"invalid JSON\"}");
        *response_code = 400;
        return -1;
    }
    
    json_object* metadata = json_object_object_get(req, "metadata");
    if (!metadata) {
        snprintf(response_buffer, RESPONSE_BUFFER_SIZE, "{\"error\":\"metadata required\"}");
        *response_code = 400;
        json_object_put(req);
        return -1;
    }
    
    const char* name = json_object_get_string(json_object_object_get(metadata, "name"));
    if (!name) {
        snprintf(response_buffer, RESPONSE_BUFFER_SIZE, "{\"error\":\"name required\"}");
        *response_code = 400;
        json_object_put(req);
        return -1;
    }
    
    // Add resourceVersion
    char rv_str[32];
    snprintf(rv_str, sizeof(rv_str), "%lu", (unsigned long)time(NULL) * 1000);
    json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
    json_object_object_add(metadata, "generation", json_object_new_int64(0));
    
    const char* json_str = json_object_to_json_string(req);
    
    char etcd_key[256];
    build_statefulset_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t put_resp = {0};
    int status = etcd_manager_put(etcd_key, json_str, &put_resp);
    
    if (status == ETCD_OK) {
        snprintf(rv_str, sizeof(rv_str), "%lu", put_resp.revision);
        json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
        
        const char* response_json = json_object_to_json_string(req);
        strncpy(response_buffer, response_json, RESPONSE_BUFFER_SIZE - 1);
        *response_code = 201;
    } else {
        snprintf(response_buffer, RESPONSE_BUFFER_SIZE, "{\"error\":\"failed to store statefulset\"}");
        *response_code = 500;
    }
    
    json_object_put(req);
    etcd_response_free(&put_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

int endpoint_get_statefulset(const char* namespace, const char* name,
                             char* response_buffer, int* response_code) {
    char etcd_key[256];
    build_statefulset_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t get_resp = {0};
    int status = etcd_manager_get(etcd_key, &get_resp);
    
    if (status == ETCD_OK && get_resp.value) {
        strncpy(response_buffer, get_resp.value, RESPONSE_BUFFER_SIZE - 1);
        *response_code = 200;
    } else {
        snprintf(response_buffer, RESPONSE_BUFFER_SIZE, "{\"error\":\"statefulset not found\"}");
        *response_code = 404;
    }
    
    etcd_response_free(&get_resp);
    return (status == ETCD_OK && get_resp.value) ? 0 : -1;
}

int endpoint_list_statefulsets(const char* namespace, char* response_buffer, int* response_code) {
    char etcd_prefix[256];
    snprintf(etcd_prefix, sizeof(etcd_prefix), "/sirah/statefulsets/%s/",
             namespace ? namespace : "default");
    
    etcd_response_t list_resp = {0};
    int status = etcd_manager_list(etcd_prefix, &list_resp);
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("apps/v1"));
    json_object_object_add(root, "kind", json_object_new_string("StatefulSetList"));
    json_object* items = json_object_new_array();
    
    if (status == ETCD_OK && list_resp.value) {
        // Parse returned items
        char* copy = strdup(list_resp.value);
        char* line = strtok(copy, "\n");
        while (line) {
            if (strlen(line) > 0) {
                json_object* ss = json_tokener_parse(line);
                if (ss) json_object_array_add(items, ss);
            }
            line = strtok(NULL, "\n");
        }
        free(copy);
    }
    
    json_object_object_add(root, "items", items);
    const char* json_str = json_object_to_json_string(root);
    strncpy(response_buffer, json_str, RESPONSE_BUFFER_SIZE - 1);
    *response_code = 200;
    
    json_object_put(root);
    etcd_response_free(&list_resp);
    return 0;
}

int endpoint_patch_statefulset(const char* namespace, const char* name, const char* body,
                               const char* content_type, char* response_buffer, int* response_code) {
    (void)content_type;
    
    if (!body) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"empty body\"}");
        return -1;
    }
    
    char etcd_key[256];
    build_statefulset_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t get_resp = {0};
    int get_status = etcd_manager_get(etcd_key, &get_resp);
    
    if (get_status != ETCD_OK || !get_resp.value) {
        *response_code = 404;
        strcpy(response_buffer, "{\"error\":\"statefulset not found\"}");
        etcd_response_free(&get_resp);
        return -1;
    }
    
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
    
    // Merge patch (simple merge for now)
    json_object* patch_spec = json_object_object_get(patch, "spec");
    if (patch_spec) {
        json_object_object_add(current, "spec", patch_spec);
    }
    
    // Update resourceVersion
    json_object* meta = json_object_object_get(current, "metadata");
    if (meta) {
        char rv_str[32];
        snprintf(rv_str, sizeof(rv_str), "%lu", get_resp.revision + 1);
        json_object_object_add(meta, "resourceVersion", json_object_new_string(rv_str));
    }
    
    const char* merged_json = json_object_to_json_string(current);
    
    etcd_response_t patch_resp = {0};
    int patch_status = etcd_manager_patch(etcd_key, merged_json, get_resp.revision, &patch_resp);
    
    if (patch_status == ETCD_OK) {
        const char* response_json = json_object_to_json_string(current);
        strncpy(response_buffer, response_json, RESPONSE_BUFFER_SIZE - 1);
        *response_code = 200;
    } else if (patch_status == ETCD_CAS_FAILED) {
        *response_code = 409;
        strcpy(response_buffer, "{\"error\":\"Conflict\"}");
    } else {
        *response_code = 500;
        strcpy(response_buffer, "{\"error\":\"internal error\"}");
    }
    
    json_object_put(current);
    json_object_put(patch);
    etcd_response_free(&get_resp);
    etcd_response_free(&patch_resp);
    
    return (patch_status == ETCD_OK) ? 0 : -1;
}

int endpoint_delete_statefulset(const char* namespace, const char* name,
                                char* response_buffer, int* response_code) {
    char etcd_key[256];
    build_statefulset_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t del_resp = {0};
    int status = etcd_manager_delete(etcd_key, &del_resp);
    
    if (status == ETCD_OK) {
        *response_code = 204;
        response_buffer[0] = '\0';
    } else {
        snprintf(response_buffer, RESPONSE_BUFFER_SIZE, "{\"error\":\"failed to delete\"}");
        *response_code = 500;
    }
    
    etcd_response_free(&del_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

// ============================================================================
// Job Endpoints
// ============================================================================

int endpoint_create_job(const char* namespace, const char* body,
                        char* response_buffer, int* response_code) {
    if (!body || strlen(body) == 0) {
        snprintf(response_buffer, RESPONSE_BUFFER_SIZE, "{\"error\":\"empty body\"}");
        *response_code = 400;
        return -1;
    }
    
    json_object* req = json_tokener_parse(body);
    if (!req) {
        snprintf(response_buffer, RESPONSE_BUFFER_SIZE, "{\"error\":\"invalid JSON\"}");
        *response_code = 400;
        return -1;
    }
    
    json_object* metadata = json_object_object_get(req, "metadata");
    if (!metadata) {
        snprintf(response_buffer, RESPONSE_BUFFER_SIZE, "{\"error\":\"metadata required\"}");
        *response_code = 400;
        json_object_put(req);
        return -1;
    }
    
    const char* name = json_object_get_string(json_object_object_get(metadata, "name"));
    if (!name) {
        snprintf(response_buffer, RESPONSE_BUFFER_SIZE, "{\"error\":\"name required\"}");
        *response_code = 400;
        json_object_put(req);
        return -1;
    }
    
    // Add resourceVersion
    char rv_str[32];
    snprintf(rv_str, sizeof(rv_str), "%lu", (unsigned long)time(NULL) * 1000);
    json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
    json_object_object_add(metadata, "generation", json_object_new_int64(1));
    
    const char* json_str = json_object_to_json_string(req);
    
    char etcd_key[256];
    build_job_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t put_resp = {0};
    int status = etcd_manager_put(etcd_key, json_str, &put_resp);
    
    if (status == ETCD_OK) {
        snprintf(rv_str, sizeof(rv_str), "%lu", put_resp.revision);
        json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
        
        const char* response_json = json_object_to_json_string(req);
        strncpy(response_buffer, response_json, RESPONSE_BUFFER_SIZE - 1);
        *response_code = 201;
    } else {
        snprintf(response_buffer, RESPONSE_BUFFER_SIZE, "{\"error\":\"failed to store job\"}");
        *response_code = 500;
    }
    
    json_object_put(req);
    etcd_response_free(&put_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

int endpoint_get_job(const char* namespace, const char* name,
                     char* response_buffer, int* response_code) {
    char etcd_key[256];
    build_job_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t get_resp = {0};
    int status = etcd_manager_get(etcd_key, &get_resp);
    
    if (status == ETCD_OK && get_resp.value) {
        strncpy(response_buffer, get_resp.value, RESPONSE_BUFFER_SIZE - 1);
        *response_code = 200;
    } else {
        snprintf(response_buffer, RESPONSE_BUFFER_SIZE, "{\"error\":\"job not found\"}");
        *response_code = 404;
    }
    
    etcd_response_free(&get_resp);
    return (status == ETCD_OK && get_resp.value) ? 0 : -1;
}

int endpoint_list_jobs(const char* namespace, char* response_buffer, int* response_code) {
    char etcd_prefix[256];
    snprintf(etcd_prefix, sizeof(etcd_prefix), "/sirah/jobs/%s/",
             namespace ? namespace : "default");
    
    etcd_response_t list_resp = {0};
    int status = etcd_manager_list(etcd_prefix, &list_resp);
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("batch/v1"));
    json_object_object_add(root, "kind", json_object_new_string("JobList"));
    json_object* items = json_object_new_array();
    
    if (status == ETCD_OK && list_resp.value) {
        char* copy = strdup(list_resp.value);
        char* line = strtok(copy, "\n");
        while (line) {
            if (strlen(line) > 0) {
                json_object* job = json_tokener_parse(line);
                if (job) json_object_array_add(items, job);
            }
            line = strtok(NULL, "\n");
        }
        free(copy);
    }
    
    json_object_object_add(root, "items", items);
    const char* json_str = json_object_to_json_string(root);
    strncpy(response_buffer, json_str, RESPONSE_BUFFER_SIZE - 1);
    *response_code = 200;
    
    json_object_put(root);
    etcd_response_free(&list_resp);
    return 0;
}

int endpoint_patch_job(const char* namespace, const char* name, const char* body,
                       const char* content_type, char* response_buffer, int* response_code) {
    (void)content_type;
    
    if (!body) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"empty body\"}");
        return -1;
    }
    
    char etcd_key[256];
    build_job_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t get_resp = {0};
    int get_status = etcd_manager_get(etcd_key, &get_resp);
    
    if (get_status != ETCD_OK || !get_resp.value) {
        *response_code = 404;
        strcpy(response_buffer, "{\"error\":\"job not found\"}");
        etcd_response_free(&get_resp);
        return -1;
    }
    
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
    
    json_object* patch_spec = json_object_object_get(patch, "spec");
    if (patch_spec) {
        json_object_object_add(current, "spec", patch_spec);
    }
    
    json_object* meta = json_object_object_get(current, "metadata");
    if (meta) {
        char rv_str[32];
        snprintf(rv_str, sizeof(rv_str), "%lu", get_resp.revision + 1);
        json_object_object_add(meta, "resourceVersion", json_object_new_string(rv_str));
    }
    
    const char* merged_json = json_object_to_json_string(current);
    
    etcd_response_t patch_resp = {0};
    int patch_status = etcd_manager_patch(etcd_key, merged_json, get_resp.revision, &patch_resp);
    
    if (patch_status == ETCD_OK) {
        const char* response_json = json_object_to_json_string(current);
        strncpy(response_buffer, response_json, RESPONSE_BUFFER_SIZE - 1);
        *response_code = 200;
    } else if (patch_status == ETCD_CAS_FAILED) {
        *response_code = 409;
        strcpy(response_buffer, "{\"error\":\"Conflict\"}");
    } else {
        *response_code = 500;
        strcpy(response_buffer, "{\"error\":\"internal error\"}");
    }
    
    json_object_put(current);
    json_object_put(patch);
    etcd_response_free(&get_resp);
    etcd_response_free(&patch_resp);
    
    return (patch_status == ETCD_OK) ? 0 : -1;
}

int endpoint_delete_job(const char* namespace, const char* name,
                        char* response_buffer, int* response_code) {
    char etcd_key[256];
    build_job_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t del_resp = {0};
    int status = etcd_manager_delete(etcd_key, &del_resp);
    
    if (status == ETCD_OK) {
        *response_code = 204;
        response_buffer[0] = '\0';
    } else {
        snprintf(response_buffer, RESPONSE_BUFFER_SIZE, "{\"error\":\"failed to delete\"}");
        *response_code = 500;
    }
    
    etcd_response_free(&del_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

// ============================================================================
// LimitRange Endpoints
// ============================================================================

int endpoint_create_limitrange(const char* namespace, const char* body,
                               char* response_buffer, int* response_code) {
    if (!body || strlen(body) == 0) {
        snprintf(response_buffer, RESPONSE_BUFFER_SIZE, "{\"error\":\"empty body\"}");
        *response_code = 400;
        return -1;
    }
    
    json_object* req = json_tokener_parse(body);
    if (!req) {
        snprintf(response_buffer, RESPONSE_BUFFER_SIZE, "{\"error\":\"invalid JSON\"}");
        *response_code = 400;
        return -1;
    }
    
    json_object* metadata = json_object_object_get(req, "metadata");
    if (!metadata) {
        snprintf(response_buffer, RESPONSE_BUFFER_SIZE, "{\"error\":\"metadata required\"}");
        *response_code = 400;
        json_object_put(req);
        return -1;
    }
    
    const char* name = json_object_get_string(json_object_object_get(metadata, "name"));
    if (!name) {
        snprintf(response_buffer, RESPONSE_BUFFER_SIZE, "{\"error\":\"name required\"}");
        *response_code = 400;
        json_object_put(req);
        return -1;
    }
    
    char rv_str[32];
    snprintf(rv_str, sizeof(rv_str), "%lu", (unsigned long)time(NULL) * 1000);
    json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
    
    const char* json_str = json_object_to_json_string(req);
    
    char etcd_key[256];
    build_limitrange_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t put_resp = {0};
    int status = etcd_manager_put(etcd_key, json_str, &put_resp);
    
    if (status == ETCD_OK) {
        snprintf(rv_str, sizeof(rv_str), "%lu", put_resp.revision);
        json_object_object_add(metadata, "resourceVersion", json_object_new_string(rv_str));
        
        const char* response_json = json_object_to_json_string(req);
        strncpy(response_buffer, response_json, RESPONSE_BUFFER_SIZE - 1);
        *response_code = 201;
    } else {
        snprintf(response_buffer, RESPONSE_BUFFER_SIZE, "{\"error\":\"failed to store limitrange\"}");
        *response_code = 500;
    }
    
    json_object_put(req);
    etcd_response_free(&put_resp);
    return (status == ETCD_OK) ? 0 : -1;
}

int endpoint_get_limitrange(const char* namespace, const char* name,
                            char* response_buffer, int* response_code) {
    char etcd_key[256];
    build_limitrange_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t get_resp = {0};
    int status = etcd_manager_get(etcd_key, &get_resp);
    
    if (status == ETCD_OK && get_resp.value) {
        strncpy(response_buffer, get_resp.value, RESPONSE_BUFFER_SIZE - 1);
        *response_code = 200;
    } else {
        snprintf(response_buffer, RESPONSE_BUFFER_SIZE, "{\"error\":\"limitrange not found\"}");
        *response_code = 404;
    }
    
    etcd_response_free(&get_resp);
    return (status == ETCD_OK && get_resp.value) ? 0 : -1;
}

int endpoint_list_limitranges(const char* namespace, char* response_buffer, int* response_code) {
    char etcd_prefix[256];
    snprintf(etcd_prefix, sizeof(etcd_prefix), "/sirah/limitranges/%s/",
             namespace ? namespace : "default");
    
    etcd_response_t list_resp = {0};
    int status = etcd_manager_list(etcd_prefix, &list_resp);
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("LimitRangeList"));
    json_object* items = json_object_new_array();
    
    if (status == ETCD_OK && list_resp.value) {
        char* copy = strdup(list_resp.value);
        char* line = strtok(copy, "\n");
        while (line) {
            if (strlen(line) > 0) {
                json_object* lr = json_tokener_parse(line);
                if (lr) json_object_array_add(items, lr);
            }
            line = strtok(NULL, "\n");
        }
        free(copy);
    }
    
    json_object_object_add(root, "items", items);
    const char* json_str = json_object_to_json_string(root);
    strncpy(response_buffer, json_str, RESPONSE_BUFFER_SIZE - 1);
    *response_code = 200;
    
    json_object_put(root);
    etcd_response_free(&list_resp);
    return 0;
}

int endpoint_patch_limitrange(const char* namespace, const char* name, const char* body,
                              const char* content_type, char* response_buffer, int* response_code) {
    (void)content_type;
    
    if (!body) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"empty body\"}");
        return -1;
    }
    
    char etcd_key[256];
    build_limitrange_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t get_resp = {0};
    int get_status = etcd_manager_get(etcd_key, &get_resp);
    
    if (get_status != ETCD_OK || !get_resp.value) {
        *response_code = 404;
        strcpy(response_buffer, "{\"error\":\"limitrange not found\"}");
        etcd_response_free(&get_resp);
        return -1;
    }
    
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
    
    json_object* patch_limits = json_object_object_get(patch, "limits");
    if (patch_limits) {
        json_object_object_add(current, "limits", patch_limits);
    }
    
    json_object* meta = json_object_object_get(current, "metadata");
    if (meta) {
        char rv_str[32];
        snprintf(rv_str, sizeof(rv_str), "%lu", get_resp.revision + 1);
        json_object_object_add(meta, "resourceVersion", json_object_new_string(rv_str));
    }
    
    const char* merged_json = json_object_to_json_string(current);
    
    etcd_response_t patch_resp = {0};
    int patch_status = etcd_manager_patch(etcd_key, merged_json, get_resp.revision, &patch_resp);
    
    if (patch_status == ETCD_OK) {
        const char* response_json = json_object_to_json_string(current);
        strncpy(response_buffer, response_json, RESPONSE_BUFFER_SIZE - 1);
        *response_code = 200;
    } else if (patch_status == ETCD_CAS_FAILED) {
        *response_code = 409;
        strcpy(response_buffer, "{\"error\":\"Conflict\"}");
    } else {
        *response_code = 500;
        strcpy(response_buffer, "{\"error\":\"internal error\"}");
    }
    
    json_object_put(current);
    json_object_put(patch);
    etcd_response_free(&get_resp);
    etcd_response_free(&patch_resp);
    
    return (patch_status == ETCD_OK) ? 0 : -1;
}

int endpoint_delete_limitrange(const char* namespace, const char* name,
                               char* response_buffer, int* response_code) {
    char etcd_key[256];
    build_limitrange_etcd_key(namespace, name, etcd_key, sizeof(etcd_key));
    
    etcd_response_t del_resp = {0};
    int status = etcd_manager_delete(etcd_key, &del_resp);
    
    if (status == ETCD_OK) {
        *response_code = 204;
        response_buffer[0] = '\0';
    } else {
        snprintf(response_buffer, RESPONSE_BUFFER_SIZE, "{\"error\":\"failed to delete\"}");
        *response_code = 500;
    }
    
    etcd_response_free(&del_resp);
    return (status == ETCD_OK) ? 0 : -1;
}
