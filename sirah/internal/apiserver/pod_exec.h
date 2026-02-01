// internal/apiserver/pod_exec.h
// Pod command execution implementation
#ifndef SIRAH_POD_EXEC_H
#define SIRAH_POD_EXEC_H

#include <json-c/json.h>

typedef struct {
    char pod_name[256];
    char namespace[256];
    char container_name[256];
    char command[1024];          // Command to execute
    int stdin_enabled;            // Accept stdin
    int stdout_enabled;           // Return stdout
    int stderr_enabled;           // Return stderr
    int tty_enabled;              // TTY mode
} exec_request_t;

typedef struct {
    int exit_code;
    char stdout_buffer[16384];
    char stderr_buffer[4096];
    int stdout_len;
    int stderr_len;
} exec_response_t;

// Parse exec request from JSON
int parse_exec_request(const char* request_json, exec_request_t* req);

// Execute command in pod context
int endpoint_exec_pod(const char* namespace, const char* pod_name,
                      const char* container_name, const char* command,
                      exec_response_t* response);

// Build exec response JSON
int build_exec_response(const exec_response_t* response, char* response_buffer);

#endif // SIRAH_POD_EXEC_H
