/*
 * instance_lifecycle.h
 * 
 * Instance/pod lifecycle management:
 * - Metadata tracking
 * - Failure detection
 * - Automatic restart with exponential backoff
 * - Graceful shutdown
 * - Node drain mode
 */

#ifndef SIRAH_INSTANCE_LIFECYCLE_H
#define SIRAH_INSTANCE_LIFECYCLE_H

#include <stdint.h>
#include <time.h>
#include <stdbool.h>
#include <pthread.h>

/**
 * Instance lifecycle states
 */
typedef enum {
    INSTANCE_STATE_CREATED = 0,      // Instance created, not started
    INSTANCE_STATE_RUNNING = 1,      // Instance running normally
    INSTANCE_STATE_READY = 2,        // Instance ready for traffic (probes passed)
    INSTANCE_STATE_NOT_READY = 3,    // Instance not ready (readiness probe failed)
    INSTANCE_STATE_RESTARTING = 4,   // Container restarting
    INSTANCE_STATE_STOPPING = 5,     // Graceful shutdown in progress
    INSTANCE_STATE_STOPPED = 6,      // Instance stopped
    INSTANCE_STATE_FAILED = 7,       // Instance failed permanently
    INSTANCE_STATE_DRAINING = 8      // Node drain: evicting pods
} instance_state_t;

/**
 * Instance metadata and lifecycle tracking
 */
typedef struct {
    // Identity
    char *pod_name;                  // Pod identifier
    char *namespace;                 // Pod namespace
    char *node_name;                 // Node running pod
    
    // Process information
    pid_t process_id;                // QEMU process ID
    char *process_path;              // Path to QEMU binary
    
    // Resource limits
    uint64_t memory_limit_bytes;     // Memory limit
    uint32_t cpu_limit_millicores;   // CPU limit in millicores
    
    // State tracking
    instance_state_t state;          // Current state
    time_t state_change_time;        // When state last changed
    time_t creation_time;            // When instance was created
    time_t start_time;               // When instance was started
    
    // Restart information
    uint32_t restart_count;          // Total number of restarts
    time_t last_restart_time;        // When last restart occurred
    uint32_t consecutive_failures;   // Consecutive failures
    
    // Exit/failure tracking
    int last_exit_code;              // Exit code from last run
    char *last_failure_reason;       // Reason for last failure
    time_t last_failure_time;        // When last failure occurred
    
    // Ready status
    bool is_ready;                   // Pod ready for traffic
    time_t ready_time;               // When pod became ready
    
    // Metadata
    char **labels;                   // Pod labels (key=value)
    uint32_t label_count;
    
    pthread_mutex_t mutex;           // Thread safety
    
} instance_metadata_t;

/**
 * Create instance metadata
 */
instance_metadata_t* instance_metadata_create(const char *pod_name,
                                             const char *namespace,
                                             const char *node_name);

/**
 * Free instance metadata
 */
void instance_metadata_free(instance_metadata_t *metadata);

/**
 * Set process information
 */
bool instance_metadata_set_process(instance_metadata_t *metadata,
                                  pid_t process_id,
                                  const char *process_path);

/**
 * Set resource limits
 */
bool instance_metadata_set_limits(instance_metadata_t *metadata,
                                 uint64_t memory_bytes,
                                 uint32_t cpu_millicores);

/**
 * Update instance state
 */
bool instance_metadata_set_state(instance_metadata_t *metadata,
                                instance_state_t new_state);

/**
 * Get current state
 */
instance_state_t instance_metadata_get_state(instance_metadata_t *metadata);

/**
 * Mark instance as ready
 */
bool instance_metadata_set_ready(instance_metadata_t *metadata);

/**
 * Mark instance as not ready
 */
bool instance_metadata_set_not_ready(instance_metadata_t *metadata);

/**
 * Record restart event
 */
bool instance_metadata_record_restart(instance_metadata_t *metadata);

/**
 * Record failure event
 */
bool instance_metadata_record_failure(instance_metadata_t *metadata,
                                     int exit_code,
                                     const char *failure_reason);

/**
 * Clear consecutive failures
 */
bool instance_metadata_clear_failures(instance_metadata_t *metadata);

/**
 * Check if instance should be auto-restarted
 */
bool instance_metadata_should_restart(instance_metadata_t *metadata);

/**
 * Get restart delay with exponential backoff
 */
uint32_t instance_metadata_get_restart_delay(instance_metadata_t *metadata);

/**
 * Instance failure detector and lifecycle manager
 */
typedef struct {
    instance_metadata_t **instances;
    uint32_t instance_count;
    
    pthread_t monitor_thread;
    bool running;
    uint32_t check_interval_seconds;
    
    // Restart policy
    uint32_t max_restarts;           // Max restarts before giving up
    uint32_t initial_backoff_seconds; // Initial restart delay
    uint32_t max_backoff_seconds;    // Max restart delay
    
    // Node drain mode
    bool drain_mode;                 // Node is draining
    uint32_t grace_period_seconds;   // Graceful shutdown timeout
    
    pthread_mutex_t mutex;
} instance_lifecycle_manager_t;

/**
 * Create lifecycle manager
 */
instance_lifecycle_manager_t* instance_lifecycle_manager_create(uint32_t check_interval);

/**
 * Free lifecycle manager
 */
void instance_lifecycle_manager_free(instance_lifecycle_manager_t *manager);

/**
 * Add instance to manager
 */
bool instance_lifecycle_manager_add(instance_lifecycle_manager_t *manager,
                                   instance_metadata_t *metadata);

/**
 * Remove instance from manager
 */
bool instance_lifecycle_manager_remove(instance_lifecycle_manager_t *manager,
                                      const char *pod_name);

/**
 * Get instance by name
 */
instance_metadata_t* instance_lifecycle_manager_get(instance_lifecycle_manager_t *manager,
                                                   const char *pod_name);

/**
 * Start monitoring thread
 */
bool instance_lifecycle_manager_start(instance_lifecycle_manager_t *manager);

/**
 * Stop monitoring thread
 */
bool instance_lifecycle_manager_stop(instance_lifecycle_manager_t *manager);

/**
 * Check instance health and detect failures
 * Returns true if instance failed
 */
bool instance_check_health(instance_metadata_t *metadata);

/**
 * Gracefully shutdown instance
 * Sends SIGTERM, waits, then SIGKILL
 */
bool instance_shutdown_graceful(instance_metadata_t *metadata,
                               uint32_t grace_period_seconds);

/**
 * Force kill instance
 */
bool instance_shutdown_force(instance_metadata_t *metadata);

/**
 * Restart instance with exponential backoff
 * Returns true if restart scheduled
 */
bool instance_restart(instance_metadata_t *metadata);

/**
 * Enable node drain mode
 * Stop accepting new pods, evict existing pods
 */
bool instance_lifecycle_manager_enable_drain(instance_lifecycle_manager_t *manager);

/**
 * Disable node drain mode
 */
bool instance_lifecycle_manager_disable_drain(instance_lifecycle_manager_t *manager);

/**
 * Check if node is draining
 */
bool instance_lifecycle_manager_is_draining(instance_lifecycle_manager_t *manager);

/**
 * Get drain status
 */
uint32_t instance_lifecycle_manager_get_draining_count(instance_lifecycle_manager_t *manager);

/**
 * State machine helper: state to string
 */
const char* instance_state_to_string(instance_state_t state);

/**
 * State machine helper: string to state
 */
instance_state_t instance_string_to_state(const char *state_str);

#endif /* SIRAH_INSTANCE_LIFECYCLE_H */
