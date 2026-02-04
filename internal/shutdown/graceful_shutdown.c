/*
 * graceful_shutdown.c
 * 
 * Implementation of graceful shutdown handler
 */

#include "graceful_shutdown.h"
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

// Global shutdown manager for signal handler
static shutdown_manager_t *g_shutdown_manager = NULL;
static pthread_mutex_t g_manager_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * Signal handler for SIGTERM/SIGINT
 */
static void signal_handler(int signum) {
    (void)signum;  // Unused
    
    pthread_mutex_lock(&g_manager_lock);
    if (g_shutdown_manager) {
        shutdown_manager_initiate(g_shutdown_manager);
    }
    pthread_mutex_unlock(&g_manager_lock);
}

/**
 * Create a new shutdown manager
 */
shutdown_manager_t* shutdown_manager_new(const shutdown_config_t *config) {
    if (!config) {
        return NULL;
    }
    
    shutdown_manager_t *manager = (shutdown_manager_t *)malloc(sizeof(shutdown_manager_t));
    if (!manager) {
        return NULL;
    }
    
    manager->config = *config;
    manager->state = SHUTDOWN_STATE_RUNNING;
    memset(&manager->stats, 0, sizeof(manager->stats));
    
    pthread_mutex_init(&manager->mutex, NULL);
    pthread_cond_init(&manager->all_requests_done, NULL);
    
    manager->handler_capacity = 10;
    manager->handler_count = 0;
    manager->handlers = (shutdown_handler_func_t *)malloc(
        sizeof(shutdown_handler_func_t) * manager->handler_capacity
    );
    manager->handler_userdata = (void **)malloc(
        sizeof(void *) * manager->handler_capacity
    );
    
    if (!manager->handlers || !manager->handler_userdata) {
        free(manager->handlers);
        free(manager->handler_userdata);
        free(manager);
        return NULL;
    }
    
    return manager;
}

/**
 * Free shutdown manager
 */
void shutdown_manager_free(shutdown_manager_t *manager) {
    if (!manager) {
        return;
    }
    
    pthread_mutex_lock(&g_manager_lock);
    if (g_shutdown_manager == manager) {
        g_shutdown_manager = NULL;
    }
    pthread_mutex_unlock(&g_manager_lock);
    
    pthread_cond_destroy(&manager->all_requests_done);
    pthread_mutex_destroy(&manager->mutex);
    free(manager->handlers);
    free(manager->handler_userdata);
    free(manager);
}

/**
 * Register a shutdown handler
 */
bool shutdown_manager_register_handler(shutdown_manager_t *manager,
                                       shutdown_handler_func_t handler,
                                       void *userdata) {
    if (!manager || !handler) {
        return false;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    // Expand array if needed
    if (manager->handler_count >= manager->handler_capacity) {
        uint32_t new_capacity = manager->handler_capacity * 2;
        
        shutdown_handler_func_t *new_handlers = (shutdown_handler_func_t *)realloc(
            manager->handlers,
            sizeof(shutdown_handler_func_t) * new_capacity
        );
        void **new_userdata = (void **)realloc(
            manager->handler_userdata,
            sizeof(void *) * new_capacity
        );
        
        if (!new_handlers || !new_userdata) {
            pthread_mutex_unlock(&manager->mutex);
            return false;
        }
        
        manager->handlers = new_handlers;
        manager->handler_userdata = new_userdata;
        manager->handler_capacity = new_capacity;
    }
    
    manager->handlers[manager->handler_count] = handler;
    manager->handler_userdata[manager->handler_count] = userdata;
    manager->handler_count++;
    
    pthread_mutex_unlock(&manager->mutex);
    
    return true;
}

/**
 * Setup signal handlers for graceful shutdown
 */
bool shutdown_manager_setup_signal_handlers(shutdown_manager_t *manager) {
    if (!manager) {
        return false;
    }
    
    pthread_mutex_lock(&g_manager_lock);
    g_shutdown_manager = manager;
    pthread_mutex_unlock(&g_manager_lock);
    
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        perror("sigaction(SIGTERM)");
        return false;
    }
    
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        perror("sigaction(SIGINT)");
        return false;
    }
    
    return true;
}

/**
 * Initiate graceful shutdown
 */
