// internal/agent/supervision.h
// OTP-Style Supervision Framework for Sirah Agent
// Implements component supervision with automatic restart, max restart rates, and backoff strategies

#ifndef SIRAH_SUPERVISION_H
#define SIRAH_SUPERVISION_H

#include <time.h>

// ============ Type Definitions ============

// Supervisor state machine
typedef enum {
    SUPERVISOR_STOPPED = 0,
    SUPERVISOR_STARTING = 1,
    SUPERVISOR_RUNNING = 2,
    SUPERVISOR_STOPPING = 3
} supervisor_state_t;

// Restart strategy
typedef enum {
    RESTART_PERMANENT = 0,      // Always restart
    RESTART_TRANSIENT = 1,      // Restart only on error
    RESTART_TEMPORARY = 2       // Never restart, log and stop
} restart_strategy_t;

// Component to supervise
typedef struct {
    char name[256];                 // Component name (e.g., "pod_sync", "heartbeat")
    
    // Component function
    int (*component_fn)(void* context);  // Function that runs component
    void* component_context;            // Context passed to component_fn
    
    // Restart policy
    restart_strategy_t strategy;    // PERMANENT, TRANSIENT, TEMPORARY
    int max_restarts;               // Max restarts within window (default: 5)
    int restart_time_window;        // Time window in seconds (default: 60)
    int restart_backoff_ms;         // Initial backoff in milliseconds (default: 1000)
    int max_backoff_ms;             // Max backoff in milliseconds (default: 60000)
    
    // Current state
    int running;                    // 1 if component is currently running
    int restart_count;              // Restarts in current window
    int consecutive_failures;       // Consecutive failures (for exponential backoff)
    time_t last_restart_time;       // When we last restarted
    time_t restart_window_start;    // Start of current restart window
    int exit_code;                  // Last exit code
} supervised_component_t;

// Supervisor tree
typedef struct {
    char supervisor_name[256];      // Name of this supervisor
    
    supervised_component_t* components;
    int num_components;
    int max_components;
    
    supervisor_state_t state;
    int running;
    
    // Supervision policy
    int max_restart_intensity;      // Max failures before killing supervisor
    int restart_period;             // Period in seconds for intensity check
    
    time_t started_at;
} supervisor_t;

// ============ Supervisor Lifecycle ============

// Create new supervisor
supervisor_t* supervisor_new(const char* name);

// Free supervisor
void supervisor_free(supervisor_t* supervisor);

// Add component to supervisor
int supervisor_add_component(supervisor_t* supervisor,
                             const char* component_name,
                             int (*component_fn)(void* context),
                             void* context,
                             restart_strategy_t strategy,
                             int max_restarts);

// Start supervisor and all components
int supervisor_start(supervisor_t* supervisor);

// Stop supervisor and all components gracefully
void supervisor_stop(supervisor_t* supervisor);

// Run supervisor main loop (blocking)
int supervisor_run(supervisor_t* supervisor);

// ============ Component Management ============

// Restart a specific component
int supervisor_restart_component(supervisor_t* supervisor,
                                const char* component_name);

// Stop a specific component
int supervisor_stop_component(supervisor_t* supervisor,
                             const char* component_name);

// Get component status
supervised_component_t* supervisor_get_component(supervisor_t* supervisor,
                                                 const char* component_name);

// Check if component is running
int supervisor_component_is_running(supervisor_t* supervisor,
                                   const char* component_name);

// ============ Backoff Calculation ============

// Calculate backoff time with exponential decay
int supervisor_calculate_backoff(int failure_count,
                                int initial_backoff_ms,
                                int max_backoff_ms);

// Calculate jitter to avoid thundering herd
int supervisor_add_jitter(int backoff_ms);

// ============ Utility Functions ============

// Get supervisor state as string
const char* supervisor_state_to_string(supervisor_state_t state);

// Get restart strategy as string
const char* restart_strategy_to_string(restart_strategy_t strategy);

#endif // SIRAH_SUPERVISION_H
