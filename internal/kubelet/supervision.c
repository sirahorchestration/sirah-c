#include "supervision.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

// Supervisor creation and lifecycle
supervisor_t* supervisor_new(int max_failures_per_window, int failure_window_ms) {
    supervisor_t* supervisor = (supervisor_t*)malloc(sizeof(supervisor_t));
    if (!supervisor) return NULL;
    
    memset(supervisor, 0, sizeof(supervisor_t));
    
    supervisor->max_failures_per_window = max_failures_per_window;
    supervisor->failure_window_ms = failure_window_ms;
    supervisor->component_capacity = 10;
    supervisor->components = (supervised_component_t*)malloc(
        sizeof(supervised_component_t) * supervisor->component_capacity);
    supervisor->component_count = 0;
    supervisor->running = 0;
    
    return supervisor;
}

void supervisor_free(supervisor_t* supervisor) {
    if (!supervisor) return;
    
    for (int i = 0; i < supervisor->component_count; i++) {
        free(supervisor->components[i].name);
    }
    free(supervisor->components);
    free(supervisor);
}

// Component management
int supervisor_add_component(supervisor_t* supervisor, const char* name,
                             component_func_t func, void* context,
                             restart_strategy_t strategy, int max_restarts) {
    if (!supervisor || !name || !func) return -1;
    
    // Expand if needed
    if (supervisor->component_count >= supervisor->component_capacity) {
        supervisor->component_capacity *= 2;
        supervisor->components = realloc(supervisor->components,
            sizeof(supervised_component_t) * supervisor->component_capacity);
    }
    
    supervised_component_t* comp = &supervisor->components[supervisor->component_count];
    memset(comp, 0, sizeof(supervised_component_t));
    
    comp->name = strdup(name);
    comp->func = func;
    comp->context = context;
    comp->strategy = strategy;
    comp->max_restarts = max_restarts;
    comp->state = COMPONENT_STOPPED;
    comp->backoff_ms = 100;  // Initial backoff
    
    supervisor->component_count++;
    printf("Component '%s' added to supervisor\n", name);
    
    return 0;
}

// Supervisor control
int supervisor_start(supervisor_t* supervisor) {
    if (!supervisor) return -1;
    
    supervisor->running = 1;
    
    // Start all components
    for (int i = 0; i < supervisor->component_count; i++) {
        supervisor->components[i].state = COMPONENT_RUNNING;
        supervisor->components[i].last_restart = time(NULL);
    }
    
    printf("Supervisor started with %d components\n", supervisor->component_count);
    
    return 0;
}

// Backoff calculation with exponential growth
int supervisor_calculate_backoff(int initial_ms, int failure_count) {
    int backoff = initial_ms;
    
    // Exponential backoff: initial * 2^failures, capped at 32x
    for (int i = 0; i < failure_count && i < 5; i++) {
        backoff *= 2;
    }
    
    // Cap at max (32 * initial)
    int max_backoff = initial_ms * 32;
    if (backoff > max_backoff) {
        backoff = max_backoff;
    }
    
    return backoff;
}

// Add random jitter (0-10%) to prevent thundering herd
int supervisor_add_jitter(int backoff_ms) {
    // Add 0-10% random jitter
    int jitter = rand() % (backoff_ms / 10 + 1);
    return backoff_ms + jitter;
}

// Restart component with backoff
int supervisor_restart_component(supervisor_t* supervisor, int component_idx) {
    if (!supervisor || component_idx < 0 || component_idx >= supervisor->component_count) {
        return -1;
    }
    
    supervised_component_t* comp = &supervisor->components[component_idx];
    
    // Check if max restarts exceeded
    if (comp->failure_count >= comp->max_restarts) {
        printf("Component '%s' exceeded max restarts (%d), shutting down\n",
               comp->name, comp->max_restarts);
        comp->state = COMPONENT_STOPPED;
        return -1;
    }
    
    // Calculate backoff
    int backoff = supervisor_calculate_backoff(100, comp->failure_count);
    backoff = supervisor_add_jitter(backoff);
    
    printf("Component '%s' restarting (failure %d/%d, backoff %dms)\n",
           comp->name, comp->failure_count + 1, comp->max_restarts, backoff);
    
    // Apply backoff
    usleep(backoff * 1000);  // Convert to microseconds
    
    comp->failure_count++;
    comp->last_restart = time(NULL);
    comp->state = COMPONENT_RUNNING;
    
    return 0;
}

// Get component status
int supervisor_get_component_status(supervisor_t* supervisor, int component_idx) {
    if (!supervisor || component_idx < 0 || component_idx >= supervisor->component_count) {
        return -1;
    }
    
    return supervisor->components[component_idx].state;
}

// Main supervisor loop
int supervisor_run(supervisor_t* supervisor) {
    if (!supervisor) return -1;
    
    supervisor_start(supervisor);
    
    while (supervisor->running) {
        // Monitor each component
        for (int i = 0; i < supervisor->component_count; i++) {
            supervised_component_t* comp = &supervisor->components[i];
            
            if (comp->state != COMPONENT_RUNNING) {
                continue;
            }
            
            // Run component function
            int result = comp->func(comp->context);
            
            // Handle result based on restart strategy
            if (result != 0) {
                switch (comp->strategy) {
                    case RESTART_PERMANENT:
                        printf("Component '%s' failed, restarting (PERMANENT)\n", comp->name);
                        supervisor_restart_component(supervisor, i);
                        break;
                    
                    case RESTART_TRANSIENT:
                        // Restart on non-zero exit
                        if (result != 0) {
                            printf("Component '%s' failed, restarting (TRANSIENT)\n", comp->name);
                            supervisor_restart_component(supervisor, i);
                        }
                        break;
                    
                    case RESTART_TEMPORARY:
                        // Never restart
                        printf("Component '%s' failed, not restarting (TEMPORARY)\n", comp->name);
                        comp->state = COMPONENT_STOPPED;
                        break;
                }
            }
        }
        
        // Check if any components are still running
        int any_running = 0;
        for (int i = 0; i < supervisor->component_count; i++) {
            if (supervisor->components[i].state == COMPONENT_RUNNING) {
                any_running = 1;
                break;
            }
        }
        
        if (!any_running) {
            printf("All components have stopped, exiting supervisor\n");
            break;
        }
        
        // Sleep 100ms before next check
        usleep(100000);
    }
    
    return 0;
}

// Supervisor stop
void supervisor_stop(supervisor_t* supervisor) {
    if (!supervisor) return;
    
    supervisor->running = 0;
    
    // Mark all components for stopping
    for (int i = 0; i < supervisor->component_count; i++) {
        supervisor->components[i].state = COMPONENT_STOPPED;
    }
    
    printf("Supervisor stopping\n");
}
