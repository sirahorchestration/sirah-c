// internal/apiserver/pod_logs.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <json-c/json.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "pod_logs.h"

#define LOG_STORE_DIR "/tmp/sirah-logs/pods"

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

    // Ensure log store directory exists
    mkdir(LOG_STORE_DIR, 0755);
    
    // Build log file path: /tmp/sirah-logs/pods/{namespace}/{pod_name}/{container_name}.log
    char dir_path[1024];
    char file_path[1024];
    
    snprintf(dir_path, sizeof(dir_path), "%s/%s/%s", LOG_STORE_DIR, namespace, pod_name);
    snprintf(file_path, sizeof(file_path), "%s/%s.log", dir_path, container_name);
    
    // Create directories if needed
    mkdir(LOG_STORE_DIR, 0755);
    mkdir(dir_path, 0755);
    
    // Append log line to file
    FILE* fp = fopen(file_path, "a");
    if (!fp) {
        fprintf(stderr, "[POD_LOG_WRITE] ERROR: Cannot open %s\n", file_path);
        return -1;
    }
    
    fprintf(fp, "%s\n", log_line);
    fclose(fp);
    
    return 0;
}

// Clear logs
int pod_log_clear(const char* namespace, const char* pod_name) {
    // Delete log files for this pod
    char dir_path[1024];
    snprintf(dir_path, sizeof(dir_path), "%s/%s/%s", LOG_STORE_DIR, namespace, pod_name);
    
    // For now, just succeed (would need recursive delete in production)
    return 0;
}

// Get pod logs
int endpoint_get_pod_logs(const char* namespace, const char* pod_name,
                          const char* container_name, log_query_params_t* params,
                          char* response_buffer, int* response_code) {
    // Build log file path
    char file_path[1024];
    snprintf(file_path, sizeof(file_path), "%s/%s/%s/%s.log", 
             LOG_STORE_DIR, namespace, pod_name, container_name);
    
    // Try to read log file
    FILE* fp = fopen(file_path, "r");
    if (!fp) {
        // No logs yet - return empty string (plain text response)
        strcpy(response_buffer, "");
        *response_code = 200;
        return 0;
    }
    
    // Read entire file into response buffer
    int offset = 0;
    char line[4096];
    int line_num = 0;
    int total_lines = 0;
    
    // Count total lines first (for tail functionality)
    while (fgets(line, sizeof(line), fp) != NULL) {
        total_lines++;
    }
    
    // Reset to beginning
    rewind(fp);
    
    // Determine start line for tail
    int start_line = 0;
    if (params->tail_lines > 0 && total_lines > params->tail_lines) {
        start_line = total_lines - params->tail_lines;
    }
    
    // Read lines and add to buffer
    line_num = 0;
    while (fgets(line, sizeof(line), fp) != NULL && offset < 16384) {
        if (line_num >= start_line) {
            // Check size limit
            if (params->limit_bytes > 0 && offset + strlen(line) > params->limit_bytes) {
                break;
            }
            
            // Add timestamp if requested
            if (params->timestamps) {
                time_t now = time(NULL);
                struct tm* tm_info = localtime(&now);
                char timestamp[64] = {0};
                strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ ", tm_info);
                offset += snprintf(response_buffer + offset, 16384 - offset, "%s%s", 
                                  timestamp, line);
            } else {
                offset += snprintf(response_buffer + offset, 16384 - offset, "%s", line);
            }
        }
        line_num++;
    }
    
    fclose(fp);
    
    // Ensure null-termination
    if (offset < 16384) {
        response_buffer[offset] = '\0';
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
