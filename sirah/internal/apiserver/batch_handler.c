#include "batch_handler.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Global batch handler
static k8s_batch_handler_t* g_batch_handler = NULL;

// ============ Configuration ============

k8s_batch_config_t* k8s_batch_config_new(int max_batch_size, int batch_timeout_ms, int max_concurrent) {
    if (max_batch_size <= 0 || batch_timeout_ms <= 0 || max_concurrent <= 0) return NULL;
    
    k8s_batch_config_t* config = calloc(1, sizeof(k8s_batch_config_t));
    if (!config) return NULL;
    
    config->max_batch_size = max_batch_size;
    config->batch_timeout_ms = batch_timeout_ms;
    config->max_concurrent = max_concurrent;
    config->enabled = true;
    
    return config;
}

void k8s_batch_config_free(k8s_batch_config_t* config) {
    if (!config) return;
    free(config);
}

// ============ Request Management ============

static k8s_batch_request_t* k8s_batch_request_new(const char* operation,
                                                   const char* resource_type,
                                                   const char* namespace,
                                                   const char* name,
                                                   const char* data) {
    if (!operation || !resource_type || !namespace || !name) return NULL;
    
    k8s_batch_request_t* req = calloc(1, sizeof(k8s_batch_request_t));
    if (!req) return NULL;
    
    req->operation = malloc(strlen(operation) + 1);
    req->resource_type = malloc(strlen(resource_type) + 1);
    req->namespace = malloc(strlen(namespace) + 1);
    req->name = malloc(strlen(name) + 1);
    req->data = data ? malloc(strlen(data) + 1) : NULL;
    
    if (!req->operation || !req->resource_type || !req->namespace || !req->name) {
        free(req->operation);
        free(req->resource_type);
        free(req->namespace);
        free(req->name);
        free(req->data);
        free(req);
        return NULL;
    }
    
    strcpy(req->operation, operation);
    strcpy(req->resource_type, resource_type);
    strcpy(req->namespace, namespace);
    strcpy(req->name, name);
    if (data) strcpy(req->data, data);
    
    return req;
}

static void k8s_batch_request_free(k8s_batch_request_t* req) {
    if (!req) return;
    
    free(req->operation);
    free(req->resource_type);
    free(req->namespace);
    free(req->name);
    free(req->data);
    free(req);
}

// ============ Response Management ============

static k8s_batch_response_t* k8s_batch_response_new(const char* resource_id,
                                                     int status_code,
                                                     const char* message,
                                                     bool success) {
    if (!resource_id) return NULL;
    
    k8s_batch_response_t* resp = calloc(1, sizeof(k8s_batch_response_t));
    if (!resp) return NULL;
    
    resp->resource_id = malloc(strlen(resource_id) + 1);
    resp->message = message ? malloc(strlen(message) + 1) : NULL;
    
    if (!resp->resource_id) {
        free(resp->message);
        free(resp);
        return NULL;
    }
    
    strcpy(resp->resource_id, resource_id);
    if (message) strcpy(resp->message, message);
    resp->status_code = status_code;
    resp->success = success;
    
    return resp;
}

static void k8s_batch_response_free(k8s_batch_response_t* resp) {
    if (!resp) return;
    
    free(resp->resource_id);
    free(resp->message);
    free(resp);
}

// ============ Handler Management ============

k8s_batch_handler_t* k8s_batch_handler_new(k8s_batch_config_t* config) {
    if (!config) return NULL;
    
    k8s_batch_handler_t* handler = calloc(1, sizeof(k8s_batch_handler_t));
    if (!handler) return NULL;
    
    handler->config = config;
    handler->num_pending = 0;
    handler->processing = false;
    
    return handler;
}

void k8s_batch_handler_free(k8s_batch_handler_t* handler) {
    if (!handler) return;
    
    for (int i = 0; i < handler->num_pending; i++) {
        if (handler->pending_requests[i]) {
            k8s_batch_request_free(handler->pending_requests[i]);
        }
    }
    free(handler->pending_requests);
    
    if (handler->config) {
        k8s_batch_config_free(handler->config);
    }
    
    free(handler);
}

void k8s_batch_handler_set_enabled(k8s_batch_handler_t* handler, bool enabled) {
    if (handler && handler->config) {
        handler->config->enabled = enabled;
    }
}

bool k8s_batch_handler_is_enabled(k8s_batch_handler_t* handler) {
    if (!handler || !handler->config) return false;
    return handler->config->enabled;
}

// ============ Batch Operations ============

int k8s_batch_handler_add_request(k8s_batch_handler_t* handler,
                                   const char* operation,
                                   const char* resource_type,
                                   const char* namespace,
                                   const char* name,
                                   const char* data) {
    if (!handler || !operation || !resource_type || !namespace || !name) return -1;
    if (!handler->config->enabled) return -1;
    
    k8s_batch_request_t* req = k8s_batch_request_new(operation, resource_type, namespace, name, data);
    if (!req) return -1;
    
    // Add to pending requests
    k8s_batch_request_t** new_requests = realloc(handler->pending_requests,
                                                  (handler->num_pending + 1) * sizeof(k8s_batch_request_t*));
    if (!new_requests) {
        k8s_batch_request_free(req);
        return -1;
    }
    
    new_requests[handler->num_pending] = req;
    handler->pending_requests = new_requests;
    handler->num_pending++;
    
    return 0;
}

int k8s_batch_handler_process(k8s_batch_handler_t* handler,
                               k8s_batch_response_t*** responses,
                               int* num_responses) {
    if (!handler || !responses || !num_responses) return -1;
    if (handler->num_pending == 0) return 0;
    
    // Process all pending requests
    k8s_batch_response_t** resp_array = calloc(handler->num_pending, sizeof(k8s_batch_response_t*));
    if (!resp_array) return -1;
    
    for (int i = 0; i < handler->num_pending; i++) {
        k8s_batch_request_t* req = handler->pending_requests[i];
        if (!req) continue;
        
        // TODO: Perform actual operation
        // For now, simulate successful operation
        char resource_id[256];
        snprintf(resource_id, sizeof(resource_id), "%s/%s/%s", req->namespace, req->resource_type, req->name);
        
        resp_array[i] = k8s_batch_response_new(resource_id, 200, "OK", true);
        
        k8s_batch_request_free(req);
    }
    
    // Clear pending requests
    free(handler->pending_requests);
    handler->pending_requests = NULL;
    handler->num_pending = 0;
    
    *responses = resp_array;
    *num_responses = handler->num_pending;
    
    return 0;
}

int k8s_batch_handler_get_pending(k8s_batch_handler_t* handler) {
    if (!handler) return 0;
    return handler->num_pending;
}

bool k8s_batch_handler_is_ready(k8s_batch_handler_t* handler) {
    if (!handler) return false;
    
    // Ready if batch is full
    if (handler->num_pending >= handler->config->max_batch_size) {
        return true;
    }
    
    // TODO: Check timeout using last_request_time
    
    return false;
}

// ============ Global Handler ============

k8s_batch_handler_t* k8s_batch_handler_global() {
    if (!g_batch_handler) {
        // 100 max per batch, 100ms timeout, 10 concurrent
        k8s_batch_config_t* config = k8s_batch_config_new(100, 100, 10);
        if (!config) return NULL;
        
        g_batch_handler = k8s_batch_handler_new(config);
    }
    return g_batch_handler;
}
