// internal/agent/supervision.c
// OTP-Style Supervision Framework Implementation

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include "supervision.h"

// ============ Helper Functions ============

const char* supervisor_state_to_string(supervisor_state_t state) {
    switch (state) {
        case SUPERVISOR_STOPPED: return "Stopped";
        case SUPERVISOR_STARTING: return "Starting";
        case SUPERVISOR_RUNNING: return "Running";
        case SUPERVISOR_STOPPING: return "Stopping";
        default: return "Unknown";
    }
}

const char* restart_strategy_to_string(restart_strategy_t strategy) {
    switch (strategy) {
        case RESTART_PERMANENT: return "Permanent";
        case RESTART_TRANSIENT: return "Transient";
        case RESTART_TEMPORARY: return "Temporary";
        default: return "Unknown";
    }
}

// Calculate backoff with exponential decay
int supervisor_calculate_backoff(int failure_count,
                                int initial_backoff_ms,
                                int max_backoff_ms) {
    // Exponential backoff: initial * 2^failures, capped at max
    int backoff = initial_backoff_ms;
    for (int i = 0; i < failure_count && backoff < max_backoff_ms; i++) {
        backoff *= 2;
    }
    return (backoff > max_backoff_ms) ? max_backoff_ms : backoff;
}

// Add jitter to backoff
int supervisor_add_jitter(int backoff_ms) {
    // Add 0-10% jitter
    int jitter = (backoff_ms / 10) * (rand() % 11) / 10;
    return backoff_ms + jitter;
}

// ============ Lifecycle Functions ============

supervisor_t* supervisor_new(const char* name) {
    if (!name) return NULL;

    supervisor_t* supervisor = calloc(1, sizeof(supervisor_t));
    if (!supervisor) return NULL;

    strncpy(supervisor->supervisor_name, name, sizeof(supervisor->supervisor_name) - 1);
    
    supervisor->max_components = 50;
    supervisor->components = calloc(supervisor->max_components, sizeof(supervised_component_t));
    if (!supervisor->components) {
        free(supervisor);
        return NULL;
    }

    supervisor->state = SUPERVISOR_STOPPED;
    supervisor->running = 0;
    supervisor->max_restart_intensity = 10;  // Max 10 failures
    supervisor->restart_period = 60;         // In 60 seconds

    printf("Supervisor created: %s\n", name);
    return supervisor;
}

void supervisor_free(supervisor_t* supervisor) {
    if (!supervisor) return;

    if (supervisor->components) {
        // Free component contexts if needed
        free(supervisor->components);
    }

    free(supervisor);
}

int supervisor_add_component(supervisor_t* supervisor,
                             const char* component_name,
                             int (*component_fn)(void* context),
                             void* context,
                             restart_strategy_t strategy,
                             int max_restarts) {
    if (!supervisor || !component_name || !component_fn) return -1;

    if (supervisor->num_components >= supervisor->max_components) {
        printf("ERROR: Supervisor component list full\n");
        return -1;
    }

    supervised_component_t* comp = &supervisor->components[supervisor->num_components];
    strncpy(comp->name, component_name, sizeof(comp->name) - 1);
    comp->component_fn = component_fn;
    comp->component_context = context;
    comp->strategy = strategy;
    comp->max_restarts = max_restarts;
    comp->restart_time_window = 60;         // 60 second window
    comp->restart_backoff_ms = 1000;        // 1 second initial
    comp->max_backoff_ms = 60000;           // 60 second max
    comp->running = 0;
    comp->restart_count = 0;
    comp->consecutive_failures = 0;

    supervisor->num_components++;

    printf("  Added component: %s (%s)\n", component_name,
           restart_strategy_to_string(strategy));
    return 0;
}

int supervisor_start(supervisor_t* supervisor) {
    if (!supervisor) return -1;

    supervisor->state = SUPERVISOR_STARTING;
    supervisor->started_at = time(NULL);

    printf("Supervisor %s starting (%d components)\n",
           supervisor->supervisor_name, supervisor->num_components);

    for (int i = 0; i < supervisor->num_components; i++) {
        supervised_component_t* comp = &supervisor->components[i];
        printf("  Starting component: %s\n", comp->name);
        comp->running = 1;
    }

    supervisor->state = SUPERVISOR_RUNNING;
    supervisor->running = 1;
    return 0;
}

