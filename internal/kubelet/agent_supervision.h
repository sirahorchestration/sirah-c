/*
 * agent_supervision.h
 * 
 * OTP-style supervision tree for agent process management
 */

#ifndef __AGENT_SUPERVISION_H__
#define __AGENT_SUPERVISION_H__

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/**
 * Restart strategies
 */
typedef enum {
    SUPERVISION_RESTART_ONE_FOR_ONE,    // Restart only the failed child
    SUPERVISION_RESTART_ALL_FOR_ONE,    // Restart all children if one fails
    SUPERVISION_RESTART_REST_FOR_ONE    // Restart failed and all after in spec order
} supervision_restart_strategy_t;

/**
 * Supervision state
 */
typedef enum {
    SUPERVISION_STATE_INITIALIZING,
    SUPERVISION_STATE_RUNNING,
    SUPERVISION_STATE_STOPPING,
    SUPERVISION_STATE_STOPPED,
    SUPERVISION_STATE_CRASHED
} supervision_state_t;

/**
 * Child process type
 */
typedef enum {
    SUPERVISION_CHILD_PERMANENT,   // Always restart
    SUPERVISION_CHILD_TRANSIENT,   // Restart on abnormal exit
    SUPERVISION_CHILD_TEMPORARY    // No restart
} supervision_child_type_t;

/**
 * Child process specification
 */
typedef struct {
    char *id;                           // Child identifier
    char *command;                      // Command to execute
    supervision_child_type_t type;      // Child type
    uint32_t restart_count;             // Current restart count
    uint32_t max_restarts;              // Max restarts in time window
    uint32_t max_restart_window_secs;   // Time window for max restarts
    pid_t current_pid;                  // Current process ID
    int last_exit_code;                 // Last exit code
    time_t last_restart_time;           // Last restart timestamp
    time_t creation_time;               // Creation timestamp
    bool running;                       // Is process running
    pthread_mutex_t mutex;              // Synchronization
} supervision_child_spec_t;

/**
 * Supervision tree
 */
typedef struct {
    char *name;                                      // Supervisor name
    supervision_restart_strategy_t restart_strategy; // Restart strategy
    uint32_t max_restarts;                          // Max restarts allowed
    uint32_t max_restart_window_secs;               // Restart window
    uint32_t health_check_interval_secs;            // Health check interval
    
    supervision_child_spec_t **children;            // Child specifications
    uint32_t child_count;                           // Number of children
    
    supervision_state_t state;                      // Supervisor state
    pid_t supervisor_pid;                           // Supervisor process ID
    
    uint32_t restart_count;                         // Restart count in current window
    time_t last_restart_time;                       // Last restart time
    
    pthread_t monitor_thread;                       // Health monitor thread
    bool running;                                   // Is monitor thread running
    
    pthread_mutex_t mutex;                          // Synchronization
} supervision_tree_t;

/**
 * Create supervision tree
 */
supervision_tree_t* supervision_tree_create(const char *name,
                                           supervision_restart_strategy_t strategy);

/**
 * Free supervision tree
 */
void supervision_tree_free(supervision_tree_t *tree);

/**
 * Add child to supervision tree
 */
bool supervision_tree_add_child(supervision_tree_t *tree,
                               const char *child_id,
                               const char *command,
                               supervision_child_type_t type);

/**
 * Set child restart policy
 */
bool supervision_tree_set_child_policy(supervision_tree_t *tree,
                                      const char *child_id,
                                      uint32_t max_restarts,
                                      uint32_t max_restart_window_secs);

/**
 * Start supervision tree
 */
bool supervision_tree_start(supervision_tree_t *tree);

/**
 * Stop supervision tree
 */
bool supervision_tree_stop(supervision_tree_t *tree);

/**
 * Restart specific child
 */
bool supervision_tree_restart_child(supervision_tree_t *tree,
                                   const char *child_id);

/**
 * Get child state
 */
bool supervision_tree_get_child_state(supervision_tree_t *tree,
                                     const char *child_id,
                                     pid_t *out_pid,
                                     bool *out_running,
                                     int *out_exit_code);

/**
 * Get supervisor state
 */
supervision_state_t supervision_tree_get_state(supervision_tree_t *tree);

/**
 * Check if child is running
 */
bool supervision_tree_is_child_running(supervision_tree_t *tree,
                                      const char *child_id);

/**
 * Get restart count in current window
 */
uint32_t supervision_tree_get_restart_count(supervision_tree_t *tree);

/**
 * Set health check interval
 */
bool supervision_tree_set_health_check_interval(supervision_tree_t *tree,
                                               uint32_t interval_secs);

/**
 * Set max restarts policy
 */
bool supervision_tree_set_max_restarts(supervision_tree_t *tree,
                                      uint32_t max_restarts,
                                      uint32_t window_secs);

#endif /* __AGENT_SUPERVISION_H__ */
