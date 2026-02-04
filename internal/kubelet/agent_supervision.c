/*
 * agent_supervision.c
 * 
 * OTP-style supervision tree implementation
 */

#include "agent_supervision.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

/**
 * Create child specification
 */
static supervision_child_spec_t* supervision_child_spec_create(const char *id,
                                                               const char *command) {
    if (!id || !command) return NULL;
    
    supervision_child_spec_t *spec = malloc(sizeof(supervision_child_spec_t));
    if (!spec) return NULL;
    
    memset(spec, 0, sizeof(supervision_child_spec_t));
    
    spec->id = strdup(id);
    spec->command = strdup(command);
    spec->type = SUPERVISION_CHILD_PERMANENT;
    spec->max_restarts = 3;
    spec->max_restart_window_secs = 60;
    spec->creation_time = time(NULL);
    spec->last_exit_code = -1;
    
    pthread_mutex_init(&spec->mutex, NULL);
    
    return spec;
}

/**
 * Free child specification
 */
static void supervision_child_spec_free(supervision_child_spec_t *spec) {
    if (!spec) return;
    
    free(spec->id);
    free(spec->command);
    pthread_mutex_destroy(&spec->mutex);
    free(spec);
}

/**
 * Create supervision tree
 */
supervision_tree_t* supervision_tree_create(const char *name,
                                           supervision_restart_strategy_t strategy) {
    if (!name) return NULL;
    
    supervision_tree_t *tree = malloc(sizeof(supervision_tree_t));
    if (!tree) return NULL;
    
    memset(tree, 0, sizeof(supervision_tree_t));
    
    tree->name = strdup(name);
    if (!tree->name) {
        free(tree);
        return NULL;
    }
    
    tree->restart_strategy = strategy;
    tree->max_restarts = 5;
    tree->max_restart_window_secs = 300;
    tree->health_check_interval_secs = 5;
    tree->state = SUPERVISION_STATE_STOPPED;
    tree->supervisor_pid = getpid();
    
    pthread_mutex_init(&tree->mutex, NULL);
    
    return tree;
}

/**
 * Free supervision tree
 */
void supervision_tree_free(supervision_tree_t *tree) {
    if (!tree) return;
    
    if (tree->running) {
        supervision_tree_stop(tree);
    }
    
    if (tree->children) {
        for (uint32_t i = 0; i < tree->child_count; i++) {
            if (tree->children[i]) {
                supervision_child_spec_free(tree->children[i]);
            }
        }
        free(tree->children);
    }
    
    free(tree->name);
    pthread_mutex_destroy(&tree->mutex);
    free(tree);
}

/**
 * Find child by ID
 */
static supervision_child_spec_t* supervision_tree_find_child(supervision_tree_t *tree,
                                                            const char *child_id) {
    for (uint32_t i = 0; i < tree->child_count; i++) {
        if (tree->children[i] && strcmp(tree->children[i]->id, child_id) == 0) {
            return tree->children[i];
        }
    }
    return NULL;
}

/**
 * Add child to supervision tree
 */
bool supervision_tree_add_child(supervision_tree_t *tree,
                               const char *child_id,
                               const char *command,
                               supervision_child_type_t type) {
    if (!tree || !child_id || !command) return false;
    
    pthread_mutex_lock(&tree->mutex);
    
    // Check if child already exists
    if (supervision_tree_find_child(tree, child_id) != NULL) {
        pthread_mutex_unlock(&tree->mutex);
        return false;
    }
    
    // Create new child spec
    supervision_child_spec_t *spec = supervision_child_spec_create(child_id, command);
    if (!spec) {
        pthread_mutex_unlock(&tree->mutex);
        return false;
    }
    
    spec->type = type;
    
    // Add to children array
    supervision_child_spec_t **new_children = realloc(tree->children,
                                                      sizeof(supervision_child_spec_t *) * (tree->child_count + 1));
    if (!new_children) {
        supervision_child_spec_free(spec);
        pthread_mutex_unlock(&tree->mutex);
        return false;
    }
    
    tree->children = new_children;
    tree->children[tree->child_count] = spec;
    tree->child_count++;
    
    pthread_mutex_unlock(&tree->mutex);
    return true;
}

