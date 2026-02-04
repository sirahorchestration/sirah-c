// internal/apiserver/event_system.h
// Event system with buffering and retention for watch subscriptions
#ifndef SIRAH_EVENT_SYSTEM_H
#define SIRAH_EVENT_SYSTEM_H

#include <json-c/json.h>
#include <time.h>
#include <stdint.h>

// Event type
typedef enum {
    EVENT_TYPE_ADDED = 0,
    EVENT_TYPE_MODIFIED = 1,
    EVENT_TYPE_DELETED = 2,
    EVENT_TYPE_ERROR = 3,
    EVENT_TYPE_BOOKMARK = 4
} event_type_t;

// Event structure
typedef struct {
    event_type_t type;
    char* resource_type;          // "pods", "services", "deployments"
    char* namespace;
    char* name;                   // Resource name
    json_object* object;          // Full resource object
    uint64_t resource_version;    // From the object
    time_t timestamp;
} event_t;

// Event buffer for a watch subscription
typedef struct {
    event_t** events;
    int count;
    int capacity;
    int read_index;
    time_t creation_time;
    uint64_t resume_version;      // For BOOKMARK events
} event_buffer_t;

// Initialize event system
int event_system_init(void);
void event_system_shutdown(void);

// Create a new event
event_t* event_new(event_type_t type, const char* resource_type, 
                  const char* namespace, const char* name, 
                  json_object* object);

// Free an event
void event_free(event_t* event);

// Create event buffer for watch subscription
event_buffer_t* event_buffer_new(int capacity);

// Free event buffer and all events
void event_buffer_free(event_buffer_t* buffer);

// Add event to buffer
// Returns 0 on success, -1 on buffer full
int event_buffer_add(event_buffer_t* buffer, event_t* event);

// Get next event from buffer
// Returns NULL if no more events
event_t* event_buffer_next(event_buffer_t* buffer);

// Reset buffer read index (for rewinding)
void event_buffer_reset(event_buffer_t* buffer);

// Check if buffer has unread events
int event_buffer_has_events(event_buffer_t* buffer);

// Send event to all active watch subscriptions
// This is called by controllers/handlers when resource changes
int event_dispatch(event_t* event);

// Convert event to NDJSON format (newline-delimited JSON)
// Returns allocated string that must be freed by caller
char* event_to_ndjson(event_t* event);

// Convert event to watch event format (type + object)
// Returns allocated JSON object that must be freed by caller
json_object* event_to_watch_json(event_t* event);

// Create a BOOKMARK event for resume position
event_t* event_bookmark_new(uint64_t resource_version);

#endif // SIRAH_EVENT_SYSTEM_H
