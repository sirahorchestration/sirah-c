#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include <time.h>

// ============ ConfigMap ==============

k8s_configmap_t* k8s_configmap_new(const char* name, const char* namespace) {
    if (!name || !namespace) return NULL;
    
    k8s_configmap_t* cm = (k8s_configmap_t*)malloc(sizeof(k8s_configmap_t));
    
    cm->metadata = (k8s_metadata_t){
        .name = strdup(name),
        .namespace = strdup(namespace),
        .uid = strdup(""),
        .resource_version = strdup("1"),
        .creation_timestamp = time(NULL),
        .deletion_timestamp = 0,
        .owner_references = NULL,
        .num_owners = 0,
        .finalizers = NULL,
        .num_finalizers = 0
    };
    
    cm->keys = (char**)malloc(sizeof(char*) * 100);
    cm->values = (char**)malloc(sizeof(char*) * 100);
    cm->num_items = 0;
    
    return cm;
}

void k8s_configmap_free(k8s_configmap_t* cm) {
    if (!cm) return;
    
    free(cm->metadata.name);
    free(cm->metadata.namespace);
    free(cm->metadata.uid);
    free(cm->metadata.resource_version);
    
    for (int i = 0; i < cm->num_items; i++) {
        free(cm->keys[i]);
        free(cm->values[i]);
    }
    free(cm->keys);
    free(cm->values);
    free(cm);
}

char* k8s_configmap_to_json(k8s_configmap_t* cm) {
    if (!cm) return strdup("{}");
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("ConfigMap"));
    
    json_object* meta = json_object_new_object();
    json_object_object_add(meta, "name", json_object_new_string(cm->metadata.name));
    json_object_object_add(meta, "namespace", json_object_new_string(cm->metadata.namespace));
    json_object_object_add(root, "metadata", meta);
    
    json_object* data = json_object_new_object();
    for (int i = 0; i < cm->num_items; i++) {
        json_object_object_add(data, cm->keys[i], json_object_new_string(cm->values[i]));
    }
    json_object_object_add(root, "data", data);
    
    const char* json_str = json_object_to_json_string(root);
    char* result = strdup(json_str);
    json_object_put(root);
    
    return result;
}

k8s_configmap_t* k8s_configmap_from_json(const char* json_str) {
    if (!json_str) return NULL;
    
    json_object* root = json_tokener_parse(json_str);
    if (!root) return NULL;
    
    json_object* meta = json_object_object_get(root, "metadata");
    const char* name = json_object_get_string(json_object_object_get(meta, "name"));
    const char* ns = json_object_get_string(json_object_object_get(meta, "namespace"));
    
    k8s_configmap_t* cm = k8s_configmap_new(name, ns);
    
    // Skip detailed parsing for now - just return empty cm
    json_object_put(root);
    return cm;
}

int k8s_configmap_set(k8s_configmap_t* cm, const char* key, const char* value) {
    if (!cm || !key || !value || cm->num_items >= 100) return -1;
    
    cm->keys[cm->num_items] = strdup(key);
    cm->values[cm->num_items] = strdup(value);
    cm->num_items++;
    
    return 0;
}

const char* k8s_configmap_get(k8s_configmap_t* cm, const char* key) {
    if (!cm || !key) return NULL;
    
    for (int i = 0; i < cm->num_items; i++) {
        if (strcmp(cm->keys[i], key) == 0) {
            return cm->values[i];
        }
    }
    
    return NULL;
}

// ============ Secret ==============

k8s_secret_t* k8s_secret_new(const char* name, const char* namespace, const char* type) {
    if (!name || !namespace) return NULL;
    
    k8s_secret_t* secret = (k8s_secret_t*)malloc(sizeof(k8s_secret_t));
    
    secret->metadata = (k8s_metadata_t){
        .name = strdup(name),
        .namespace = strdup(namespace),
        .uid = strdup(""),
        .resource_version = strdup("1"),
        .creation_timestamp = time(NULL),
        .deletion_timestamp = 0,
        .owner_references = NULL,
        .num_owners = 0,
        .finalizers = NULL,
        .num_finalizers = 0
    };
    
    secret->type = strdup(type ? type : "Opaque");
    secret->keys = (char**)malloc(sizeof(char*) * 100);
    secret->values = (char**)malloc(sizeof(char*) * 100);
    secret->num_items = 0;
    
    return secret;
}

void k8s_secret_free(k8s_secret_t* secret) {
    if (!secret) return;
    
    free(secret->metadata.name);
    free(secret->metadata.namespace);
    free(secret->metadata.uid);
    free(secret->metadata.resource_version);
    free(secret->type);
    
    for (int i = 0; i < secret->num_items; i++) {
        free(secret->keys[i]);
        free(secret->values[i]);
    }
    free(secret->keys);
    free(secret->values);
    free(secret);
}

char* k8s_secret_to_json(k8s_secret_t* secret) {
    if (!secret) return strdup("{}");
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("Secret"));
    
    json_object* meta = json_object_new_object();
    json_object_object_add(meta, "name", json_object_new_string(secret->metadata.name));
    json_object_object_add(meta, "namespace", json_object_new_string(secret->metadata.namespace));
    json_object_object_add(root, "metadata", meta);
    
    json_object_object_add(root, "type", json_object_new_string(secret->type));
    
    json_object* data = json_object_new_object();
    for (int i = 0; i < secret->num_items; i++) {
        json_object_object_add(data, secret->keys[i], json_object_new_string(secret->values[i]));
    }
    json_object_object_add(root, "data", data);
    
    const char* json_str = json_object_to_json_string(root);
    char* result = strdup(json_str);
    json_object_put(root);
    
    return result;
}

k8s_secret_t* k8s_secret_from_json(const char* json_str) {
    if (!json_str) return NULL;
    
    json_object* root = json_tokener_parse(json_str);
    if (!root) return NULL;
    
    json_object* meta = json_object_object_get(root, "metadata");
    const char* name = json_object_get_string(json_object_object_get(meta, "name"));
    const char* ns = json_object_get_string(json_object_object_get(meta, "namespace"));
    json_object* type_obj = json_object_object_get(root, "type");
    const char* type = type_obj ? json_object_get_string(type_obj) : "Opaque";
    
    k8s_secret_t* secret = k8s_secret_new(name, ns, type);
    
    // Skip detailed parsing for now - just return empty secret
    json_object_put(root);
    return secret;
}

int k8s_secret_set(k8s_secret_t* secret, const char* key, const char* value) {
    if (!secret || !key || !value || secret->num_items >= 100) return -1;
    
    secret->keys[secret->num_items] = strdup(key);
    secret->values[secret->num_items] = strdup(value);
    secret->num_items++;
    
    return 0;
}

const char* k8s_secret_get(k8s_secret_t* secret, const char* key) {
    if (!secret || !key) return NULL;
    
    for (int i = 0; i < secret->num_items; i++) {
        if (strcmp(secret->keys[i], key) == 0) {
            return secret->values[i];
        }
    }
    
    return NULL;
}
