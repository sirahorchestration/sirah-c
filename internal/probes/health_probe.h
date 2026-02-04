/*
 * health_probe.h
 * 
 * Health probe implementation for Kubernetes pod health checks
 * 
 * Supports three probe types:
 *   - Startup Probe: Wait for container to become ready (runs once at startup)
 *   - Readiness Probe: Check if pod is ready to serve traffic (periodic)
 *   - Liveness Probe: Check if container is still alive (periodic, restarts if fails)
 * 
 * Supports three execution methods:
 *   - HTTP GET: HTTP request to endpoint
 *   - TCP: TCP port connection
 *   - Exec: Custom command execution
 * 
 * Kubernetes v1.28 Conformance:
 *   - Startup probe blocks pod readiness until passing
 *   - Readiness probe determines if pod receives traffic
 *   - Liveness probe triggers container restart on failure
 *   - Configurable timeouts, initial delays, thresholds
 *   - Thread-safe probe execution
 */

#ifndef SIRAH_HEALTH_PROBE_H
#define SIRAH_HEALTH_PROBE_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

/**
 * Probe types
 */
typedef enum {
    PROBE_TYPE_STARTUP,     // Runs once, blocks readiness
    PROBE_TYPE_READINESS,   // Periodic, determines traffic
    PROBE_TYPE_LIVENESS     // Periodic, triggers restart
} probe_type_t;

/**
 * Probe execution methods
 */
typedef enum {
    PROBE_HANDLER_HTTP,     // HTTP GET request
    PROBE_HANDLER_TCP,      // TCP port connection
    PROBE_HANDLER_EXEC      // Command execution
} probe_handler_t;

/**
 * HTTP probe configuration
 */
typedef struct {
    char *path;             // HTTP path (e.g., "/healthz")
    uint16_t port;          // HTTP port
    char *host;             // Host IP (optional, defaults to pod IP)
    char *scheme;           // HTTP or HTTPS
} http_probe_config_t;

/**
 * TCP probe configuration
 */
typedef struct {
    uint16_t port;          // TCP port to check
    char *host;             // Host IP (optional, defaults to pod IP)
} tcp_probe_config_t;

/**
 * Exec probe configuration
 */
typedef struct {
    char **command;         // Command and arguments to execute
    uint32_t command_len;
} exec_probe_config_t;

/**
 * Probe definition
 */
typedef struct {
    probe_type_t type;
    probe_handler_t handler_type;
    
    union {
        http_probe_config_t http;
        tcp_probe_config_t tcp;
        exec_probe_config_t exec;
    } handler_config;
    
    uint32_t initial_delay_seconds;  // Delay before first execution
    uint32_t timeout_seconds;        // Timeout for single execution
    uint32_t period_seconds;         // Interval between executions
    uint32_t success_threshold;      // Consecutive successes to pass
    uint32_t failure_threshold;      // Consecutive failures to fail
} probe_config_t;

/**
 * Probe execution result
 */
typedef enum {
    PROBE_RESULT_SUCCESS,
    PROBE_RESULT_FAILURE,
    PROBE_RESULT_TIMEOUT,
    PROBE_RESULT_ERROR
} probe_result_t;

/**
 * Probe execution state
 */
typedef struct {
    probe_result_t last_result;
    uint32_t consecutive_successes;
    uint32_t consecutive_failures;
    time_t last_check_time;
    bool passed;                      // Has passed (for startup probe)
    char *last_error;                 // Error message if failed
} probe_state_t;

/**
 * Probe instance (per pod/container)
 */
typedef struct {
    char *pod_name;
    char *namespace;
    char *container_name;
    char *pod_ip;
    
    probe_config_t config;
    probe_state_t state;
    
    pthread_mutex_t mutex;
} probe_instance_t;

/**
 * Probe manager (tracks all probes for pod)
 */
typedef struct {
    char *pod_name;
    char *namespace;
    char *pod_ip;
    
    probe_instance_t *probes;
    uint32_t probe_count;
    
    // Probe results
    bool startup_passed;              // Has startup probe passed?
    bool ready;                       // Is pod ready? (readiness probe passed)
    bool alive;                       // Is pod alive? (liveness probe passed)
    
    pthread_mutex_t mutex;
} pod_probe_manager_t;

