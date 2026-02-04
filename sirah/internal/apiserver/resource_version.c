// internal/apiserver/resource_version.c
// Resource versioning for optimistic concurrency control
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <json-c/json.h>
#include <inttypes.h>
#include "resource_version.h"

static resource_version_tracker_t tracker = {
    .current_version = 1000,  // Start at 1000 to leave room for cluster startup
    .lock = PTHREAD_MUTEX_INITIALIZER
};

// Initialize resource versioning
int resource_version_init(void) {
    pthread_mutex_init(&tracker.lock, NULL);
    return 0;
}

void resource_version_shutdown(void) {
    pthread_mutex_destroy(&tracker.lock);
}

// Get next version number (monotonically increasing)
uint64_t resource_version_next(void) {
    pthread_mutex_lock(&tracker.lock);
    tracker.current_version++;
    uint64_t version = tracker.current_version;
    pthread_mutex_unlock(&tracker.lock);
    return version;
}

// Get current version without incrementing
uint64_t resource_version_current(void) {
    pthread_mutex_lock(&tracker.lock);
    uint64_t version = tracker.current_version;
    pthread_mutex_unlock(&tracker.lock);
    return version;
}

// Check if a resourceVersion is valid for Compare-And-Swap
// Returns 0 if valid (no conflict), 1 if conflict, -1 on error
int resource_version_check(uint64_t expected_version, uint64_t current_version) {
    if (expected_version != current_version) {
        return 1;  // Conflict - versions don't match
    }
    return 0;  // No conflict
}

// Format version as string
char* resource_version_to_string(uint64_t version) {
    char* str = malloc(32);
    if (!str) return NULL;
    snprintf(str, 32, "%" PRIu64, version);
    return str;
}

// Parse version from string
uint64_t resource_version_from_string(const char* version_str) {
    if (!version_str) return 0;
    char* endptr = NULL;
    uint64_t version = strtoull(version_str, &endptr, 10);
    if (endptr == version_str) return 0;  // Invalid format
    return version;
}

// Add resourceVersion to JSON object
int resource_version_add_to_json(json_object* obj, uint64_t version) {
    if (!obj) return -1;
    
    // Ensure metadata object exists
    json_object* metadata = NULL;
    if (!json_object_object_get_ex(obj, "metadata", &metadata)) {
        metadata = json_object_new_object();
        json_object_object_add(obj, "metadata", metadata);
    }
    
    char version_str[32];
    snprintf(version_str, sizeof(version_str), "%" PRIu64, version);
    
    json_object_object_add(metadata, "resourceVersion", 
                          json_object_new_string(version_str));
    return 0;
}

// Extract resourceVersion from JSON object
uint64_t resource_version_from_json(json_object* obj) {
    if (!obj) return 0;
    
    json_object* metadata = NULL;
    if (!json_object_object_get_ex(obj, "metadata", &metadata)) {
        return 0;
    }
    
    json_object* version_obj = NULL;
    if (!json_object_object_get_ex(metadata, "resourceVersion", &version_obj)) {
        return 0;
    }
    
    const char* version_str = json_object_get_string(version_obj);
    return resource_version_from_string(version_str);
}

// Update object's resourceVersion
int resource_version_update_object(json_object* obj) {
    if (!obj) return -1;
    
    uint64_t new_version = resource_version_next();
    return resource_version_add_to_json(obj, new_version);
}