void supervisor_stop(supervisor_t* supervisor) {
    if (!supervisor) return;

    supervisor->state = SUPERVISOR_STOPPING;
    printf("Supervisor %s stopping...\n", supervisor->supervisor_name);

    for (int i = 0; i < supervisor->num_components; i++) {
        supervisor_stop_component(supervisor, supervisor->components[i].name);
    }

    supervisor->state = SUPERVISOR_STOPPED;
    supervisor->running = 0;
}

// ============ Component Management ============

int supervisor_restart_component(supervisor_t* supervisor,
                                const char* component_name) {
    if (!supervisor || !component_name) return -1;

    supervised_component_t* comp = supervisor_get_component(supervisor, component_name);
    if (!comp) return -1;

    printf("[Supervision] Restarting component: %s\n", component_name);
    
    // Check restart policy
    time_t now = time(NULL);
    
    // Reset window if expired
    if (now - comp->restart_window_start > comp->restart_time_window) {
        comp->restart_window_start = now;
        comp->restart_count = 0;
        comp->consecutive_failures = 0;
    }

    // Check max restarts in window
    if (comp->restart_count >= comp->max_restarts) {
        printf("[Supervision] Max restarts reached for %s\n", component_name);
        
        if (comp->strategy == RESTART_TEMPORARY) {
            printf("[Supervision] Component %s: TEMPORARY - not restarting\n", component_name);
            comp->running = 0;
            return -1;
        }
    }

    // Calculate backoff
    int backoff_ms = supervisor_calculate_backoff(comp->consecutive_failures,
                                                 comp->restart_backoff_ms,
                                                 comp->max_backoff_ms);
    backoff_ms = supervisor_add_jitter(backoff_ms);

    printf("[Supervision] Component %s: waiting %d ms before restart\n",
           component_name, backoff_ms);

    usleep(backoff_ms * 1000);

    // Restart component
    comp->running = 1;
    comp->restart_count++;
    comp->last_restart_time = time(NULL);

    printf("[Supervision] Component %s restarted (attempt %d)\n",
           component_name, comp->restart_count);

    return 0;
}

int supervisor_stop_component(supervisor_t* supervisor,
                             const char* component_name) {
    if (!supervisor || !component_name) return -1;

    supervised_component_t* comp = supervisor_get_component(supervisor, component_name);
    if (!comp) return -1;

    printf("[Supervision] Stopping component: %s\n", component_name);
    comp->running = 0;

    return 0;
}

supervised_component_t* supervisor_get_component(supervisor_t* supervisor,
                                                 const char* component_name) {
    if (!supervisor || !component_name) return NULL;

    for (int i = 0; i < supervisor->num_components; i++) {
        if (strcmp(supervisor->components[i].name, component_name) == 0) {
            return &supervisor->components[i];
        }
    }

    return NULL;
}

int supervisor_component_is_running(supervisor_t* supervisor,
                                   const char* component_name) {
    supervised_component_t* comp = supervisor_get_component(supervisor, component_name);
    if (!comp) return 0;
    return comp->running;
}

// ============ Main Supervision Loop ============

int supervisor_run(supervisor_t* supervisor) {
    if (!supervisor) return -1;

    if (supervisor_start(supervisor) != 0) return -1;

    printf("Supervisor main loop started\n");

    while (supervisor->running) {
        // Monitor each component
        for (int i = 0; i < supervisor->num_components; i++) {
            supervised_component_t* comp = &supervisor->components[i];

            if (!comp->running) {
                // Component is not running
                
                if (comp->strategy == RESTART_PERMANENT) {
                    // Always restart permanent components
                    printf("[Supervision] Permanent component %s stopped, restarting\n",
                           comp->name);
                    supervisor_restart_component(supervisor, comp->name);
                } else if (comp->strategy == RESTART_TRANSIENT) {
                    // Restart transient only if it failed (exit_code != 0)
                    if (comp->exit_code != 0) {
                        printf("[Supervision] Transient component %s failed, restarting\n",
                               comp->name);
                        supervisor_restart_component(supervisor, comp->name);
                    }
                } else {
                    // RESTART_TEMPORARY: don't restart
                    printf("[Supervision] Temporary component %s stopped, not restarting\n",
                           comp->name);
                }
            }
        }

        sleep(1);  // Check every second
    }

    return 0;
}
