// internal/apiserver/cas_handler.c
// Compare-And-Swap handler for optimistic concurrency control
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <json-c/json.h>
#include "cas_handler.h"
#include "resource_version.h"

// Object version tracker
typedef struct {
    char* key;  // resource_type/namespace/name
    uint64_t version;
    pthread_mutex_t lock;
} object_version_t;

#define MAX_TRACKED_OBJECTS 10000
static object_version_t objects[MAX_TRACKED_OBJECTS];
static pthread_mutex_t objects_lock = PTHREAD_MUTEX_INITIALIZER;
static int initialized = 0;

// Helper: Generate key for object
static void generate_key(char* key, int key_size, const char* resource_type,
                        const char* namespace, const char* name) {
    snprintf(key, key_size, "%s/%s/%s", resource_type, namespace, name);
}

// Helper: Find or create object version tracker
static object_version_t* get_object_version(const char* resource_type,
                                            const char* namespace, const char* name) {
    char key[256];
    generate_key(key, sizeof(key), resource_type, namespace, name);
    
    pthread_mutex_lock(&objects_lock);
    
    // Search for existing
    for (int i = 0; i < MAX_TRACKED_OBJECTS; i++) {
        if (objects[i].key && strcmp(objects[i].key, key) == 0) {
            pthread_mutex_unlock(&objects_lock);
            return &objects[i];
        }
    }
    
    // Create new if not found
    for (int i = 0; i < MAX_TRACKED_OBJECTS; i++) {
        if (!objects[i].key) {
            objects[i].key = malloc(strlen(key) + 1);
            strcpy(objects[i].key, key);
            objects[i].version = resource_version_next();
            pthread_mutex_init(&objects[i].lock, NULL);
            
            pthread_mutex_unlock(&objects_lock);
            return &objects[i];
        }
    }
    
    pthread_mutex_unlock(&objects_lock);
    return NULL;  // No free slots
}

// Initialize CAS handler
int cas_handler_init(void) {
    if (initialized) return 0;
    
    pthread_mutex_lock(&objects_lock);
    memset(objects, 0, sizeof(objects));
    initialized = 1;
    pthread_mutex_unlock(&objects_lock);
    
    resource_version_init();
    return 0;
}

// Check if update would conflict
cas_result_t cas_check_version(const char* resource_type, const char* namespace,
                              const char* name, uint64_t expected_version) {
    if (!resource_type || !namespace || !name) {
        return CAS_RESULT_ERROR;
    }
    
    cas_handler_init();  // Ensure initialized
    
    object_version_t* obj_ver = get_object_version(resource_type, namespace, name);
    if (!obj_ver) {
        return CAS_RESULT_ERROR;
    }
    
    pthread_mutex_lock(&obj_ver->lock);
    uint64_t current = obj_ver->version;
    pthread_mutex_unlock(&obj_ver->lock);
    
    if (expected_version != current) {
        return CAS_RESULT_CONFLICT;
    }
    
    return CAS_RESULT_OK;
}

// Get and check version
json_object* cas_get_and_check(const char* resource_type, const char* namespace,
                              const char* name, uint64_t expected_version) {
    cas_result_t result = cas_check_version(resource_type, namespace, name, expected_version);
    if (result != CAS_RESULT_OK) {
        return NULL;  // Conflict or error
    }
    
    // TODO: Retrieve actual object from storage
    // For now, return a minimal object with version
    json_object* obj = json_object_new_object();
    json_object* metadata = json_object_new_object();
    
    char version_str[32];
    snprintf(version_str, sizeof(version_str), "%" PRIu64, expected_version);
    json_object_object_add(metadata, "resourceVersion", json_object_new_string(version_str));
    json_object_object_add(obj, "metadata", metadata);
    
    return obj;
}

// Update object version after successful mutation
int cas_update_version(const char* resource_type, const char* namespace, const char* name) {
    object_version_t* obj_ver = get_object_version(resource_type, namespace, name);
    if (!obj_ver) {
        return -1;
    }
    
    pthread_mutex_lock(&obj_ver->lock);
    obj_ver->version = resource_version_next();
    pthread_mutex_unlock(&obj_ver->lock);
    
    return 0;
}

// Generate conflict response (409 Conflict)
char* cas_generate_conflict_response(const char* resource_type, const char* namespace,
                                     const char* name, json_object* current_object) {
    if (!current_object) {
        return strdup("{\"kind\":\"Status\",\"status\":\"Failure\",\"message\":\"Conflict\",\"code\":409}");
    }
    
    // Build conflict response with current object's resourceVersion
    json_object* response = json_object_new_object();
    json_object_object_add(response, "kind", json_object_new_string("Status"));
    json_object_object_add(response, "status", json_object_new_string("Failure"));
    json_object_object_add(response, "message", json_object_new_string(
        "The object has been modified; please apply your changes to the latest version and try again"));
    json_object_object_add(response, "code", json_object_new_int(409));
    
    // Include the current object
    const char* obj_str = json_object_to_json_string(current_object);
    json_object* obj_copy = json_tokener_parse(obj_str);
    json_object_object_add(response, "object", obj_copy);
    
    const char* response_str = json_object_to_json_string(response);
    char* result = strdup(response_str);
    json_object_put(response);
    
    return result;
}

// Retry with exponential backoff
int cas_retry_with_backoff(const char* resource_type, const char* namespace,
                          const char* name, cas_operation_t callback,
                          void* context, int max_retries, int initial_backoff_ms) {
    if (!callback) return -1;
    
    int backoff_ms = initial_backoff_ms;
    
    for (int attempt = 0; attempt < max_retries; attempt++) {
        json_object* current = cas_get_and_check(resource_type, namespace, name, 0);
        
        int result = callback(context, current, attempt);
        
        if (current) json_object_put(current);
        
        if (result == 0) {
            return 0;  // Success
        } else if (result < 0) {
            return -1;  // Fatal error
        }
        
        // Retry with exponential backoff
        if (attempt < max_retries - 1) {
            usleep(backoff_ms * 1000);  // Convert ms to microseconds
            backoff_ms *= 2;  // Exponential backoff
            if (backoff_ms > 32000) {
                backoff_ms = 32000;  // Cap at 32 seconds
            }
        }
    }
    
    return -1;  // Max retries exceeded
}

// Public function to update version after mutation
// Called by handlers after successful PUT/PATCH
int cas_mark_updated(const char* resource_type, const char* namespace, const char* name) {
    return cas_update_version(resource_type, namespace, name);
}

// Get current version for an object
uint64_t cas_get_version(const char* resource_type, const char* namespace, const char* name) {
    object_version_t* obj_ver = get_object_version(resource_type, namespace, name);
    if (!obj_ver) {
        return 0;
    }
    
    pthread_mutex_lock(&obj_ver->lock);
    uint64_t version = obj_ver->version;
    pthread_mutex_unlock(&obj_ver->lock);
    
    return version;
}
