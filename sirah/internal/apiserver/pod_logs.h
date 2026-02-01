// internal/apiserver/pod_logs.h
// Pod log streaming implementation
#ifndef SIRAH_POD_LOGS_H
#define SIRAH_POD_LOGS_H

#include <json-c/json.h>

typedef struct {
    char pod_name[256];
    char namespace[256];
    char container_name[256];
    char* log_content;
    int log_size;
    time_t timestamp;
} pod_log_t;

typedef struct {
    int follow;           // Stream logs continuously
    int previous;         // Show previous logs (after restart)
    int timestamps;       // Include timestamps
    int tail_lines;       // Number of lines to tail (default 10)
    int limit_bytes;      // Limit log size in bytes
} log_query_params_t;

// Get pod logs
// Returns log content or error
int endpoint_get_pod_logs(const char* namespace, const char* pod_name,
                          const char* container_name, log_query_params_t* params,
                          char* response_buffer, int* response_code);

// Stream pod logs (for ?follow=true)
// Returns initial logs, client should poll for updates
int endpoint_stream_pod_logs(const char* namespace, const char* pod_name,
                             const char* container_name, log_query_params_t* params,
                             char* response_buffer, int* response_code);

// Add log entry for a pod
// Called by kubelet/pod controller when logs are generated
int pod_log_write(const char* namespace, const char* pod_name,
                  const char* container_name, const char* log_line);

// Clear logs for a pod (on deletion)
int pod_log_clear(const char* namespace, const char* pod_name);

// Parse log query parameters
int parse_log_params(const char* query_string, log_query_params_t* params);

#endif // SIRAH_POD_LOGS_H
