#ifndef K8S_BATCH_HANDLER_H
#define K8S_BATCH_HANDLER_H

#include <stdbool.h>

// Batch operation request
typedef struct {
    char* operation;     // "create", "update", "delete", "patch"
    char* resource_type; // "pod", "service", etc.
    char* namespace;
    char* name;
    char* data;          // JSON payload
} k8s_batch_request_t;

// Batch response
typedef struct {
    char* resource_id;
    int status_code;
    char* message;
    bool success;
} k8s_batch_response_t;

// Batch handler configuration
typedef struct {
    int max_batch_size;       // Max requests per batch
    int batch_timeout_ms;     // Time to wait for more requests
    int max_concurrent;       // Max concurrent batch operations
    bool enabled;
} k8s_batch_config_t;

// Batch handler
typedef struct {
    k8s_batch_request_t** pending_requests;
    int num_pending;
    k8s_batch_config_t* config;
    bool processing;
} k8s_batch_handler_t;

// ============ Configuration ============

k8s_batch_config_t* k8s_batch_config_new(int max_batch_size, int batch_timeout_ms, int max_concurrent);
void k8s_batch_config_free(k8s_batch_config_t* config);

// ============ Handler Management ============

k8s_batch_handler_t* k8s_batch_handler_new(k8s_batch_config_t* config);
void k8s_batch_handler_free(k8s_batch_handler_t* handler);

void k8s_batch_handler_set_enabled(k8s_batch_handler_t* handler, bool enabled);
bool k8s_batch_handler_is_enabled(k8s_batch_handler_t* handler);

// ============ Batch Operations ============

// Add request to batch
int k8s_batch_handler_add_request(k8s_batch_handler_t* handler,
                                   const char* operation,
                                   const char* resource_type,
                                   const char* namespace,
                                   const char* name,
                                   const char* data);

// Process pending batch
int k8s_batch_handler_process(k8s_batch_handler_t* handler,
                               k8s_batch_response_t*** responses,
                               int* num_responses);

// Get pending request count
int k8s_batch_handler_get_pending(k8s_batch_handler_t* handler);

// Check if batch is ready (full or timeout)
bool k8s_batch_handler_is_ready(k8s_batch_handler_t* handler);

// ============ Global Handler ============

k8s_batch_handler_t* k8s_batch_handler_global();

#endif
