#include "event.h"
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>

k8s_event_t* k8s_event_new(const char* name, const char* namespace,
                           const char* involved_object_name, const char* involved_object_kind) {
    if (!name || !namespace || !involved_object_name) return NULL;
    
    k8s_event_t* event = (k8s_event_t*)malloc(sizeof(k8s_event_t));
    
    // Set metadata
    event->metadata = *k8s_metadata_new(name, namespace);
    
    // Set involved object
    event->involved_object.api_version = strdup("v1");
    event->involved_object.kind = strdup(involved_object_kind ? involved_object_kind : "Pod");
    event->involved_object.name = strdup(involved_object_name);
    event->involved_object.namespace = strdup(namespace);
    event->involved_object.uid = strdup("");
    
    // Initialize fields
    event->reason = strdup("");
    event->message = strdup("");
    event->type = strdup("Normal");
    event->first_timestamp = time(NULL);
    event->last_timestamp = time(NULL);
    event->count = 1;
    
    event->source.component = strdup("kubelet");
    event->source.host = strdup("localhost");
    
    return event;
}

void k8s_event_free(k8s_event_t* event) {
    if (!event) return;
    
    k8s_metadata_free(&event->metadata);
    free(event->involved_object.api_version);
    free(event->involved_object.kind);
    free(event->involved_object.name);
    free(event->involved_object.namespace);
    free(event->involved_object.uid);
    free(event->reason);
    free(event->message);
    free(event->type);
    free(event->source.component);
    free(event->source.host);
    
    free(event);
}

char* k8s_event_to_json(k8s_event_t* event) {
    if (!event) return strdup("{}");
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("Event"));
    
    // Metadata
    json_object* meta = json_object_new_object();
    json_object_object_add(meta, "name", json_object_new_string(event->metadata.name));
    json_object_object_add(meta, "namespace", json_object_new_string(event->metadata.namespace));
    json_object_object_add(root, "metadata", meta);
    
    // Involved object
    json_object* involved = json_object_new_object();
    json_object_object_add(involved, "apiVersion", json_object_new_string(event->involved_object.api_version));
    json_object_object_add(involved, "kind", json_object_new_string(event->involved_object.kind));
    json_object_object_add(involved, "name", json_object_new_string(event->involved_object.name));
    json_object_object_add(involved, "namespace", json_object_new_string(event->involved_object.namespace));
    json_object_object_add(root, "involvedObject", involved);
    
    // Event details
    json_object_object_add(root, "reason", json_object_new_string(event->reason));
    json_object_object_add(root, "message", json_object_new_string(event->message));
    json_object_object_add(root, "type", json_object_new_string(event->type));
    json_object_object_add(root, "count", json_object_new_int(event->count));
    json_object_object_add(root, "firstTimestamp", json_object_new_int64(event->first_timestamp));
    json_object_object_add(root, "lastTimestamp", json_object_new_int64(event->last_timestamp));
    
    // Source
    json_object* source = json_object_new_object();
    json_object_object_add(source, "component", json_object_new_string(event->source.component));
    json_object_object_add(source, "host", json_object_new_string(event->source.host));
    json_object_object_add(root, "source", source);
    
    char* result = strdup(json_object_to_json_string(root));
    json_object_put(root);
    return result;
}

k8s_event_t* k8s_event_from_json(const char* json_str) {
    if (!json_str) return NULL;
    
    json_object* root = json_tokener_parse(json_str);
    if (!root) return NULL;
    
    json_object* meta = json_object_object_get(root, "metadata");
    json_object* involved = json_object_object_get(root, "involvedObject");
    
    if (!meta || !involved) {
        json_object_put(root);
        return NULL;
    }
    
    const char* name = json_object_get_string(json_object_object_get(meta, "name"));
    const char* ns = json_object_get_string(json_object_object_get(meta, "namespace"));
    const char* obj_name = json_object_get_string(json_object_object_get(involved, "name"));
    const char* obj_kind = json_object_get_string(json_object_object_get(involved, "kind"));
    
    k8s_event_t* event = k8s_event_new(name ? name : "event", ns ? ns : "default", 
                                       obj_name ? obj_name : "unknown", 
                                       obj_kind ? obj_kind : "Pod");
    
    json_object_put(root);
    return event;
}

int k8s_event_set_reason(k8s_event_t* event, const char* reason) {
    if (!event || !reason) return -1;
    free(event->reason);
    event->reason = strdup(reason);
    return 0;
}

int k8s_event_set_message(k8s_event_t* event, const char* message) {
    if (!event || !message) return -1;
    free(event->message);
    event->message = strdup(message);
    return 0;
}

int k8s_event_set_type(k8s_event_t* event, const char* type) {
    if (!event || !type) return -1;
    free(event->type);
    event->type = strdup(type);
    return 0;
}
