// internal/apiserver/pod_logs.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <json-c/json.h>
#include "pod_logs.h"

#define MAX_PODS 1000
#define MAX_LOG_SIZE 1000000  // 1MB per pod
#define MAX_LOG_LINES 10000

typedef struct {
    char pod_name[256];
    char namespace[256];
    char container_name[256];
    char* logs[MAX_LOG_LINES];
    int log_count;
    time_t log_timestamps[MAX_LOG_LINES];
    int total_size;
} pod_log_store_t;

static pod_log_store_t log_store[MAX_PODS];
static int log_store_count = 0;

// Find or create pod log entry
static pod_log_store_t* find_or_create_pod_log(const char* namespace, const char* pod_name,
                                                const char* container_name) {
    // Find existing
    for (int i = 0; i < log_store_count; i++) {
        if (strcmp(log_store[i].pod_name, pod_name) == 0 &&
            strcmp(log_store[i].namespace, namespace) == 0 &&
            strcmp(log_store[i].container_name, container_name) == 0) {
            return &log_store[i];
        }
    }

    // Create new
    if (log_store_count < MAX_PODS) {
        pod_log_store_t* entry = &log_store[log_store_count++];
        strcpy(entry->pod_name, pod_name);
        strcpy(entry->namespace, namespace);
        strcpy(entry->container_name, container_name);
        entry->log_count = 0;
        entry->total_size = 0;
        return entry;
    }

    return NULL;
}

// Parse log query parameters
int parse_log_params(const char* query_string, log_query_params_t* params) {
    memset(params, 0, sizeof(*params));
    params->tail_lines = 10;  // Default

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

            if (strcmp(key, "follow") == 0) {
                params->follow = (strcmp(value, "true") == 0) ? 1 : 0;
            }
            else if (strcmp(key, "previous") == 0) {
                params->previous = (strcmp(value, "true") == 0) ? 1 : 0;
            }
            else if (strcmp(key, "timestamps") == 0) {
                params->timestamps = (strcmp(value, "true") == 0) ? 1 : 0;
            }
            else if (strcmp(key, "tailLines") == 0) {
                params->tail_lines = atoi(value);
                if (params->tail_lines <= 0) params->tail_lines = 10;
                if (params->tail_lines > 1000) params->tail_lines = 1000;
            }
            else if (strcmp(key, "limitBytes") == 0) {
                params->limit_bytes = atoi(value);
            }
        }
        token = strtok(NULL, "&");
    }

    free(qs_copy);
    return 0;
}

// Write log entry
int pod_log_write(const char* namespace, const char* pod_name,
                  const char* container_name, const char* log_line) {
    if (!log_line || strlen(log_line) == 0) {
        return 0;
    }

    pod_log_store_t* entry = find_or_create_pod_log(namespace, pod_name, container_name);
    if (!entry) {
        return -1;  // Storage full
    }

    // Check size limit
    int new_size = strlen(log_line) + 1;
    if (entry->total_size + new_size > MAX_LOG_SIZE) {
        return -1;  // Would exceed size limit
    }

    // Check line count limit
    if (entry->log_count >= MAX_LOG_LINES) {
        return -1;  // Too many lines
    }

    // Store log line
    entry->logs[entry->log_count] = strdup(log_line);
    entry->log_timestamps[entry->log_count] = time(NULL);
    entry->total_size += new_size;
    entry->log_count++;

    return 0;
}

// Clear logs
int pod_log_clear(const char* namespace, const char* pod_name) {
    for (int i = 0; i < log_store_count; i++) {
        if (strcmp(log_store[i].pod_name, pod_name) == 0 &&
            strcmp(log_store[i].namespace, namespace) == 0) {
            
            // Free logs
            for (int j = 0; j < log_store[i].log_count; j++) {
                free(log_store[i].logs[j]);
            }
            log_store[i].log_count = 0;
            log_store[i].total_size = 0;
            return 0;
        }
    }
    return 0;
}

// Get pod logs
int endpoint_get_pod_logs(const char* namespace, const char* pod_name,
                          const char* container_name, log_query_params_t* params,
                          char* response_buffer, int* response_code) {
    pod_log_store_t* entry = find_or_create_pod_log(namespace, pod_name, container_name);
    if (!entry) {
        *response_code = 500;
        strcpy(response_buffer, "{\"error\":\"log storage full\"}");
        return -1;
    }

    // Build response
    int offset = 0;
    int start_idx = 0;

    // Determine starting index for tail
    if (params->tail_lines > 0 && entry->log_count > params->tail_lines) {
        start_idx = entry->log_count - params->tail_lines;
    }

    // Stream logs as plain text
    for (int i = start_idx; i < entry->log_count; i++) {
        // Check size limit
        if (params->limit_bytes > 0 && offset + strlen(entry->logs[i]) > params->limit_bytes) {
            break;
        }

        // Add timestamp if requested
        if (params->timestamps) {
            char timestamp[64] = {0};
            struct tm* tm_info = localtime(&entry->log_timestamps[i]);
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ ", tm_info);
            offset += snprintf(response_buffer + offset, 16384 - offset, "%s%s\n", 
                              timestamp, entry->logs[i]);
        } else {
            offset += snprintf(response_buffer + offset, 16384 - offset, "%s\n", 
                              entry->logs[i]);
        }
    }

    *response_code = 200;
    return 0;
}

// Stream pod logs (for follow mode)
int endpoint_stream_pod_logs(const char* namespace, const char* pod_name,
                             const char* container_name, log_query_params_t* params,
                             char* response_buffer, int* response_code) {
    // Similar to get_pod_logs but in follow mode would stream continuously
    // For MVP, return same as get_pod_logs
    // In production, this would use WebSocket or chunked transfer encoding
    return endpoint_get_pod_logs(namespace, pod_name, container_name, params, 
                                 response_buffer, response_code);
}
