#include "etcd_manager.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>

/**
 * etcd_manager.c - etcd Connection Pool Manager Implementation
 *
 * Singleton manager for etcd connections with health monitoring
 * and automatic retry logic.
 */

/* ============================================================================
 * Singleton Instance
 * ============================================================================ */

static etcd_client_t *g_etcd_client = NULL;
static pthread_mutex_t g_manager_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_initialized = false;

/* Statistics */
static struct {
    uint64_t total_puts;
    uint64_t total_gets;
    uint64_t total_deletes;
    uint64_t total_lists;
    uint64_t total_patches;
    uint64_t total_errors;
    time_t last_error_time;
    char last_error_message[256];
} g_stats = {0};

/* Health monitoring */
static pthread_t g_health_check_thread;
static bool g_health_check_running = false;
static int g_health_check_interval = 10;  // seconds

/* ============================================================================
 * Health Check Thread
 * ============================================================================ */

static void *etcd_manager_health_check_loop(void *arg) {
    while (g_health_check_running && g_etcd_client) {
        sleep(g_health_check_interval);

        if (!etcd_is_healthy(g_etcd_client)) {
            pthread_mutex_lock(&g_manager_mutex);
            strncpy(g_stats.last_error_message, "etcd cluster unavailable",
                    sizeof(g_stats.last_error_message) - 1);
            g_stats.last_error_time = time(NULL);
            pthread_mutex_unlock(&g_manager_mutex);
        }
    }
    return NULL;
}

/* ============================================================================
 * Manager Lifecycle
 * ============================================================================ */

int etcd_manager_init(const char *endpoint, ...) {
    if (!endpoint) {
        fprintf(stderr, "etcd_manager: Invalid endpoint\n");
        return -1;
    }

    pthread_mutex_lock(&g_manager_mutex);

    if (g_initialized) {
        pthread_mutex_unlock(&g_manager_mutex);
        return 0;  // Already initialized
    }

    // Collect all endpoints
    const char *endpoints[16];
    int endpoint_count = 0;
    endpoints[endpoint_count++] = endpoint;

    va_list args;
    va_start(args, endpoint);
    const char *ep;
    while ((ep = va_arg(args, const char *)) != NULL && endpoint_count < 15) {
        endpoints[endpoint_count++] = ep;
    }
    va_end(args);

    // Create client
    g_etcd_client = etcd_client_create(endpoints, endpoint_count);
    if (!g_etcd_client) {
        fprintf(stderr, "etcd_manager: Failed to create client\n");
        pthread_mutex_unlock(&g_manager_mutex);
        return -1;
    }

    etcd_client_set_timeout(g_etcd_client, 30);
    etcd_client_set_max_retries(g_etcd_client, 3);

    g_initialized = true;

    // Start health check thread
    g_health_check_running = true;
    if (pthread_create(&g_health_check_thread, NULL, 
                       etcd_manager_health_check_loop, NULL) != 0) {
        fprintf(stderr, "etcd_manager: Failed to start health check thread\n");
        g_health_check_running = false;
    }

    pthread_mutex_unlock(&g_manager_mutex);
    return 0;
}

void etcd_manager_shutdown(void) {
    pthread_mutex_lock(&g_manager_mutex);

    if (!g_initialized) {
        pthread_mutex_unlock(&g_manager_mutex);
        return;
    }

    // Stop health check thread
    g_health_check_running = false;
    if (g_health_check_thread) {
        pthread_mutex_unlock(&g_manager_mutex);
        pthread_join(g_health_check_thread, NULL);
        pthread_mutex_lock(&g_manager_mutex);
    }

    // Free client
    if (g_etcd_client) {
        etcd_client_free(g_etcd_client);
        g_etcd_client = NULL;
    }

    g_initialized = false;

    pthread_mutex_unlock(&g_manager_mutex);
}

bool etcd_manager_is_initialized(void) {
    bool result;
    pthread_mutex_lock(&g_manager_mutex);
    result = g_initialized;
    pthread_mutex_unlock(&g_manager_mutex);
    return result;
}

int etcd_manager_wait_for_ready(int timeout_seconds) {
    if (!g_initialized) {
        return -1;
    }

    time_t start = time(NULL);
    int backoff_ms = 100;
    const int max_backoff_ms = 5000;

    while (1) {
        if (etcd_is_healthy(g_etcd_client)) {
            return 0;
        }

        if (timeout_seconds > 0) {
            time_t elapsed = time(NULL) - start;
            if (elapsed > timeout_seconds) {
                return -1;
            }
        }

        usleep(backoff_ms * 1000);
        backoff_ms = (backoff_ms * 2 < max_backoff_ms) ? backoff_ms * 2 : max_backoff_ms;
    }
}