/**
 * Set child restart policy
 */
bool supervision_tree_set_child_policy(supervision_tree_t *tree,
                                      const char *child_id,
                                      uint32_t max_restarts,
                                      uint32_t max_restart_window_secs) {
    if (!tree || !child_id) return false;
    
    pthread_mutex_lock(&tree->mutex);
    
    supervision_child_spec_t *spec = supervision_tree_find_child(tree, child_id);
    if (!spec) {
        pthread_mutex_unlock(&tree->mutex);
        return false;
    }
    
    pthread_mutex_lock(&spec->mutex);
    spec->max_restarts = max_restarts;
    spec->max_restart_window_secs = max_restart_window_secs;
    pthread_mutex_unlock(&spec->mutex);
    
    pthread_mutex_unlock(&tree->mutex);
    return true;
}

/**
 * Start child process
 */
static bool supervision_tree_start_child(supervision_tree_t *tree,
                                        supervision_child_spec_t *spec) {
    if (!spec || !spec->command) return false;
    
    // Fork and execute
    pid_t pid = fork();
    if (pid == -1) {
        return false;
    }
    
    if (pid == 0) {
        // Child process: execute command
        execl("/bin/sh", "sh", "-c", spec->command, (char *)NULL);
        exit(127);  // Failed to execute
    }
    
    // Parent process: record PID
    pthread_mutex_lock(&spec->mutex);
    spec->current_pid = pid;
    spec->running = true;
    pthread_mutex_unlock(&spec->mutex);
    
    return true;
}

/**
 * Stop child process
 */
static bool supervision_tree_stop_child(supervision_tree_t *tree,
                                       supervision_child_spec_t *spec) {
    if (!spec) return false;
    
    pthread_mutex_lock(&spec->mutex);
    
    if (spec->current_pid <= 0 || !spec->running) {
        pthread_mutex_unlock(&spec->mutex);
        return true;
    }
    
    pid_t pid = spec->current_pid;
    pthread_mutex_unlock(&spec->mutex);
    
    // Send SIGTERM
    kill(pid, SIGTERM);
    
    // Wait for graceful shutdown
    for (int i = 0; i < 5; i++) {
        sleep(1);
        if (kill(pid, 0) != 0) {
            // Process exited
            pthread_mutex_lock(&spec->mutex);
            spec->running = false;
            spec->current_pid = -1;
            pthread_mutex_unlock(&spec->mutex);
            return true;
        }
    }
    
    // Force kill
    kill(pid, SIGKILL);
    sleep(1);
    
    pthread_mutex_lock(&spec->mutex);
    spec->running = false;
    spec->current_pid = -1;
    pthread_mutex_unlock(&spec->mutex);
    
    return true;
}

/**
 * Monitor thread
 */
