#include "events.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <json-c/json.h>

static k8s_event_store_t event_store = {0};

int event_store_init() {
    event_store.capacity = 1000;
    event_store.events = (k8s_event_t**)malloc(sizeof(k8s_event_t*) * event_store.capacity);
    event_store.num_events = 0;
    return event_store.events != NULL ? 0 : -1;
}

void event_store_shutdown() {
    for (int i = 0; i < event_store.num_events; i++) {
        if (event_store.events[i]) {
            free(event_store.events[i]->name);
            free(event_store.events[i]->namespace);
            free(event_store.events[i]->involved_object_name);
            free(event_store.events[i]->involved_object_kind);
            free(event_store.events[i]->reason);
            free(event_store.events[i]->message);
            free(event_store.events[i]);
        }
    }
    free(event_store.events);
    event_store.num_events = 0;
    event_store.capacity = 0;
}

int event_record(const char* namespace, const char* pod_name,
                const char* reason, const char* message, k8s_event_type_t type) {
    if (!namespace || !pod_name || !reason || !message) return -1;
    
    // Check if this event already exists (to increment count)
    for (int i = 0; i < event_store.num_events; i++) {
        k8s_event_t* ev = event_store.events[i];
        if (ev && strcmp(ev->namespace, namespace) == 0 &&
            strcmp(ev->involved_object_name, pod_name) == 0 &&
            strcmp(ev->reason, reason) == 0) {
            ev->count++;
            ev->last_timestamp = time(NULL);
            return 0;
        }
    }
    
    // New event
    if (event_store.num_events >= event_store.capacity) {
        // Shift old events out
        if (event_store.events[0]) {
            free(event_store.events[0]->name);
            free(event_store.events[0]->namespace);
            free(event_store.events[0]->involved_object_name);
            free(event_store.events[0]->involved_object_kind);
            free(event_store.events[0]->reason);
            free(event_store.events[0]->message);
            free(event_store.events[0]);
        }
        memmove(&event_store.events[0], &event_store.events[1], 
                sizeof(k8s_event_t*) * (event_store.capacity - 1));
        event_store.num_events--;
    }
    
    k8s_event_t* event = (k8s_event_t*)malloc(sizeof(k8s_event_t));
    event->namespace = strdup(namespace);
    event->involved_object_name = strdup(pod_name);
    event->involved_object_kind = strdup("Pod");
    event->reason = strdup(reason);
    event->message = strdup(message);
    event->event_type = type;
    event->first_timestamp = time(NULL);
    event->last_timestamp = time(NULL);
    event->count = 1;
    
    // Generate event name
    char event_name[256];
    snprintf(event_name, sizeof(event_name), "%s.%lu", pod_name, event->first_timestamp);
    event->name = strdup(event_name);
    
    event_store.events[event_store.num_events++] = event;
    return 0;
}

k8s_event_t** event_get_for_pod(const char* namespace, const char* pod_name, int* count) {
    if (!count) return NULL;
    *count = 0;
    
    k8s_event_t** result = (k8s_event_t**)malloc(sizeof(k8s_event_t*) * event_store.num_events);
    
    for (int i = 0; i < event_store.num_events; i++) {
        k8s_event_t* ev = event_store.events[i];
        if (ev && strcmp(ev->namespace, namespace) == 0 &&
            strcmp(ev->involved_object_name, pod_name) == 0) {
            result[(*count)++] = ev;
        }
    }
    
    return result;
}

char* event_to_json(k8s_event_t* event) {
    if (!event) return strdup("{}");
    
    json_object* obj = json_object_new_object();
    json_object_object_add(obj, "name", json_object_new_string(event->name));
    json_object_object_add(obj, "namespace", json_object_new_string(event->namespace));
    json_object_object_add(obj, "involvedObject", json_object_new_string(event->involved_object_name));
    json_object_object_add(obj, "reason", json_object_new_string(event->reason));
    json_object_object_add(obj, "message", json_object_new_string(event->message));
    json_object_object_add(obj, "type", json_object_new_string(
        event->event_type == EVENT_NORMAL ? "Normal" :
        event->event_type == EVENT_WARNING ? "Warning" : "Error"));
    json_object_object_add(obj, "firstTimestamp", json_object_new_int64(event->first_timestamp));
    json_object_object_add(obj, "lastTimestamp", json_object_new_int64(event->last_timestamp));
    json_object_object_add(obj, "count", json_object_new_int(event->count));
    
    const char* json_str = json_object_to_json_string(obj);
    char* result = strdup(json_str);
    json_object_put(obj);
    
    return result;
}
