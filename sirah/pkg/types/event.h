#ifndef SIRAH_TYPES_EVENT_H
#define SIRAH_TYPES_EVENT_H

#include "common.h"
#include <time.h>

// Event object type
typedef struct {
    k8s_metadata_t metadata;
    
    // Involved object (what the event is about)
    struct {
        char* api_version;
        char* kind;
        char* name;
        char* namespace;
        char* uid;
    } involved_object;
    
    // Event details
    char* reason;
    char* message;
    char* type;  // "Normal" or "Warning"
    
    time_t first_timestamp;
    time_t last_timestamp;
    int count;
    
    // Source
    struct {
        char* component;
        char* host;
    } source;
} k8s_event_t;

// Event operations
k8s_event_t* k8s_event_new(const char* name, const char* namespace,
                           const char* involved_object_name, const char* involved_object_kind);
void k8s_event_free(k8s_event_t* event);
char* k8s_event_to_json(k8s_event_t* event);
k8s_event_t* k8s_event_from_json(const char* json_str);

int k8s_event_set_reason(k8s_event_t* event, const char* reason);
int k8s_event_set_message(k8s_event_t* event, const char* message);
int k8s_event_set_type(k8s_event_t* event, const char* type);

#endif
