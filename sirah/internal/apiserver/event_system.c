// internal/apiserver/event_system.c
// Event system with buffering and retention for watch subscriptions
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <json-c/json.h>
#include "event_system.h"

// Global active event buffers for subscriptions
#define MAX_ACTIVE_SUBSCRIPTIONS 1000
typedef struct {
    event_buffer_t* buffer;
    int active;
    char resource_type[64];
    char namespace[64];
} subscription_t;

static subscription_t subscriptions[MAX_ACTIVE_SUBSCRIPTIONS];
static pthread_mutex_t subscriptions_lock = PTHREAD_MUTEX_INITIALIZER;
static int initialized = 0;

// Initialize event system
int event_system_init(void) {
    if (initialized) return 0;
    
    pthread_mutex_lock(&subscriptions_lock);
    memset(subscriptions, 0, sizeof(subscriptions));
    initialized = 1;
    pthread_mutex_unlock(&subscriptions_lock);
    
    return 0;
}

void event_system_shutdown(void) {
    pthread_mutex_lock(&subscriptions_lock);
    for (int i = 0; i < MAX_ACTIVE_SUBSCRIPTIONS; i++) {
        if (subscriptions[i].active && subscriptions[i].buffer) {
            event_buffer_free(subscriptions[i].buffer);
            subscriptions[i].active = 0;
        }
    }
    initialized = 0;
    pthread_mutex_unlock(&subscriptions_lock);
}

// Create a new event
event_t* event_new(event_type_t type, const char* resource_type, 
                  const char* namespace, const char* name, 
                  json_object* object) {
    event_t* event = malloc(sizeof(event_t));
    if (!event) return NULL;
    
    memset(event, 0, sizeof(event_t));
    event->type = type;
    event->timestamp = time(NULL);
    
    if (resource_type) {
        event->resource_type = malloc(strlen(resource_type) + 1);
        strcpy(event->resource_type, resource_type);
    }
    
    if (namespace) {
        event->namespace = malloc(strlen(namespace) + 1);
        strcpy(event->namespace, namespace);
    }
    
    if (name) {
        event->name = malloc(strlen(name) + 1);
        strcpy(event->name, name);
    }
    
    // Deep copy the object
    if (object) {
        const char* json_str = json_object_to_json_string(object);
        event->object = json_tokener_parse(json_str);
        
        // Extract resourceVersion from object
        json_object* metadata = NULL;
        if (json_object_object_get_ex(event->object, "metadata", &metadata)) {
            json_object* rv = NULL;
            if (json_object_object_get_ex(metadata, "resourceVersion", &rv)) {
                const char* rv_str = json_object_get_string(rv);
                if (rv_str) {
                    event->resource_version = strtoull(rv_str, NULL, 10);
                }
            }
        }
    }
    
    return event;
}

// Free an event
void event_free(event_t* event) {
    if (!event) return;
    
    if (event->resource_type) free(event->resource_type);
    if (event->namespace) free(event->namespace);
    if (event->name) free(event->name);
    if (event->object) json_object_put(event->object);
    
    free(event);
}

// Create event buffer for watch subscription
event_buffer_t* event_buffer_new(int capacity) {
    event_buffer_t* buffer = malloc(sizeof(event_buffer_t));
    if (!buffer) return NULL;
    
    buffer->events = malloc(sizeof(event_t*) * capacity);
    if (!buffer->events) {
        free(buffer);
        return NULL;
    }
    
    memset(buffer->events, 0, sizeof(event_t*) * capacity);
    buffer->count = 0;
    buffer->capacity = capacity;
    buffer->read_index = 0;
    buffer->creation_time = time(NULL);
    buffer->resume_version = 0;
    
    return buffer;
}

// Free event buffer
void event_buffer_free(event_buffer_t* buffer) {
    if (!buffer) return;
    
    for (int i = 0; i < buffer->count; i++) {
        if (buffer->events[i]) {
            event_free(buffer->events[i]);
        }
    }
    
    free(buffer->events);
    free(buffer);
}

// Add event to buffer
int event_buffer_add(event_buffer_t* buffer, event_t* event) {
    if (!buffer || !event) return -1;
    
    if (buffer->count >= buffer->capacity) {
        // Buffer full - remove oldest event and shift
        event_free(buffer->events[0]);
        for (int i = 1; i < buffer->count; i++) {
            buffer->events[i - 1] = buffer->events[i];
        }
        buffer->count--;
        if (buffer->read_index > 0) {
            buffer->read_index--;
        }
    }
    
    buffer->events[buffer->count] = event;
    buffer->count++;
    buffer->resume_version = event->resource_version;
    
    return 0;
}

// Get next event from buffer
event_t* event_buffer_next(event_buffer_t* buffer) {
    if (!buffer) return NULL;
    
    if (buffer->read_index >= buffer->count) {
        return NULL;  // No more events
    }
    
    event_t* event = buffer->events[buffer->read_index];
    buffer->read_index++;
    return event;
}

// Reset buffer read index
void event_buffer_reset(event_buffer_t* buffer) {
    if (buffer) {
        buffer->read_index = 0;
    }
}

// Check if buffer has unread events
int event_buffer_has_events(event_buffer_t* buffer) {
    if (!buffer) return 0;
    return buffer->read_index < buffer->count;
}

