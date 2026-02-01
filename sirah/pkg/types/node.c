// pkg/types/node.c
#include "node.h"
#include <json-c/json.h>
#include <string.h>
#include <stdio.h>

int node_to_json(const k8s_node_t* node, char* buffer) {
    if (!node || !buffer) return -1;
    
    json_object* obj = json_object_new_object();
    
    // Metadata
    json_object_object_add(obj, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(obj, "kind", json_object_new_string("Node"));
    
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(node->metadata.name));
    json_object_object_add(metadata, "namespace", json_object_new_string(node->metadata.namespace));
    json_object_object_add(obj, "metadata", metadata);
    
    // Spec
    json_object* spec = json_object_new_object();
    json_object_object_add(spec, "hostname", json_object_new_string(node->capacity.hostname));
    json_object_object_add(obj, "spec", spec);
    
    // Status
    json_object* status = json_object_new_object();
    json_object_object_add(status, "addresses", json_object_new_array());
    json_object* addr = json_object_new_object();
    json_object_object_add(addr, "type", json_object_new_string("InternalIP"));
    json_object_object_add(addr, "address", json_object_new_string(node->capacity.ip_address));
    json_object_array_add(json_object_object_get(status, "addresses"), addr);
    
    json_object_object_add(status, "conditions", json_object_new_array());
    json_object_object_add(status, "capacity", json_object_new_object());
    json_object_object_add(status, "allocatable", json_object_new_object());
    json_object_object_add(obj, "status", status);
    
    const char* json_str = json_object_to_json_string(obj);
    strcpy(buffer, json_str);
    
    json_object_put(obj);
    return 0;
}

int node_from_json(const char* json_str, k8s_node_t* node) {
    if (!json_str || !node) return -1;
    
    json_object* obj = json_tokener_parse(json_str);
    if (!obj) return -1;
    
    // Parse metadata
    json_object* metadata = NULL;
    if (json_object_object_get_ex(obj, "metadata", &metadata)) {
        json_object* name_obj = NULL;
        if (json_object_object_get_ex(metadata, "name", &name_obj)) {
            const char* name = json_object_get_string(name_obj);
            strncpy(node->metadata.name, name, sizeof(node->metadata.name) - 1);
        }
    }
    
    // Parse spec for hostname
    json_object* spec = NULL;
    if (json_object_object_get_ex(obj, "spec", &spec)) {
        json_object* hostname_obj = NULL;
        if (json_object_object_get_ex(spec, "hostname", &hostname_obj)) {
            const char* hostname = json_object_get_string(hostname_obj);
            strncpy(node->capacity.hostname, hostname, sizeof(node->capacity.hostname) - 1);
        }
    }
    
    json_object_put(obj);
    return 0;
}
