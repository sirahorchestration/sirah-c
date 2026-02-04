// internal/apiserver/watch_manager.h
// Watch subscription manager with NDJSON streaming and filtering
#ifndef SIRAH_WATCH_MANAGER_H
#define SIRAH_WATCH_MANAGER_H

#include <json-c/json.h>
#include "event_system.h"

// Watch subscription
typedef struct {
    int id;
    char* resource_type;
    char* namespace;
    char* label_selector;
    char* field_selector;
    event_buffer_t* event_buffer;
    int active;
    unsigned long start_resource_version;
} watch_subscription_t;

// Initialize watch manager
int watch_manager_init(void);
void watch_manager_shutdown(void);

// Create a new watch subscription
// Returns subscription ID, -1 on error
int watch_create(const char* resource_type, const char* namespace,
                const char* label_selector, const char* field_selector,
                unsigned long from_version);

// Get next event from watch subscription (NDJSON format)
// Returns allocated NDJSON string, NULL if no events available
char* watch_next_ndjson(int subscription_id);

// Check if watch has events ready
int watch_has_events(int subscription_id);

// Close watch subscription
int watch_close(int subscription_id);

// Send a resource change event to all matching watchers
// Called by API handlers after create/update/delete operations
int watch_notify_change(const char* resource_type, const char* namespace,
                       const char* name, event_type_t event_type, 
                       json_object* object);

// Send BOOKMARK event to specific watch (for resuming)
int watch_send_bookmark(int subscription_id, uint64_t resource_version);

// Helper: Check if label selector matches resource labels
int watch_match_label_selector(json_object* object, const char* label_selector);

// Helper: Check if field selector matches resource fields
int watch_match_field_selector(json_object* object, const char* field_selector);

#endif // SIRAH_WATCH_MANAGER_H
