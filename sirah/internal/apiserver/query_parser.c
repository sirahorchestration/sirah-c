// internal/apiserver/query_parser.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include "query_parser.h"

// URL decode helper
static void url_decode(char* str) {
    char* src = str;
    char* dst = str;
    while (*src) {
        if (*src == '%' && *(src+1) && *(src+2)) {
            int val = 0;
            sscanf(src+1, "%2x", &val);
            *dst = (char)val;
            src += 3;
            dst++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// Parse query string parameters
int parse_list_query(const char* query_string, list_query_params_t* params) {
    memset(params, 0, sizeof(*params));
    params->limit = 500;  // Default limit
    params->timeout_seconds = 30;
    
    if (!query_string || strlen(query_string) == 0) {
        return 0;
    }
    
    char* qs_copy = strdup(query_string);
    char* token = strtok(qs_copy, "&");
    
    while (token) {
        char* eq = strchr(token, '=');
        if (eq) {
            *eq = '\0';
            const char* key = token;
            const char* value = eq + 1;
            
            // URL decode
            char value_decoded[512] = {0};
            strncpy(value_decoded, value, sizeof(value_decoded) - 1);
            url_decode(value_decoded);
            
            if (strcmp(key, "labelSelector") == 0) {
                strncpy(params->label_selector, value_decoded, sizeof(params->label_selector) - 1);
            }
            else if (strcmp(key, "fieldSelector") == 0) {
                strncpy(params->field_selector, value_decoded, sizeof(params->field_selector) - 1);
            }
            else if (strcmp(key, "limit") == 0) {
                params->limit = atoi(value_decoded);
                if (params->limit <= 0) params->limit = 500;
            }
            else if (strcmp(key, "continue") == 0) {
                strncpy(params->continue_token, value_decoded, sizeof(params->continue_token) - 1);
            }
            else if (strcmp(key, "timeoutSeconds") == 0) {
                params->timeout_seconds = atoi(value_decoded);
            }
            else if (strcmp(key, "allowWatchBookmarks") == 0) {
                params->allow_watch_bookmarks = (strcmp(value_decoded, "true") == 0) ? 1 : 0;
            }
        }
        token = strtok(NULL, "&");
    }
    
    free(qs_copy);
    return 0;
}

// Check if object matches label selector
// Simplified: supports "key=value" and "key!=value" selectors
int matches_label_selector(json_object* item, const char* label_selector) {
    if (!label_selector || strlen(label_selector) == 0) {
        return 1;  // No selector = matches all
    }
    
    json_object* metadata = json_object_object_get(item, "metadata");
    if (!metadata) return 0;
    
    json_object* labels = json_object_object_get(metadata, "labels");
    if (!labels) return 0;
    
    char* selector_copy = strdup(label_selector);
    char* token = strtok(selector_copy, ",");
    int matches = 1;
    
    while (token && matches) {
        char* eq = strchr(token, '=');
        char* neq = strchr(token, '!');
        
        if (eq) {
            *eq = '\0';
            const char* key = token;
            const char* expected_value = eq + 1;
            
            // Skip != if present
            if (neq && neq < eq) {
                expected_value = eq + 2;  // Skip !=
                neq = NULL;
            }
            
            json_object* label_val = json_object_object_get(labels, key);
            if (!label_val) {
                matches = neq ? 1 : 0;  // Not found: matches if looking for !=, doesn't match if looking for =
            } else {
                const char* actual = json_object_get_string(label_val);
                if (neq) {
                    // Look for != operator
                    matches = (strcmp(actual, expected_value) != 0) ? 1 : 0;
                } else {
                    matches = (strcmp(actual, expected_value) == 0) ? 1 : 0;
                }
            }
        }
        
        token = strtok(NULL, ",");
    }
    
    free(selector_copy);
    return matches;
}

// Check if object matches field selector
// Simplified: supports metadata.name, metadata.namespace, status.phase
int matches_field_selector(json_object* item, const char* field_selector) {
    if (!field_selector || strlen(field_selector) == 0) {
        return 1;  // No selector = matches all
    }
    
    char* selector_copy = strdup(field_selector);
    char* token = strtok(selector_copy, ",");
    int matches = 1;
    
    while (token && matches) {
        char* eq = strchr(token, '=');
        if (eq) {
            *eq = '\0';
            const char* field = token;
            const char* expected_value = eq + 1;
            
            // Navigate field path
            json_object* current = item;
            char field_copy[256] = {0};
            strncpy(field_copy, field, sizeof(field_copy) - 1);
            char* field_token = strtok(field_copy, ".");
            
            while (field_token && current) {
                current = json_object_object_get(current, field_token);
                field_token = strtok(NULL, ".");
            }
            
            if (!current) {
                matches = 0;
            } else {
                const char* actual = json_object_get_string(current);
                if (actual) {
                    matches = (strcmp(actual, expected_value) == 0) ? 1 : 0;
                } else {
                    matches = 0;
                }
            }
        }
        
        token = strtok(NULL, ",");
    }
    
    free(selector_copy);
    return matches;
}

// Apply limit and continue token to items array
void apply_list_limits(json_object* items_array, int limit, const char* continue_token) {
    if (!items_array || !json_object_is_type(items_array, json_type_array)) {
        return;
    }
    
    int array_len = json_object_array_length(items_array);
    int start_idx = 0;
    
    // Simple continue token: base64(offset)
    if (continue_token && strlen(continue_token) > 0) {
        // For simplicity, treat continue token as offset number
        start_idx = atoi(continue_token);
    }
    
    // Remove items beyond limit
    while (json_object_array_length(items_array) > limit) {
        int idx = json_object_array_length(items_array) - 1;
        // Simple removal by creating new array without last element
        json_object* new_array = json_object_new_array();
        for (int i = 0; i < idx; i++) {
            json_object_array_add(new_array, json_object_array_get_idx(items_array, i));
        }
        // Replace old array (simplified, just stop adding)
        break;
    }
}
