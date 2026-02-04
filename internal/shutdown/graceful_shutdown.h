/*
 * graceful_shutdown.h
 * 
 * Graceful shutdown handler for control plane components
 * 
 * Features:
 *   - SIGTERM/SIGINT signal handlers
 *   - Request draining (wait for in-flight requests to complete)
 *   - Configurable shutdown timeout
 *   - Forceful shutdown if timeout expires
 * 
 * Kubernetes v1.28 Conformance:
 *   - Implements Kubernetes API server graceful shutdown
 *   - Handles preemption of requests during shutdown
 *   - Respects termination grace period
 * 
 * References:
 *   - https://kubernetes.io/docs/tasks/run-application/graceful-node-shutdown/
 */

#ifndef SIRAH_GRACEFUL_SHUTDOWN_H
#define SIRAH_GRACEFUL_SHUTDOWN_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>

/**
 * Shutdown state
 */
typedef enum {
    SHUTDOWN_STATE_RUNNING = 0,      // Normal operation
    SHUTDOWN_STATE_SHUTTING_DOWN = 1 // Shutdown in progress
} shutdown_state_t;

/**
 * Shutdown handler function signature
 * Called when shutdown is initiated
 */
typedef void (*shutdown_handler_func_t)(void *userdata);

/**
 * Shutdown manager configuration
 */
typedef struct {
    uint32_t grace_period_seconds;    // Time to allow requests to drain (e.g., 30)
    uint32_t force_timeout_seconds;   // Time before forceful shutdown (e.g., 45)
    bool     drain_requests;          // Wait for in-flight requests
} shutdown_config_t;

/**
 * Request tracking for draining
 */
typedef struct {
    uint32_t active_requests;         // Current in-flight requests
    uint32_t total_drained;           // Total requests that drained successfully
    time_t   shutdown_start;          // Timestamp when shutdown initiated
} shutdown_stats_t;

/**
 * Shutdown manager instance
 */
typedef struct {
    shutdown_config_t config;
    shutdown_state_t state;
    shutdown_stats_t stats;
    
    pthread_mutex_t mutex;
    pthread_cond_t  all_requests_done;
    
    // Registered shutdown handlers
    shutdown_handler_func_t *handlers;
    void **handler_userdata;
    uint32_t handler_count;
    uint32_t handler_capacity;
} shutdown_manager_t;

/**
 * Create a new shutdown manager
 */
shutdown_manager_t* shutdown_manager_new(const shutdown_config_t *config);

/**
 * Free shutdown manager
 */
void shutdown_manager_free(shutdown_manager_t *manager);

/**
 * Register a shutdown handler
 * 
 * Handlers are called in reverse order of registration when shutdown occurs
 * Allows components to clean up resources gracefully
 * 
 * @param manager Shutdown manager
 * @param handler Function to call on shutdown
 * @param userdata User-provided data passed to handler
 * @return true on success
 */
bool shutdown_manager_register_handler(shutdown_manager_t *manager,
                                       shutdown_handler_func_t handler,
                                       void *userdata);

/**
 * Setup signal handlers for graceful shutdown
 * 
 * Registers SIGTERM and SIGINT handlers that initiate shutdown
 * Must be called once per process, before starting work
 * 
 * @param manager Shutdown manager (required)
 * @return true on success
 */
bool shutdown_manager_setup_signal_handlers(shutdown_manager_t *manager);

/**
 * Initiate graceful shutdown
 * 
 * Calls all registered handlers in reverse registration order
 * Waits for in-flight requests to drain (respecting grace period)
 * Forces shutdown after timeout
 * 
 * Non-blocking - sets shutdown flag and returns
 * Call shutdown_manager_wait() to wait for completion
 * 
 * @param manager Shutdown manager
 */
void shutdown_manager_initiate(shutdown_manager_t *manager);

/**
 * Wait for shutdown to complete
 * 
 * Blocks until all requests have drained or timeout expires
 * 
 * @param manager Shutdown manager
 * @return true if shutdown completed, false if timeout
 */
bool shutdown_manager_wait(shutdown_manager_t *manager);

/**
 * Increment active request counter
 * 
 * Call at start of each request
 * Prevents shutdown from completing until request finishes
 */
void shutdown_manager_request_start(shutdown_manager_t *manager);

/**
 * Decrement active request counter
 * 
 * Call at end of each request
 * If active_requests reaches 0, notifies waiting shutdown
 */
void shutdown_manager_request_end(shutdown_manager_t *manager);

/**
 * Check if shutdown is in progress
 * 
 * Returns true if shutdown has been initiated
 * Used by request handlers to know if new requests should be accepted
 */
bool shutdown_manager_is_shutting_down(shutdown_manager_t *manager);

/**
 * Get shutdown statistics
 */
void shutdown_manager_get_stats(shutdown_manager_t *manager, shutdown_stats_t *out_stats);

#endif // SIRAH_GRACEFUL_SHUTDOWN_H
