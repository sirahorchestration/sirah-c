// internal/kubelet/health_probe.h
// Health probe execution engine for kubelet
// Implements startup, readiness, and liveness probes per Kubernetes semantics

#ifndef SIRAH_HEALTH_PROBE_H
#define SIRAH_HEALTH_PROBE_H

#include "../../pkg/types/pod.h"
#include <time.h>

// Probe types
typedef enum {
    PROBE_STARTUP = 0,
    PROBE_READINESS = 1,
    PROBE_LIVENESS = 2
} probe_type_t;

// Probe execution result
typedef enum {
    PROBE_SUCCESS = 0,
    PROBE_FAILURE = 1,
    PROBE_UNKNOWN = 2
} probe_result_t;

// Probe handler function type
typedef probe_result_t (*probe_handler_t)(const char* pod_name, const char* namespace,
                                          const char* container_name, void* config);

// Probe configuration (mirrors Kubernetes Probe object)
typedef struct {
    // Handler choice
    int exec_enabled;
    int http_enabled;
    int tcp_enabled;
    
    // Exec handler
    char** exec_command;
    int exec_command_count;
    
    // HTTP handler
    char* http_host;
    int http_port;
    char* http_path;
    char* http_scheme;
    
    // TCP handler
    char* tcp_host;
    int tcp_port;
    
    // Timing
    int initial_delay_seconds;      // How long to wait before first probe
    int timeout_seconds;            // How long to wait for probe response
    int period_seconds;             // How often to probe
    int success_threshold;          // How many times probe must succeed to be considered successful
    int failure_threshold;          // How many times probe must fail before action
} probe_config_t;

// Probe execution state
typedef struct {
    probe_type_t type;
    probe_config_t config;
    
    // Timing
    time_t first_execution_time;
    time_t last_execution_time;
    
    // Results tracking
    int consecutive_successes;
    int consecutive_failures;
    probe_result_t last_result;
    
    // Status
    int enabled;
    int has_executed;
    int is_successful;
} probe_state_t;

// Container health status
typedef struct {
    char* container_name;
    
    // Probe states
    probe_state_t startup_probe;
    probe_state_t readiness_probe;
    probe_state_t liveness_probe;
    
    // Container state (mirrors Kubernetes)
    enum {
        CONTAINER_STATE_WAITING = 0,
        CONTAINER_STATE_RUNNING = 1,
        CONTAINER_STATE_TERMINATED = 2
    } state;
    
    // Current health
    int is_ready;
    int is_alive;
    
    // Restart info
    int restart_count;
    int last_restart_timestamp;
} container_health_t;

// ============================================================================
// Probe Execution Functions
// ============================================================================

/**
 * Execute HTTP probe against container
 * @param pod_name - Pod name
 * @param namespace - Pod namespace
 * @param container_name - Container name
 * @param probe_cfg - HTTP probe configuration
 * @return PROBE_SUCCESS, PROBE_FAILURE, or PROBE_UNKNOWN
 */
probe_result_t health_probe_execute_http(const char* pod_name, const char* namespace,
                                         const char* container_name, probe_config_t* probe_cfg);

/**
 * Execute TCP probe against container
 * @param pod_name - Pod name
 * @param namespace - Pod namespace
 * @param container_name - Container name
 * @param probe_cfg - TCP probe configuration
 * @return PROBE_SUCCESS, PROBE_FAILURE, or PROBE_UNKNOWN
 */
probe_result_t health_probe_execute_tcp(const char* pod_name, const char* namespace,
                                        const char* container_name, probe_config_t* probe_cfg);

/**
 * Execute command probe against container (via container exec)
 * @param pod_name - Pod name
 * @param namespace - Pod namespace
 * @param container_name - Container name
 * @param probe_cfg - Exec probe configuration
 * @return PROBE_SUCCESS, PROBE_FAILURE, or PROBE_UNKNOWN
 */
probe_result_t health_probe_execute_exec(const char* pod_name, const char* namespace,
                                         const char* container_name, probe_config_t* probe_cfg);

/**
 * Execute appropriate probe handler
 * @param pod_name - Pod name
 * @param namespace - Pod namespace
 * @param container_name - Container name
 * @param probe_cfg - Probe configuration
 * @return Probe result
 */
probe_result_t health_probe_execute(const char* pod_name, const char* namespace,
                                    const char* container_name, probe_config_t* probe_cfg);

// ============================================================================
// Probe State Management
// ============================================================================

/**
 * Create probe state from container probe configuration
 */
probe_state_t health_probe_state_create(probe_type_t type, const k8s_container_t* container);

/**
 * Update probe state after execution
 * @return 1 if status changed, 0 otherwise
 */
int health_probe_state_update(probe_state_t* state, probe_result_t result, time_t now);

/**
 * Check if probe is due for execution based on timing
 */
int health_probe_state_is_due(probe_state_t* state, time_t now);

/**
 * Check if startup probe has completed
 */
int health_probe_startup_completed(probe_state_t* startup);

// ============================================================================
// Container Health Management
// ============================================================================

/**
 * Create container health state
 */
container_health_t* health_container_create(const char* container_name);

/**
 * Free container health state
 */
void health_container_free(container_health_t* health);

/**
 * Update container health based on probe results
 * Handles probe success/failure thresholds and state transitions
 */
int health_container_update(container_health_t* health, time_t now);

/**
 * Determine if container should be restarted based on liveness probe
 */
int health_container_should_restart(container_health_t* health);

/**
 * Reset restart counters (typically when container running successfully)
 */
int health_container_reset_restart_count(container_health_t* health);

/**
 * Handle container restart (increment counter, update timestamps)
 */
int health_container_handle_restart(container_health_t* health);

// ============================================================================
// Probe Configuration Parsing
// ============================================================================

/**
 * Create probe config from JSON object (parsed from Pod spec)
 * Returns NULL if probe not defined
 */
probe_config_t* health_probe_config_from_json(json_object* probe_json);

/**
 * Free probe configuration
 */
void health_probe_config_free(probe_config_t* config);

// ============================================================================
// Diagnostics
// ============================================================================

/**
 * Get human-readable probe result string
 */
const char* health_probe_result_str(probe_result_t result);

/**
 * Get container health summary (for logging)
 */
char* health_container_summary(container_health_t* health, char* buf, int buf_len);

#endif // SIRAH_HEALTH_PROBE_H