static void* supervision_tree_monitor_thread(void *arg) {
    supervision_tree_t *tree = (supervision_tree_t *)arg;
    
    while (tree->running) {
        pthread_mutex_lock(&tree->mutex);
        
        // Check each child
        for (uint32_t i = 0; i < tree->child_count; i++) {
            if (tree->children[i]) {
                supervision_child_spec_t *spec = tree->children[i];
                
                pthread_mutex_lock(&spec->mutex);
                if (spec->running && spec->current_pid > 0) {
                    // Check if process is still alive
                    int ret = waitpid(spec->current_pid, &spec->last_exit_code, WNOHANG);
                    
                    if (ret == spec->current_pid) {
                        // Process exited
                        spec->running = false;
                        
                        // Determine if restart is needed
                        bool should_restart = false;
                        
                        if (spec->type == SUPERVISION_CHILD_PERMANENT) {
                            // Always restart permanent children
                            should_restart = true;
                        } else if (spec->type == SUPERVISION_CHILD_TRANSIENT) {
                            // Restart on abnormal exit
                            should_restart = (WIFEXITED(spec->last_exit_code) && WEXITSTATUS(spec->last_exit_code) != 0) ||
                                           WIFSIGNALED(spec->last_exit_code);
                        }
                        
                        // Check restart limits
                        time_t now = time(NULL);
                        if (spec->last_restart_time > 0) {
                            time_t elapsed = now - spec->last_restart_time;
                            if (elapsed > spec->max_restart_window_secs) {
                                // Reset restart count
                                spec->restart_count = 0;
                            }
                        }
                        
                        if (should_restart && spec->restart_count < spec->max_restarts) {
                            // Wait before restart (exponential backoff)
                            uint32_t delay = 1;
                            for (uint32_t j = 0; j < spec->restart_count && delay < 32; j++) {
                                delay *= 2;
                            }
                            
                            spec->restart_count++;
                            spec->last_restart_time = now;
                            
                            pthread_mutex_unlock(&spec->mutex);
                            pthread_mutex_unlock(&tree->mutex);
                            
                            sleep(delay);
                            
                            // Restart the child
                            supervision_tree_start_child(tree, spec);
                            
                            pthread_mutex_lock(&tree->mutex);
                            break;  // Restart monitoring from beginning
                        } else if (!should_restart) {
                            // Child is transient and exited normally
                            spec->restart_count = 0;
                        }
                    }
                }
                pthread_mutex_unlock(&spec->mutex);
            }
        }
        
        pthread_mutex_unlock(&tree->mutex);
        
        // Sleep before next check
        sleep(tree->health_check_interval_secs);
    }
    
    return NULL;
}

/**
 * Start supervision tree
 */
bool supervision_tree_start(supervision_tree_t *tree) {
    if (!tree || tree->running) return false;
    
    pthread_mutex_lock(&tree->mutex);
    
    // Start all children
    for (uint32_t i = 0; i < tree->child_count; i++) {
        if (tree->children[i]) {
            if (!supervision_tree_start_child(tree, tree->children[i])) {
                pthread_mutex_unlock(&tree->mutex);
                return false;
            }
        }
    }
    
    tree->state = SUPERVISION_STATE_RUNNING;
    tree->running = true;
    
    pthread_mutex_unlock(&tree->mutex);
    
    // Start monitor thread
    int ret = pthread_create(&tree->monitor_thread, NULL,
                            supervision_tree_monitor_thread, tree);
    if (ret != 0) {
        pthread_mutex_lock(&tree->mutex);
        tree->running = false;
        tree->state = SUPERVISION_STATE_STOPPED;
        pthread_mutex_unlock(&tree->mutex);
        return false;
    }
    
    return true;
}

/**
 * Stop supervision tree
 */
bool supervision_tree_stop(supervision_tree_t *tree) {
    if (!tree) return false;
    
    pthread_mutex_lock(&tree->mutex);
    
    if (!tree->running) {
        pthread_mutex_unlock(&tree->mutex);
        return true;
    }
    
    tree->running = false;
    tree->state = SUPERVISION_STATE_STOPPING;
    
    // Stop all children
    for (uint32_t i = 0; i < tree->child_count; i++) {
        if (tree->children[i]) {
            supervision_tree_stop_child(tree, tree->children[i]);
        }
    }
    
    pthread_mutex_unlock(&tree->mutex);
    
    // Wait for monitor thread
    pthread_join(tree->monitor_thread, NULL);
    
    pthread_mutex_lock(&tree->mutex);
    tree->state = SUPERVISION_STATE_STOPPED;
    pthread_mutex_unlock(&tree->mutex);
    
    return true;
}

/**
 * Restart specific child
 */
