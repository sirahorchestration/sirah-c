#ifndef SIRAH_EVENTS_H
#define SIRAH_EVENTS_H

#include <time.h>
#include "../../pkg/types/common.h"

// Event types
typedef enum {
    EVENT_NORMAL = 0,
    EVENT_WARNING = 1,
    EVENT_ERROR = 2
} k8s_event_type_t;

// Pod event structure
typedef struct {
    char* name;                    // Event name (e.g., "pod-created", "pod-started")
    char* namespace;
    char* involved_object_name;    // Pod name
    char* involved_object_kind;    // "Pod", "Node", "Deployment"
    char* reason;                  // "Created", "Started", "Failed", etc.
    char* message;                 // Human-readable message
    k8s_event_type_t event_type;   // NORMAL, WARNING, ERROR
    time_t first_timestamp;
    time_t last_timestamp;
    int count;                     // How many times this event occurred
} k8s_event_t;

// Event tracking
typedef struct {
    k8s_event_t** events;
    int num_events;
    int capacity;
} k8s_event_store_t;

// Event operations
int event_store_init();
void event_store_shutdown();
int event_record(const char* namespace, const char* pod_name, 
                const char* reason, const char* message, k8s_event_type_t type);
k8s_event_t** event_get_for_pod(const char* namespace, const char* pod_name, int* count);
char* event_to_json(k8s_event_t* event);

#endif
