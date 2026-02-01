// internal/apiserver/query_parser.h
// Parse and handle Kubernetes list query parameters
#ifndef SIRAH_QUERY_PARSER_H
#define SIRAH_QUERY_PARSER_H

#include <json-c/json.h>

typedef struct {
    char label_selector[512];
    char field_selector[512];
    int limit;
    char continue_token[256];
    int timeout_seconds;
    int allow_watch_bookmarks;
} list_query_params_t;

// Parse query string from URL
// Returns 0 on success
int parse_list_query(const char* query_string, list_query_params_t* params);

// Check if pod matches label selector (simplified: key=value format)
int matches_label_selector(json_object* pod, const char* label_selector);

// Check if pod matches field selector (simplified: fieldPath=value format)
int matches_field_selector(json_object* pod, const char* field_selector);

// Apply limit and continue token to items
void apply_list_limits(json_object* items_array, int limit, const char* continue_token);

#endif // SIRAH_QUERY_PARSER_H