/* ============================================================================
 * Key-Value Operations
 * ============================================================================ */

etcd_status_t etcd_manager_put(const char *key, const char *value, 
                               etcd_response_t *response) {
    pthread_mutex_lock(&g_manager_mutex);
    if (!g_initialized) {
        pthread_mutex_unlock(&g_manager_mutex);
        return ETCD_UNAVAILABLE;
    }

    g_stats.total_puts++;
    pthread_mutex_unlock(&g_manager_mutex);

    etcd_status_t status = etcd_put(g_etcd_client, key, value, 0, response);

    if (status != ETCD_OK) {
        pthread_mutex_lock(&g_manager_mutex);
        g_stats.total_errors++;
        g_stats.last_error_time = time(NULL);
        snprintf(g_stats.last_error_message, sizeof(g_stats.last_error_message),
                 "PUT failed: %s", etcd_strerror(status));
        pthread_mutex_unlock(&g_manager_mutex);
    }

    return status;
}

etcd_status_t etcd_manager_get(const char *key, etcd_response_t *response) {
    pthread_mutex_lock(&g_manager_mutex);
    if (!g_initialized) {
        pthread_mutex_unlock(&g_manager_mutex);
        return ETCD_UNAVAILABLE;
    }

    g_stats.total_gets++;
    pthread_mutex_unlock(&g_manager_mutex);

    etcd_status_t status = etcd_get(g_etcd_client, key, response);

    if (status != ETCD_OK && status != ETCD_NOT_FOUND) {
        pthread_mutex_lock(&g_manager_mutex);
        g_stats.total_errors++;
        g_stats.last_error_time = time(NULL);
        snprintf(g_stats.last_error_message, sizeof(g_stats.last_error_message),
                 "GET failed: %s", etcd_strerror(status));
        pthread_mutex_unlock(&g_manager_mutex);
    }

    return status;
}

etcd_status_t etcd_manager_delete(const char *key, etcd_response_t *response) {
    pthread_mutex_lock(&g_manager_mutex);
    if (!g_initialized) {
        pthread_mutex_unlock(&g_manager_mutex);
        return ETCD_UNAVAILABLE;
    }

    g_stats.total_deletes++;
    pthread_mutex_unlock(&g_manager_mutex);

    etcd_status_t status = etcd_delete(g_etcd_client, key, response);

    if (status != ETCD_OK) {
        pthread_mutex_lock(&g_manager_mutex);
        g_stats.total_errors++;
        g_stats.last_error_time = time(NULL);
        snprintf(g_stats.last_error_message, sizeof(g_stats.last_error_message),
                 "DELETE failed: %s", etcd_strerror(status));
        pthread_mutex_unlock(&g_manager_mutex);
    }

    return status;
}

etcd_status_t etcd_manager_list(const char *prefix, etcd_response_t *response) {
    pthread_mutex_lock(&g_manager_mutex);
    if (!g_initialized) {
        pthread_mutex_unlock(&g_manager_mutex);
        return ETCD_UNAVAILABLE;
    }

    g_stats.total_lists++;
    pthread_mutex_unlock(&g_manager_mutex);

    etcd_status_t status = etcd_list(g_etcd_client, prefix, response);

    if (status != ETCD_OK) {
        pthread_mutex_lock(&g_manager_mutex);
        g_stats.total_errors++;
        g_stats.last_error_time = time(NULL);
        snprintf(g_stats.last_error_message, sizeof(g_stats.last_error_message),
                 "LIST failed: %s", etcd_strerror(status));
        pthread_mutex_unlock(&g_manager_mutex);
    }

    return status;
}

/* ============================================================================
 * Optimistic Locking
 * ============================================================================ */

etcd_status_t etcd_manager_patch(const char *key, const char *new_value,
                                 uint64_t expected_revision, etcd_response_t *response) {
    pthread_mutex_lock(&g_manager_mutex);
    if (!g_initialized) {
        pthread_mutex_unlock(&g_manager_mutex);
        return ETCD_UNAVAILABLE;
    }

    g_stats.total_patches++;
    pthread_mutex_unlock(&g_manager_mutex);

    etcd_status_t status = etcd_put_if_revision_matches(g_etcd_client, key, new_value,
                                                        expected_revision, response);

    if (status != ETCD_OK && status != ETCD_CAS_FAILED) {
        pthread_mutex_lock(&g_manager_mutex);
        g_stats.total_errors++;
        g_stats.last_error_time = time(NULL);
        snprintf(g_stats.last_error_message, sizeof(g_stats.last_error_message),
                 "PATCH failed: %s", etcd_strerror(status));
        pthread_mutex_unlock(&g_manager_mutex);
    }

    return status;
}

