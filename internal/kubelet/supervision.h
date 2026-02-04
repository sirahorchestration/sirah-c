#ifndef SIRAH_SUPERVISION_H
#define SIRAH_SUPERVISION_H

#include <time.h>

typedef int (*component_func_t)(void* context);

typedef enum {
    RESTART_PERMANENT,   // Always restart if stops
    RESTART_TRANSIENT,   // Restart only if exit code != 0
    RESTART_TEMPORARY    // Never restart
} restart_strategy_t;

typedef enum {
    COMPONENT_STOPPED,
    COMPONENT_STARTING,
    COMPONENT_RUNNING,
    COMPONENT_STOPPING
} component_state_t;

typedef struct {
    char* name;
    component_func_t func;
    void* context;
    restart_strategy_t strategy;
    component_state_t state;
    int failure_count;
    int max_restarts;
    time_t last_restart;
    int backoff_ms;
    int exit_code;
} supervised_component_t;

typedef struct {
    supervised_component_t* components;
    int component_count;
    int component_capacity;
    int max_failures_per_window;
    int failure_window_ms;
    int running;
} supervisor_t;

// Supervisor lifecycle
supervisor_t* supervisor_new(int max_failures_per_window, int failure_window_ms);
void supervisor_free(supervisor_t* supervisor);

// Component management
int supervisor_add_component(supervisor_t* supervisor, const char* name,
                             component_func_t func, void* context,
                             restart_strategy_t strategy, int max_restarts);

// Supervisor control
int supervisor_start(supervisor_t* supervisor);
int supervisor_run(supervisor_t* supervisor);
void supervisor_stop(supervisor_t* supervisor);

// Internal functions
int supervisor_calculate_backoff(int initial_ms, int failure_count);
int supervisor_add_jitter(int backoff_ms);
int supervisor_restart_component(supervisor_t* supervisor, int component_idx);
int supervisor_get_component_status(supervisor_t* supervisor, int component_idx);

#endif // SIRAH_SUPERVISION_H