// Send event to all active subscriptions
int event_dispatch(event_t* event) {
    if (!event) return -1;
    
    pthread_mutex_lock(&subscriptions_lock);
    
    for (int i = 0; i < MAX_ACTIVE_SUBSCRIPTIONS; i++) {
        if (subscriptions[i].active && subscriptions[i].buffer) {
            // Check if subscription matches resource type and namespace
            if (strcmp(subscriptions[i].resource_type, event->resource_type) == 0 &&
                (strlen(subscriptions[i].namespace) == 0 || 
                 strcmp(subscriptions[i].namespace, event->namespace) == 0)) {
                
                // Create a copy of the event for this subscription
                event_t* copy = event_new(event->type, event->resource_type,
                                         event->namespace, event->name, event->object);
                if (copy) {
                    event_buffer_add(subscriptions[i].buffer, copy);
                }
            }
        }
    }
    
    pthread_mutex_unlock(&subscriptions_lock);
    return 0;
}

// Convert event to NDJSON format
char* event_to_ndjson(event_t* event) {
    if (!event) return NULL;
    
    json_object* watch_event = json_object_new_object();
    
    // Add type
    const char* type_str = "ADDED";
    switch (event->type) {
        case EVENT_TYPE_ADDED: type_str = "ADDED"; break;
        case EVENT_TYPE_MODIFIED: type_str = "MODIFIED"; break;
        case EVENT_TYPE_DELETED: type_str = "DELETED"; break;
        case EVENT_TYPE_ERROR: type_str = "ERROR"; break;
        case EVENT_TYPE_BOOKMARK: type_str = "BOOKMARK"; break;
    }
    json_object_object_add(watch_event, "type", json_object_new_string(type_str));
    
    // Add object (deep copy)
    if (event->object) {
        const char* obj_str = json_object_to_json_string(event->object);
        json_object* obj_copy = json_tokener_parse(obj_str);
        json_object_object_add(watch_event, "object", obj_copy);
    }
    
    // Serialize to NDJSON (JSON on single line)
    const char* json_str = json_object_to_json_string_ext(watch_event, JSON_C_TO_STRING_PLAIN);
    char* result = malloc(strlen(json_str) + 2);  // +1 for newline, +1 for null
    if (result) {
        sprintf(result, "%s\n", json_str);
    }
    
    json_object_put(watch_event);
    return result;
}

// Convert event to watch event format
json_object* event_to_watch_json(event_t* event) {
    if (!event) return NULL;
    
    json_object* watch_event = json_object_new_object();
    
    // Add type
    const char* type_str = "ADDED";
    switch (event->type) {
        case EVENT_TYPE_ADDED: type_str = "ADDED"; break;
        case EVENT_TYPE_MODIFIED: type_str = "MODIFIED"; break;
        case EVENT_TYPE_DELETED: type_str = "DELETED"; break;
        case EVENT_TYPE_ERROR: type_str = "ERROR"; break;
        case EVENT_TYPE_BOOKMARK: type_str = "BOOKMARK"; break;
    }
    json_object_object_add(watch_event, "type", json_object_new_string(type_str));
    
    // Add object (deep copy)
    if (event->object) {
        const char* obj_str = json_object_to_json_string(event->object);
        json_object* obj_copy = json_tokener_parse(obj_str);
        json_object_object_add(watch_event, "object", obj_copy);
    }
    
    return watch_event;
}

// Create a BOOKMARK event
event_t* event_bookmark_new(uint64_t resource_version) {
    event_t* event = malloc(sizeof(event_t));
    if (!event) return NULL;
    
    memset(event, 0, sizeof(event_t));
    event->type = EVENT_TYPE_BOOKMARK;
    event->timestamp = time(NULL);
    event->resource_version = resource_version;
    
    // Create a minimal BOOKMARK object
    json_object* obj = json_object_new_object();
    json_object* metadata = json_object_new_object();
    
    char version_str[32];
    snprintf(version_str, sizeof(version_str), "%" PRIu64, resource_version);
    json_object_object_add(metadata, "resourceVersion", json_object_new_string(version_str));
    json_object_object_add(obj, "metadata", metadata);
    
    event->object = obj;
    
    return event;
}

// Register a watch subscription (internal use)
int event_register_subscription(event_buffer_t* buffer, const char* resource_type, const char* namespace) {
    if (!buffer) return -1;
    
    pthread_mutex_lock(&subscriptions_lock);
    
    int slot = -1;
    for (int i = 0; i < MAX_ACTIVE_SUBSCRIPTIONS; i++) {
        if (!subscriptions[i].active) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        pthread_mutex_unlock(&subscriptions_lock);
        return -1;  // No free slots
    }
    
    subscriptions[slot].active = 1;
    subscriptions[slot].buffer = buffer;
    strncpy(subscriptions[slot].resource_type, resource_type, sizeof(subscriptions[slot].resource_type) - 1);
    if (namespace) {
        strncpy(subscriptions[slot].namespace, namespace, sizeof(subscriptions[slot].namespace) - 1);
    }
    
    pthread_mutex_unlock(&subscriptions_lock);
    return slot;
}

// Unregister a watch subscription (internal use)
void event_unregister_subscription(int subscription_id) {
    if (subscription_id < 0 || subscription_id >= MAX_ACTIVE_SUBSCRIPTIONS) return;
    
    pthread_mutex_lock(&subscriptions_lock);
    subscriptions[subscription_id].active = 0;
    pthread_mutex_unlock(&subscriptions_lock);
}
