// internal/apiserver/patch_handler.h
// Implements Strategic Merge Patch and JSON Patch operations
#ifndef SIRAH_PATCH_HANDLER_H
#define SIRAH_PATCH_HANDLER_H

#include <json-c/json.h>

// Patch operation types
typedef enum {
    PATCH_TYPE_STRATEGIC_MERGE,  // RFC 6902 with strategic merge semantics
    PATCH_TYPE_JSON_PATCH        // RFC 6902 JSON Patch
} patch_type_t;

// Apply a JSON patch to a document
// Returns 0 on success, -1 on error
int apply_json_patch(json_object* target, json_object* patch_ops, 
                     char* error_buffer, int error_buffer_size);

// Apply a strategic merge patch to a document
// Returns 0 on success, -1 on error
int apply_strategic_merge_patch(json_object* target, json_object* patch,
                                char* error_buffer, int error_buffer_size);

// Determine patch type from Content-Type header
patch_type_t detect_patch_type(const char* content_type);

#endif // SIRAH_PATCH_HANDLER_H