bool supervision_tree_restart_child(supervision_tree_t *tree,
                                   const char *child_id) {
    if (!tree || !child_id) return false;
    
    pthread_mutex_lock(&tree->mutex);
    
    supervision_child_spec_t *spec = supervision_tree_find_child(tree, child_id);
    if (!spec) {
        pthread_mutex_unlock(&tree->mutex);
        return false;
    }
    
    // Stop the child
    supervision_tree_stop_child(tree, spec);
    
    pthread_mutex_unlock(&tree->mutex);
    
    // Give it a moment
    sleep(1);
    
    // Restart the child
    pthread_mutex_lock(&tree->mutex);
    bool result = supervision_tree_start_child(tree, spec);
    pthread_mutex_unlock(&tree->mutex);
    
    return result;
}

/**
 * Get child state
 */
bool supervision_tree_get_child_state(supervision_tree_t *tree,
                                     const char *child_id,
                                     pid_t *out_pid,
                                     bool *out_running,
                                     int *out_exit_code) {
    if (!tree || !child_id) return false;
    
    pthread_mutex_lock(&tree->mutex);
    
    supervision_child_spec_t *spec = supervision_tree_find_child(tree, child_id);
    if (!spec) {
        pthread_mutex_unlock(&tree->mutex);
        return false;
    }
    
    pthread_mutex_lock(&spec->mutex);
    
    if (out_pid) *out_pid = spec->current_pid;
    if (out_running) *out_running = spec->running;
    if (out_exit_code) *out_exit_code = spec->last_exit_code;
    
    pthread_mutex_unlock(&spec->mutex);
    pthread_mutex_unlock(&tree->mutex);
    
    return true;
}

/**
 * Get supervisor state
 */
supervision_state_t supervision_tree_get_state(supervision_tree_t *tree) {
    if (!tree) return SUPERVISION_STATE_STOPPED;
    
    pthread_mutex_lock(&tree->mutex);
    supervision_state_t state = tree->state;
    pthread_mutex_unlock(&tree->mutex);
    
    return state;
}

/**
 * Check if child is running
 */
bool supervision_tree_is_child_running(supervision_tree_t *tree,
                                      const char *child_id) {
    pid_t pid;
    bool running;
    
    if (!supervision_tree_get_child_state(tree, child_id, &pid, &running, NULL)) {
        return false;
    }
    
    return running;
}

/**
 * Get restart count in current window
 */
uint32_t supervision_tree_get_restart_count(supervision_tree_t *tree) {
    if (!tree) return 0;
    
    pthread_mutex_lock(&tree->mutex);
    
    // Calculate restart count in current window
    time_t now = time(NULL);
    uint32_t count = 0;
    
    for (uint32_t i = 0; i < tree->child_count; i++) {
        if (tree->children[i]) {
            pthread_mutex_lock(&tree->children[i]->mutex);
            
            if (tree->children[i]->last_restart_time > 0) {
                time_t elapsed = now - tree->children[i]->last_restart_time;
                if (elapsed <= tree->max_restart_window_secs) {
                    count += tree->children[i]->restart_count;
                }
            }
            
            pthread_mutex_unlock(&tree->children[i]->mutex);
        }
    }
    
    pthread_mutex_unlock(&tree->mutex);
    return count;
}

/**
 * Set health check interval
 */
bool supervision_tree_set_health_check_interval(supervision_tree_t *tree,
                                               uint32_t interval_secs) {
    if (!tree || interval_secs == 0) return false;
    
    pthread_mutex_lock(&tree->mutex);
    tree->health_check_interval_secs = interval_secs;
    pthread_mutex_unlock(&tree->mutex);
    
    return true;
}

/**
 * Set max restarts policy
 */
bool supervision_tree_set_max_restarts(supervision_tree_t *tree,
                                      uint32_t max_restarts,
                                      uint32_t window_secs) {
    if (!tree || max_restarts == 0 || window_secs == 0) return false;
    
    pthread_mutex_lock(&tree->mutex);
    tree->max_restarts = max_restarts;
    tree->max_restart_window_secs = window_secs;
    pthread_mutex_unlock(&tree->mutex);
    
    return true;
}
