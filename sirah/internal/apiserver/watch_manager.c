// internal/apiserver/watch_manager.c
// Watch subscription manager with NDJSON streaming and filtering
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <json-c/json.h>
#include "watch_manager.h"
#include "event_system.h"

#define MAX_WATCH_SUBSCRIPTIONS 1000

static watch_subscription_t subscriptions[MAX_WATCH_SUBSCRIPTIONS];
static pthread_mutex_t subscriptions_lock = PTHREAD_MUTEX_INITIALIZER;
static int initialized = 0;
static int next_subscription_id = 1;

// Initialize watch manager
int watch_manager_init(void) {
    if (initialized) return 0;
    
    pthread_mutex_lock(&subscriptions_lock);
    memset(subscriptions, 0, sizeof(subscriptions));
    initialized = 1;
    pthread_mutex_unlock(&subscriptions_lock);
    
    event_system_init();
    return 0;
}

void watch_manager_shutdown(void) {
    pthread_mutex_lock(&subscriptions_lock);
    for (int i = 0; i < MAX_WATCH_SUBSCRIPTIONS; i++) {
        if (subscriptions[i].active && subscriptions[i].event_buffer) {
            event_buffer_free(subscriptions[i].event_buffer);
        }
        if (subscriptions[i].resource_type) free(subscriptions[i].resource_type);
        if (subscriptions[i].namespace) free(subscriptions[i].namespace);
        if (subscriptions[i].label_selector) free(subscriptions[i].label_selector);
        if (subscriptions[i].field_selector) free(subscriptions[i].field_selector);
    }
    initialized = 0;
    pthread_mutex_unlock(&subscriptions_lock);
    
    event_system_shutdown();
}

// Create a new watch subscription
int watch_create(const char* resource_type, const char* namespace,
                const char* label_selector, const char* field_selector,
                unsigned long from_version) {
    if (!resource_type) return -1;
    
    watch_manager_init();  // Ensure initialized
    
    pthread_mutex_lock(&subscriptions_lock);
    
    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_WATCH_SUBSCRIPTIONS; i++) {
        if (!subscriptions[i].active) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        pthread_mutex_unlock(&subscriptions_lock);
        return -1;  // No free slots
    }
    
    // Initialize subscription
    subscriptions[slot].id = next_subscription_id++;
    subscriptions[slot].active = 1;
    subscriptions[slot].start_resource_version = from_version;
    subscriptions[slot].event_buffer = event_buffer_new(1000);  // 1000 event buffer
    
    if (!subscriptions[slot].event_buffer) {
        pthread_mutex_unlock(&subscriptions_lock);
        return -1;
    }
    
    // Copy strings
    subscriptions[slot].resource_type = malloc(strlen(resource_type) + 1);
    strcpy(subscriptions[slot].resource_type, resource_type);
    
    if (namespace) {
        subscriptions[slot].namespace = malloc(strlen(namespace) + 1);
        strcpy(subscriptions[slot].namespace, namespace);
    } else {
        subscriptions[slot].namespace = malloc(1);
        subscriptions[slot].namespace[0] = '\0';
    }
    
    if (label_selector) {
        subscriptions[slot].label_selector = malloc(strlen(label_selector) + 1);
        strcpy(subscriptions[slot].label_selector, label_selector);
    }
    
    if (field_selector) {
        subscriptions[slot].field_selector = malloc(strlen(field_selector) + 1);
        strcpy(subscriptions[slot].field_selector, field_selector);
    }
    
    pthread_mutex_unlock(&subscriptions_lock);
    
    // Register with event system
    event_register_subscription(subscriptions[slot].event_buffer, 
                               resource_type, namespace);
    
    return subscriptions[slot].id;
}

// Get next event from watch subscription (NDJSON format)
char* watch_next_ndjson(int subscription_id) {
    pthread_mutex_lock(&subscriptions_lock);
    
    // Find subscription by ID
    int slot = -1;
    for (int i = 0; i < MAX_WATCH_SUBSCRIPTIONS; i++) {
        if (subscriptions[i].active && subscriptions[i].id == subscription_id) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        pthread_mutex_unlock(&subscriptions_lock);
        return NULL;
    }
    
    // Get next event
    event_t* event = event_buffer_next(subscriptions[slot].event_buffer);
    pthread_mutex_unlock(&subscriptions_lock);
    
    if (!event) return NULL;
    
    // Apply filters
    if (subscriptions[slot].label_selector && 
        !watch_match_label_selector(event->object, subscriptions[slot].label_selector)) {
        // Event doesn't match label selector, skip it
        return NULL;
    }
    
    if (subscriptions[slot].field_selector && 
        !watch_match_field_selector(event->object, subscriptions[slot].field_selector)) {
        // Event doesn't match field selector, skip it
        return NULL;
    }
    
    // Convert to NDJSON
    return event_to_ndjson(event);
}

// Check if watch has events ready
int watch_has_events(int subscription_id) {
    pthread_mutex_lock(&subscriptions_lock);
    
    int slot = -1;
    for (int i = 0; i < MAX_WATCH_SUBSCRIPTIONS; i++) {
        if (subscriptions[i].active && subscriptions[i].id == subscription_id) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        pthread_mutex_unlock(&subscriptions_lock);
        return 0;
    }
    
    int has_events = event_buffer_has_events(subscriptions[slot].event_buffer);
    pthread_mutex_unlock(&subscriptions_lock);
    
    return has_events;
}

