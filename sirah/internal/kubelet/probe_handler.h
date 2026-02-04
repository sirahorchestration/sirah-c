// internal/kubelet/probe_handler.h
// Health probe execution for pods (startup, readiness, liveness)

#ifndef SIRAH_PROBE_HANDLER_H
#define SIRAH_PROBE_HANDLER_H

#include <time.h>

// Probe types
typedef enum {
    PROBE_STARTUP = 0,      // Check if container started successfully
    PROBE_READINESS = 1,    // Check if container is ready to serve traffic
    PROBE_LIVENESS = 2      // Check if container needs restart
} probe_type_t;

// Probe handler types
typedef enum {
    HANDLER_HTTP = 0,       // HTTP GET probe
    HANDLER_TCP = 1,        // TCP socket probe
    HANDLER_EXEC = 2,       // Exec (command) probe
    HANDLER_GRPC = 3        // gRPC probe
} handler_type_t;

// Probe result
typedef struct {
    int success;            // 1 = success, 0 = failure
    int exit_code;          // For exec probes
    char* message;          // Error message on failure
    time_t timestamp;       // When probe was executed
    int probe_count;        // How many times this probe has been executed
    int success_count;      // How many successful executions
    int failure_count;      // How many failed executions
} probe_result_t;

// HTTP probe configuration
typedef struct {
    char* path;             // URL path (e.g., /healthz)
    int port;               // Port number
    char* host;             // Host (optional, default localhost)
    int timeout_seconds;    // Timeout for probe
    int initial_delay_seconds;  // Delay before first probe
    int period_seconds;     // How often to probe
    int success_threshold;  // Consecutive successes required
    int failure_threshold;  // Consecutive failures before considered unhealthy
    // HTTP headers (optional)
    int num_headers;
    char** header_names;
    char** header_values;
} http_probe_t;

// TCP probe configuration
typedef struct {
    int port;               // Port to connect to
    char* host;             // Host (optional)
    int timeout_seconds;    // Timeout for probe
    int initial_delay_seconds;  // Delay before first probe
    int period_seconds;     // How often to probe
    int success_threshold;  // Consecutive successes required
    int failure_threshold;  // Consecutive failures before considered unhealthy
} tcp_probe_t;

// Exec probe configuration
typedef struct {
    int num_commands;       // Command argument count
    char** commands;        // Command and arguments (e.g., ["sh", "-c", "test -f /tmp/healthy"])
    int timeout_seconds;    // Timeout for probe
    int initial_delay_seconds;  // Delay before first probe
    int period_seconds;     // How often to probe
    int success_threshold;  // Consecutive successes required
    int failure_threshold;  // Consecutive failures before considered unhealthy
} exec_probe_t;

// gRPC probe configuration
typedef struct {
    int port;               // gRPC service port
    char* service;          // gRPC service name
    int timeout_seconds;    // Timeout for probe
    int initial_delay_seconds;  // Delay before first probe
    int period_seconds;     // How often to probe
    int success_threshold;  // Consecutive successes required
    int failure_threshold;  // Consecutive failures before considered unhealthy
} grpc_probe_t;

// Unified probe configuration
typedef struct {
    probe_type_t probe_type;        // Which probe (startup/readiness/liveness)
    handler_type_t handler_type;    // Which handler (HTTP/TCP/exec/gRPC)
    
    // Handler-specific config
    union {
        http_probe_t* http;
        tcp_probe_t* tcp;
        exec_probe_t* exec;
        grpc_probe_t* grpc;
    } handler;
    
    // Probe state
    int enabled;                    // Is this probe enabled?
    time_t last_probe_time;         // When last probe executed
    probe_result_t last_result;     // Result of last probe
} probe_spec_t;

// Probe execution
probe_result_t* probe_execute_http(const char* pod_namespace, const char* pod_name, 
                                    const char* container_name, http_probe_t* probe);
probe_result_t* probe_execute_tcp(const char* pod_namespace, const char* pod_name,
                                   const char* container_name, tcp_probe_t* probe);
probe_result_t* probe_execute_exec(const char* pod_namespace, const char* pod_name,
                                    const char* container_name, exec_probe_t* probe);
probe_result_t* probe_execute_grpc(const char* pod_namespace, const char* pod_name,
                                    const char* container_name, grpc_probe_t* probe);

// Generic probe execution
probe_result_t* probe_execute(const char* pod_namespace, const char* pod_name,
                              const char* container_name, probe_spec_t* probe);

// Probe result management
void probe_result_free(probe_result_t* result);

// Container health status based on probes
typedef enum {
    HEALTH_UNKNOWN = 0,
    HEALTH_STARTING = 1,    // Startup probe running
    HEALTH_READY = 2,       // Ready for traffic
    HEALTH_NOT_READY = 3,   // Not ready for traffic
    HEALTH_DEAD = 4         // Liveness probe failed
} container_health_t;

// Update container health based on probe results
container_health_t probe_update_container_health(probe_result_t* startup_result,
                                                  probe_result_t* readiness_result,
                                                  probe_result_t* liveness_result);

#endif // SIRAH_PROBE_HANDLER_H