/**
 * Create pod probe manager
 * 
 * @param pod_name Pod name
 * @param namespace Namespace
 * @param pod_ip Pod IP address
 * @return Probe manager instance or NULL
 */
pod_probe_manager_t* pod_probe_manager_create(const char *pod_name,
                                               const char *namespace,
                                               const char *pod_ip);

/**
 * Free pod probe manager
 */
void pod_probe_manager_free(pod_probe_manager_t *manager);

/**
 * Add probe to pod
 * 
 * @param manager Probe manager
 * @param config Probe configuration
 * @return Probe instance or NULL
 */
probe_instance_t* pod_probe_manager_add_probe(pod_probe_manager_t *manager,
                                               const probe_config_t *config);

/**
 * Execute HTTP probe
 * 
 * Performs HTTP GET request to container endpoint
 * 
 * @param pod_ip Pod IP address
 * @param config HTTP probe configuration
 * @param timeout_seconds Request timeout
 * @return PROBE_RESULT_SUCCESS if 2xx response, else PROBE_RESULT_FAILURE
 */
probe_result_t execute_http_probe(const char *pod_ip,
                                   const http_probe_config_t *config,
                                   uint32_t timeout_seconds);

/**
 * Execute TCP probe
 * 
 * Attempts TCP connection to port
 * 
 * @param pod_ip Pod IP address
 * @param config TCP probe configuration
 * @param timeout_seconds Connection timeout
 * @return PROBE_RESULT_SUCCESS if connection successful, else PROBE_RESULT_FAILURE
 */
probe_result_t execute_tcp_probe(const char *pod_ip,
                                  const tcp_probe_config_t *config,
                                  uint32_t timeout_seconds);

/**
 * Execute Exec probe
 * 
 * Runs command inside container namespace
 * 
 * @param pod_name Pod name
 * @param namespace Namespace
 * @param config Exec probe configuration
 * @param timeout_seconds Command timeout
 * @return PROBE_RESULT_SUCCESS if exit code 0, else PROBE_RESULT_FAILURE
 */
probe_result_t execute_exec_probe(const char *pod_name,
                                   const char *namespace,
                                   const exec_probe_config_t *config,
                                   uint32_t timeout_seconds);

/**
 * Execute probe (dispatches to correct executor)
 * 
 * @param manager Probe manager
 * @param probe_config Probe configuration
 * @return Probe result
 */
probe_result_t pod_probe_execute(pod_probe_manager_t *manager,
                                  const probe_config_t *probe_config);

/**
 * Update probe result and check thresholds
 * 
 * Updates consecutive success/failure counts
 * Determines if probe has passed/failed
 * 
 * @param probe Probe instance
 * @param result Latest execution result
 * @return true if state changed (passed/failed)
 */
bool pod_probe_update_result(probe_instance_t *probe, probe_result_t result);

/**
 * Check if pod startup is complete
 * 
 * All startup probes must pass
 * 
 * @param manager Probe manager
 * @return true if startup complete
 */
bool pod_probe_is_startup_complete(pod_probe_manager_t *manager);

/**
 * Check if pod is ready
 * 
 * Readiness probes must pass
 * 
 * @param manager Probe manager
 * @return true if pod ready
 */
bool pod_probe_is_ready(pod_probe_manager_t *manager);

/**
 * Check if pod is alive
 * 
 * Liveness probes must pass (if not, container should restart)
 * 
 * @param manager Probe manager
 * @return true if pod alive
 */
bool pod_probe_is_alive(pod_probe_manager_t *manager);

/**
 * Get probe state for inspection/debugging
 * 
 * @param manager Probe manager
 * @param probe_type Type of probe
 * @param out_state Output probe state
 * @return true if probe found
 */
bool pod_probe_get_state(pod_probe_manager_t *manager,
                         probe_type_t probe_type,
                         probe_state_t *out_state);

/**
 * Get probe manager statistics
 * 
 * @param manager Probe manager
 * @param out_total Total number of probes
 * @param out_passing Number of passing probes
 * @param out_failing Number of failing probes
 */
void pod_probe_get_stats(pod_probe_manager_t *manager,
                         uint32_t *out_total,
                         uint32_t *out_passing,
                         uint32_t *out_failing);

/**
 * Free probe config (for exec probes with allocated command)
 */
void pod_probe_free_config(probe_config_t *config);

#endif // SIRAH_HEALTH_PROBE_H