// Close watch subscription
int watch_close(int subscription_id) {
    pthread_mutex_lock(&subscriptions_lock);
    
    int slot = -1;
    for (int i = 0; i < MAX_WATCH_SUBSCRIPTIONS; i++) {
        if (subscriptions[i].active && subscriptions[i].id == subscription_id) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        pthread_mutex_unlock(&subscriptions_lock);
        return -1;
    }
    
    // Clean up
    if (subscriptions[slot].event_buffer) {
        event_buffer_free(subscriptions[slot].event_buffer);
    }
    if (subscriptions[slot].resource_type) free(subscriptions[slot].resource_type);
    if (subscriptions[slot].namespace) free(subscriptions[slot].namespace);
    if (subscriptions[slot].label_selector) free(subscriptions[slot].label_selector);
    if (subscriptions[slot].field_selector) free(subscriptions[slot].field_selector);
    
    subscriptions[slot].active = 0;
    
    pthread_mutex_unlock(&subscriptions_lock);
    return 0;
}

// Send a resource change event to all matching watchers
int watch_notify_change(const char* resource_type, const char* namespace,
                       const char* name, event_type_t event_type, 
                       json_object* object) {
    if (!resource_type || !object) return -1;
    
    event_t* event = event_new(event_type, resource_type, namespace, name, object);
    if (!event) return -1;
    
    int result = event_dispatch(event);
    event_free(event);
    
    return result;
}

// Send BOOKMARK event to specific watch
int watch_send_bookmark(int subscription_id, uint64_t resource_version) {
    event_t* bookmark = event_bookmark_new(resource_version);
    if (!bookmark) return -1;
    
    pthread_mutex_lock(&subscriptions_lock);
    
    int slot = -1;
    for (int i = 0; i < MAX_WATCH_SUBSCRIPTIONS; i++) {
        if (subscriptions[i].active && subscriptions[i].id == subscription_id) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        pthread_mutex_unlock(&subscriptions_lock);
        event_free(bookmark);
        return -1;
    }
    
    event_buffer_add(subscriptions[slot].event_buffer, bookmark);
    pthread_mutex_unlock(&subscriptions_lock);
    
    return 0;
}

// Simple label selector matching (basic implementation)
// Supports: key=value (exact match), key!=value (not equal)
static int match_label(const char* selector, json_object* labels_obj) {
    if (!selector || !labels_obj) return 1;  // No selector = match all
    
    // Parse selector: "key=value,key2=value2"
    char* selector_copy = malloc(strlen(selector) + 1);
    strcpy(selector_copy, selector);
    
    char* saveptr = NULL;
    char* token = strtok_r(selector_copy, ",", &saveptr);
    
    int matches = 1;
    while (token && matches) {
        char* equals = strchr(token, '=');
        if (!equals) {
            token = strtok_r(NULL, ",", &saveptr);
            continue;
        }
        
        // Split on first =
        int key_len = equals - token;
        char key[256];
        strncpy(key, token, key_len);
        key[key_len] = '\0';
        
        char* value = equals + 1;
        if (*value == '=') {
            // !=  (not equal)
            value++;
            json_object* label_val = NULL;
            if (json_object_object_get_ex(labels_obj, key, &label_val)) {
                const char* label_str = json_object_get_string(label_val);
                if (label_str && strcmp(label_str, value) == 0) {
                    matches = 0;  // Found matching value, but we wanted !=
                }
            }
        } else {
            // = (equal)
            json_object* label_val = NULL;
            if (json_object_object_get_ex(labels_obj, key, &label_val)) {
                const char* label_str = json_object_get_string(label_val);
                if (!label_str || strcmp(label_str, value) != 0) {
                    matches = 0;
                }
            } else {
                matches = 0;  // Key not found
            }
        }
        
        token = strtok_r(NULL, ",", &saveptr);
    }
    
    free(selector_copy);
    return matches;
}

// Helper: Check if label selector matches resource labels
int watch_match_label_selector(json_object* object, const char* label_selector) {
    if (!label_selector) return 1;  // No selector = match all
    if (!object) return 0;
    
    json_object* metadata = NULL;
    if (!json_object_object_get_ex(object, "metadata", &metadata)) {
        return 0;
    }
    
    json_object* labels = NULL;
    if (!json_object_object_get_ex(metadata, "labels", &labels)) {
        return 0;  // No labels = doesn't match label selector
    }
    
    return match_label(label_selector, labels);
}

// Simple field selector matching
// Supports: metadata.name=value, status.phase=Running, metadata.namespace=default
static int match_field(const char* selector, json_object* object) {
    if (!selector || !object) return 1;  // No selector = match all
    
    // For simplicity, we'll just check common fields
    // Full implementation would need a proper field selector parser
    
    if (strstr(selector, "metadata.name=")) {
        const char* expected = strchr(selector, '=') + 1;
        json_object* metadata = NULL;
        if (json_object_object_get_ex(object, "metadata", &metadata)) {
            json_object* name = NULL;
            if (json_object_object_get_ex(metadata, "name", &name)) {
                const char* actual = json_object_get_string(name);
                if (actual && strcmp(actual, expected) == 0) {
                    return 1;
                }
            }
        }
        return 0;
    }
    
    return 1;  // Unknown field selector = match all
}

// Helper: Check if field selector matches resource fields
int watch_match_field_selector(json_object* object, const char* field_selector) {
    if (!field_selector) return 1;  // No selector = match all
    if (!object) return 0;
    
    return match_field(field_selector, object);
}
