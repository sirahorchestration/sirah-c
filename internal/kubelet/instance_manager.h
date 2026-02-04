/*
 * instance_manager.h
 *
 * Instance metadata tracking and lifecycle management
 */

#ifndef SIRAH_INSTANCE_MANAGER_H
#define SIRAH_INSTANCE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

/**
 * Instance status
 */
typedef enum {
    INSTANCE_STATUS_PENDING = 0,       // Waiting to start
    INSTANCE_STATUS_STARTING = 1,      // Starting QEMU process
    INSTANCE_STATUS_RUNNING = 2,       // QEMU running
    INSTANCE_STATUS_READY = 3,         // Ready for traffic
    INSTANCE_STATUS_NOT_READY = 4,     // Not ready (failed health check)
    INSTANCE_STATUS_STOPPING = 5,      // Shutting down
    INSTANCE_STATUS_STOPPED = 6,       // Stopped
    INSTANCE_STATUS_FAILED = 7,        // Failed (unrecoverable)
    INSTANCE_STATUS_UNKNOWN = 8        // Unknown state
} instance_status_t;

/**
 * Instance metadata
 */
typedef struct {
    char *instance_id;                 // Unique ID
    char *pod_name;                    // Pod name
    char *namespace;                   // Pod namespace
    char *node_name;                   // Node running on
    
    // Pod specification
    char *image;                       // Container image
    char *pod_ip;                      // Pod IP address
    uint16_t *container_ports;         // Container ports
    uint32_t port_count;               // Number of ports
    
    // Resource limits
    uint64_t memory_limit_bytes;        // Memory limit
    uint64_t memory_request_bytes;      // Memory request
    uint64_t cpu_limit_millicores;      // CPU limit
    uint64_t cpu_request_millicores;    // CPU request
    
    // Instance state
    instance_status_t status;           // Current status
    uint32_t pid;                       // QEMU process ID
    uint32_t restart_count;             // Number of restarts
    time_t created_time;                // Creation time
    time_t started_time;                // Start time
    time_t stopped_time;                // Stop time
    
    // Health tracking
    bool ready;                         // Ready for traffic
    bool alive;                         // Container alive
    uint32_t failed_health_checks;      // Consecutive health check failures
    
    // Metadata labels
    char **labels;                      // Label key-value pairs
    uint32_t label_count;               // Number of labels
    
} instance_metadata_t;

/**
 * Instance manager
 */
typedef struct {
    instance_metadata_t metadata;
    
    // Restart configuration
    uint32_t max_restart_count;         // Max restarts before giving up
    uint32_t restart_backoff_seconds;   // Initial backoff
    uint32_t max_backoff_seconds;       // Max backoff
    float backoff_multiplier;           // Backoff increase multiplier
    
    // Monitoring
    time_t last_status_update;
    time_t next_restart_time;
    
    pthread_mutex_t mutex;
    
} instance_manager_t;

/**
 * Create instance manager
 */
instance_manager_t* instance_manager_create(const char *instance_id,
                                            const char *pod_name,
                                            const char *namespace,
                                            const char *node_name);

/**
 * Free instance manager
 */
void instance_manager_free(instance_manager_t *manager);

/**
 * Set pod specification
 */
bool instance_manager_set_pod_spec(instance_manager_t *manager,
                                    const char *image,
                                    const char *pod_ip,
                                    uint64_t memory_limit,
                                    uint64_t cpu_limit_millicores);

/**
 * Add container port
 */
bool instance_manager_add_port(instance_manager_t *manager, uint16_t port);

/**
 * Set resource limits
 */
bool instance_manager_set_resources(instance_manager_t *manager,
                                     uint64_t memory_limit,
                                     uint64_t memory_request,
                                     uint64_t cpu_limit,
                                     uint64_t cpu_request);

/**
 * Update instance status
 */
bool instance_manager_update_status(instance_manager_t *manager,
                                     instance_status_t new_status,
                                     uint32_t pid);

/**
 * Mark instance as ready
 */
bool instance_manager_set_ready(instance_manager_t *manager, bool ready);

/**
 * Mark instance as alive
 */
bool instance_manager_set_alive(instance_manager_t *manager, bool alive);

/**
 * Record failed health check
 */
bool instance_manager_health_check_failed(instance_manager_t *manager);

/**
 * Reset health check failures
 */
bool instance_manager_health_check_success(instance_manager_t *manager);

/**
 * Mark instance for restart
 */
bool instance_manager_mark_restart(instance_manager_t *manager);

/**
 * Get restart backoff time
 */
uint32_t instance_manager_get_restart_backoff(instance_manager_t *manager);

/**
 * Check if restart is allowed
 */
bool instance_manager_can_restart(instance_manager_t *manager);

/**
 * Get metadata
 */
bool instance_manager_get_metadata(instance_manager_t *manager,
                                    instance_metadata_t *out_metadata);

/**
 * Add label
 */
bool instance_manager_add_label(instance_manager_t *manager,
                                 const char *key,
                                 const char *value);

/**
 * Get label value
 */
const char* instance_manager_get_label(instance_manager_t *manager,
                                        const char *key);

#endif // SIRAH_INSTANCE_MANAGER_H
