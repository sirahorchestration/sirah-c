// internal/apiserver/resource_version.h
// Resource versioning for optimistic concurrency control
#ifndef SIRAH_RESOURCE_VERSION_H
#define SIRAH_RESOURCE_VERSION_H

#include <json-c/json.h>
#include <stdint.h>
#include <time.h>

// Global resource version counter (etcd-style monotonic versioning)
typedef struct {
    uint64_t current_version;
    pthread_mutex_t lock;
} resource_version_tracker_t;

// Initialize resource versioning
int resource_version_init(void);
void resource_version_shutdown(void);

// Get next version number (monotonically increasing)
uint64_t resource_version_next(void);

// Get current version number without incrementing
uint64_t resource_version_current(void);

// Check if a resourceVersion is valid for Compare-And-Swap
// Returns 0 if valid, 1 if conflict, -1 on error
int resource_version_check(uint64_t expected_version, uint64_t current_version);

// Format version as string (for JSON)
// Returns allocated string that must be freed by caller
char* resource_version_to_string(uint64_t version);

// Parse version from string
// Returns version number, 0 if invalid
uint64_t resource_version_from_string(const char* version_str);

// Add resourceVersion to JSON object
int resource_version_add_to_json(json_object* obj, uint64_t version);

// Extract resourceVersion from JSON object
// Returns version number, 0 if not present
uint64_t resource_version_from_json(json_object* obj);

// Update object's resourceVersion
// Updates metadata.resourceVersion with new version
// Returns 0 on success, -1 on error
int resource_version_update_object(json_object* obj);

#endif // SIRAH_RESOURCE_VERSION_H