/* ============================================================================
 * Health & Monitoring
 * ============================================================================ */

bool etcd_manager_is_healthy(void) {
    pthread_mutex_lock(&g_manager_mutex);
    if (!g_initialized) {
        pthread_mutex_unlock(&g_manager_mutex);
        return false;
    }

    bool result = etcd_is_healthy(g_etcd_client);
    pthread_mutex_unlock(&g_manager_mutex);
    return result;
}

const char *etcd_manager_get_health_status(void) {
    static char status_buf[256];

    pthread_mutex_lock(&g_manager_mutex);
    if (!g_initialized) {
        pthread_mutex_unlock(&g_manager_mutex);
        return "etcd manager not initialized";
    }

    etcd_client_stats_t *stats = etcd_client_get_stats(g_etcd_client);
    
    snprintf(status_buf, sizeof(status_buf),
             "%d of %d endpoints healthy (%lu successful operations, %lu errors)",
             stats->healthy_endpoints, stats->total_endpoints,
             stats->total_requests - stats->total_errors, stats->total_errors);

    pthread_mutex_unlock(&g_manager_mutex);
    return status_buf;
}

/* ============================================================================
 * Statistics
 * ============================================================================ */

int etcd_manager_get_stats(etcd_manager_stats_t *stats) {
    if (!stats) {
        return -1;
    }

    pthread_mutex_lock(&g_manager_mutex);

    stats->total_puts = g_stats.total_puts;
    stats->total_gets = g_stats.total_gets;
    stats->total_deletes = g_stats.total_deletes;
    stats->total_lists = g_stats.total_lists;
    stats->total_patches = g_stats.total_patches;
    stats->total_errors = g_stats.total_errors;
    stats->last_error_time = g_stats.last_error_time;
    strncpy(stats->last_error_message, g_stats.last_error_message,
            sizeof(stats->last_error_message) - 1);

    if (g_initialized && g_etcd_client) {
        etcd_client_stats_t *client_stats = etcd_client_get_stats(g_etcd_client);
        stats->healthy_endpoints = client_stats->healthy_endpoints;
        stats->total_endpoints = client_stats->total_endpoints;
    } else {
        stats->healthy_endpoints = 0;
        stats->total_endpoints = 0;
    }

    pthread_mutex_unlock(&g_manager_mutex);
    return 0;
}

void etcd_manager_reset_stats(void) {
    pthread_mutex_lock(&g_manager_mutex);
    memset(&g_stats, 0, sizeof(g_stats));
    pthread_mutex_unlock(&g_manager_mutex);
}

/* ============================================================================
 * Configuration
 * ============================================================================ */

void etcd_manager_set_timeout(int timeout_seconds) {
    pthread_mutex_lock(&g_manager_mutex);
    if (g_initialized && g_etcd_client) {
        etcd_client_set_timeout(g_etcd_client, timeout_seconds);
    }
    pthread_mutex_unlock(&g_manager_mutex);
}

void etcd_manager_set_max_retries(int max_retries) {
    pthread_mutex_lock(&g_manager_mutex);
    if (g_initialized && g_etcd_client) {
        etcd_client_set_max_retries(g_etcd_client, max_retries);
    }
    pthread_mutex_unlock(&g_manager_mutex);
}

void etcd_manager_set_health_check_interval(int interval_seconds) {
    pthread_mutex_lock(&g_manager_mutex);
    g_health_check_interval = interval_seconds;
    pthread_mutex_unlock(&g_manager_mutex);
}

/* ============================================================================
 * Internal Utilities
 * ============================================================================ */

etcd_client_t *etcd_manager_get_client(void) {
    pthread_mutex_lock(&g_manager_mutex);
    etcd_client_t *client = g_etcd_client;
    pthread_mutex_unlock(&g_manager_mutex);
    return client;
}

bool etcd_manager_trigger_health_check(void) {
    pthread_mutex_lock(&g_manager_mutex);
    if (!g_initialized) {
        pthread_mutex_unlock(&g_manager_mutex);
        return false;
    }

    bool healthy = etcd_is_healthy(g_etcd_client);
    if (!healthy) {
        g_stats.last_error_time = time(NULL);
        strncpy(g_stats.last_error_message, "etcd cluster unavailable",
                sizeof(g_stats.last_error_message) - 1);
    }

    pthread_mutex_unlock(&g_manager_mutex);
    return healthy;
}