void shutdown_manager_initiate(shutdown_manager_t *manager) {
    if (!manager) {
        return;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    if (manager->state == SHUTDOWN_STATE_SHUTTING_DOWN) {
        pthread_mutex_unlock(&manager->mutex);
        return;  // Already shutting down
    }
    
    manager->state = SHUTDOWN_STATE_SHUTTING_DOWN;
    manager->stats.shutdown_start = time(NULL);
    
    printf("SHUTDOWN: Graceful shutdown initiated\n");
    
    pthread_mutex_unlock(&manager->mutex);
    
    // Call registered handlers in reverse order
    pthread_mutex_lock(&manager->mutex);
    uint32_t handler_count = manager->handler_count;
    pthread_mutex_unlock(&manager->mutex);
    
    for (int32_t i = handler_count - 1; i >= 0; i--) {
        printf("SHUTDOWN: Calling handler %d/%d\n", (int32_t)handler_count - i, (int32_t)handler_count);
        
        pthread_mutex_lock(&manager->mutex);
        shutdown_handler_func_t handler = manager->handlers[i];
        void *userdata = manager->handler_userdata[i];
        pthread_mutex_unlock(&manager->mutex);
        
        if (handler) {
            handler(userdata);
        }
    }
    
    printf("SHUTDOWN: All handlers called, waiting for requests to drain\n");
}

/**
 * Wait for shutdown to complete
 */
bool shutdown_manager_wait(shutdown_manager_t *manager) {
    if (!manager) {
        return false;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    time_t start_time = time(NULL);
    time_t grace_deadline = start_time + manager->config.grace_period_seconds;
    time_t force_deadline = start_time + manager->config.force_timeout_seconds;
    
    // Wait for requests to drain with timeout
    while (manager->stats.active_requests > 0) {
        time_t now = time(NULL);
        
        if (now >= force_deadline) {
            printf("SHUTDOWN: Force timeout reached, terminating\n");
            pthread_mutex_unlock(&manager->mutex);
            return false;
        }
        
        if (now >= grace_deadline && manager->stats.active_requests > 0) {
            printf("SHUTDOWN: Grace period expired, %u requests still active (forcing)\n",
                   manager->stats.active_requests);
            pthread_mutex_unlock(&manager->mutex);
            return false;
        }
        
        // Wait with timeout
        struct timespec ts;
        ts.tv_sec = now + 1;
        ts.tv_nsec = 0;
        
        int rc = pthread_cond_timedwait(&manager->all_requests_done, &manager->mutex, &ts);
        if (rc != 0 && rc != ETIMEDOUT) {
            printf("SHUTDOWN: Error waiting for requests\n");
            pthread_mutex_unlock(&manager->mutex);
            return false;
        }
    }
    
    printf("SHUTDOWN: All requests drained successfully\n");
    pthread_mutex_unlock(&manager->mutex);
    
    return true;
}

/**
 * Increment active request counter
 */
void shutdown_manager_request_start(shutdown_manager_t *manager) {
    if (!manager) {
        return;
    }
    
    pthread_mutex_lock(&manager->mutex);
    manager->stats.active_requests++;
    pthread_mutex_unlock(&manager->mutex);
}

/**
 * Decrement active request counter
 */
void shutdown_manager_request_end(shutdown_manager_t *manager) {
    if (!manager) {
        return;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    if (manager->stats.active_requests > 0) {
        manager->stats.active_requests--;
        manager->stats.total_drained++;
        
        if (manager->stats.active_requests == 0) {
            pthread_cond_signal(&manager->all_requests_done);
        }
    }
    
    pthread_mutex_unlock(&manager->mutex);
}

/**
 * Check if shutdown is in progress
 */
bool shutdown_manager_is_shutting_down(shutdown_manager_t *manager) {
    if (!manager) {
        return false;
    }
    
    pthread_mutex_lock(&manager->mutex);
    bool shutting_down = (manager->state == SHUTDOWN_STATE_SHUTTING_DOWN);
    pthread_mutex_unlock(&manager->mutex);
    
    return shutting_down;
}

/**
 * Get shutdown statistics
 */
void shutdown_manager_get_stats(shutdown_manager_t *manager, shutdown_stats_t *out_stats) {
    if (!manager || !out_stats) {
        return;
    }
    
    pthread_mutex_lock(&manager->mutex);
    *out_stats = manager->stats;
    pthread_mutex_unlock(&manager->mutex);
}
