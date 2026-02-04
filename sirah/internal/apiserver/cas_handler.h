// internal/apiserver/cas_handler.h
// Compare-And-Swap handler for optimistic concurrency control
#ifndef SIRAH_CAS_HANDLER_H
#define SIRAH_CAS_HANDLER_H

#include <json-c/json.h>

typedef enum {
    CAS_RESULT_OK = 0,
    CAS_RESULT_CONFLICT = 1,
    CAS_RESULT_NOT_FOUND = 2,
    CAS_RESULT_ERROR = -1
} cas_result_t;

// Initialize CAS handler
int cas_handler_init(void);

// Check if update would conflict with current version
// resource_type: "pods", "services", "deployments"
// namespace: pod namespace
// name: pod name
// expected_version: version from client (from metadata.resourceVersion)
// Returns CAS_RESULT_OK if update is safe, CAS_RESULT_CONFLICT if version mismatch
cas_result_t cas_check_version(const char* resource_type, const char* namespace, 
                              const char* name, uint64_t expected_version);

// Apply CAS check and get current object
// Returns current object if version matches, NULL if conflict
// Caller must free returned JSON object
json_object* cas_get_and_check(const char* resource_type, const char* namespace,
                              const char* name, uint64_t expected_version);

// Generate HTTP response for conflict
// Returns allocated response string that must be freed
char* cas_generate_conflict_response(const char* resource_type, const char* namespace,
                                     const char* name, json_object* current_object);

// Automatically retry a CAS operation with exponential backoff
// Calls callback function repeatedly until success or max retries
// callback should return 0 on success, 1 to retry, -1 on fatal error
typedef int (*cas_operation_t)(void* context, json_object* current_object, int attempt);

int cas_retry_with_backoff(const char* resource_type, const char* namespace,
                          const char* name, cas_operation_t callback, 
                          void* context, int max_retries, int initial_backoff_ms);

#endif // SIRAH_CAS_HANDLER_H
