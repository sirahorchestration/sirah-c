// internal/apiserver/watch.h
// Watch API implementation for real-time updates
#ifndef SIRAH_WATCH_H
#define SIRAH_WATCH_H

#include <json-c/json.h>

typedef enum {
    WATCH_EVENT_ADDED,
    WATCH_EVENT_MODIFIED,
    WATCH_EVENT_DELETED,
    WATCH_EVENT_ERROR,
    WATCH_EVENT_BOOKMARK
} watch_event_type_t;

typedef struct {
    watch_event_type_t type;
    json_object* object;
    char error_message[512];
    unsigned long resource_version;
} watch_event_t;

// Start watching resources
// Returns watch session ID, or -1 on error
int watch_start(const char* resource_type, const char* namespace,
                const char* label_selector, const char* field_selector);

// Get next event from watch session
// Returns 0 if event available, 1 if timeout, -1 on error
int watch_next_event(int watch_id, watch_event_t* event, int timeout_ms);

// Stop watching
int watch_stop(int watch_id);

// Add event to all active watchers (called by controllers/kubelet)
int watch_notify_all(const char* resource_type, const char* namespace,
                     watch_event_type_t event_type, json_object* object);

#endif // SIRAH_WATCH_H
