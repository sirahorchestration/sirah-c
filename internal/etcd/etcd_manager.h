#ifndef ETCD_MANAGER_H
#define ETCD_MANAGER_H

#include "etcd_client.h"

/**
 * etcd_manager.h - etcd Connection Pool Manager
 *
 * Provides a singleton manager for etcd connections with:
 * - Global client instance for thread-safe access
 * - Connection health monitoring
 * - Automatic reconnection on failure
 * - Per-operation retry logic
 * - Statistics and metrics tracking
 *
 * Usage Pattern:
 *   etcd_manager_init("http://127.0.0.1:2379", "http://127.0.0.1:2380", ...);
 *   etcd_manager_put("/sirah/pods/my-pod", json_pod);
 *   etcd_manager_get("/sirah/pods/my-pod", &response);
 *   etcd_manager_shutdown();
 */

/* ============================================================================
 * Manager Initialization & Shutdown
 * ============================================================================ */

/**
 * etcd_manager_init - Initialize singleton etcd manager
 *
 * Must be called once at application startup before any operations.
 * Subsequent calls return existing instance.
 *
 * @param endpoint: Primary etcd endpoint (e.g., "http://127.0.0.1:2379")
 * @param ...: Additional endpoints (optional), terminated with NULL
 * @return: 0 on success, -1 on error
 *
 * Example:
 *   etcd_manager_init("http://127.0.0.1:2379",
 *                     "http://127.0.0.1:2380",
 *                     "http://127.0.0.1:2381",
 *                     NULL);
 */
int etcd_manager_init(const char *endpoint, ...);

/**
 * etcd_manager_shutdown - Shutdown singleton manager
 *
 * Closes all connections and frees resources.
 * Safe to call multiple times.
 */
void etcd_manager_shutdown(void);

/**
 * etcd_manager_is_initialized - Check if manager is initialized
 *
 * @return: true if manager is ready for operations
 */
bool etcd_manager_is_initialized(void);

/**
 * etcd_manager_wait_for_ready - Block until etcd is healthy
 *
 * Polls for etcd availability with exponential backoff.
 * Useful during startup to ensure cluster is accessible.
 *
 * @param timeout_seconds: Max time to wait (0 = infinite)
 * @return: 0 if ready, -1 if timeout
 *
 * Example:
 *   etcd_manager_init("http://127.0.0.1:2379", NULL);
 *   if (etcd_manager_wait_for_ready(60) < 0) {
 *       printf("etcd still unavailable after 60 seconds\n");
 *   }
 */
int etcd_manager_wait_for_ready(int timeout_seconds);

/* ============================================================================
 * Key-Value Operations (Thin Wrappers Around etcd_client)
 * ============================================================================ */

/**
 * etcd_manager_put - Store key-value pair
 *
 * @param key: Key name (e.g., "/sirah/pods/default/my-pod")
 * @param value: JSON value to store
 * @param response: Output response (caller allocates, must free with etcd_response_free)
 * @return: Status code
 *
 * Example:
 *   etcd_response_t resp = {0};
 *   status = etcd_manager_put("/sirah/pods/default/my-pod", pod_json, &resp);
 *   if (status == ETCD_OK) {
 *       printf("Revision: %lu\n", resp.revision);
 *   }
 *   etcd_response_free(&resp);
 */
etcd_status_t etcd_manager_put(const char *key, const char *value, etcd_response_t *response);

/**
 * etcd_manager_get - Retrieve single key
 *
 * @param key: Key name
 * @param response: Output response (caller must free)
 * @return: Status code (ETCD_NOT_FOUND if key doesn't exist)
 *
 * Example:
 *   etcd_response_t resp = {0};
 *   status = etcd_manager_get("/sirah/pods/default/my-pod", &resp);
 *   if (status == ETCD_OK) {
 *       printf("Pod JSON: %s\n", resp.value);
 *   } else if (status == ETCD_NOT_FOUND) {
 *       printf("Pod not found\n");
 *   }
 *   etcd_response_free(&resp);
 */
etcd_status_t etcd_manager_get(const char *key, etcd_response_t *response);

/**
 * etcd_manager_delete - Delete key
 *
 * @param key: Key name
 * @param response: Output response (caller must free)
 * @return: Status code
 *
 * Example:
 *   etcd_response_t resp = {0};
 *   etcd_manager_delete("/sirah/pods/default/my-pod", &resp);
 */
etcd_status_t etcd_manager_delete(const char *key, etcd_response_t *response);

/**
 * etcd_manager_list - List keys with prefix
 *
 * @param prefix: Prefix to search (e.g., "/sirah/pods/default/")
 * @param response: Output response with arrays
 * @return: Status code
 *
 * Response contains:
 *   - kvs_keys: Array of matching key names
 *   - kvs_values: Array of corresponding values
 *   - kvs_count: Number of results
 *
 * Example:
 *   etcd_response_t resp = {0};
 *   status = etcd_manager_list("/sirah/pods/default/", &resp);
 *   if (status == ETCD_OK) {
 *       for (int i = 0; i < resp.kvs_count; i++) {
 *           printf("%s: %s\n", resp.kvs_keys[i], resp.kvs_values[i]);
 *       }
 *   }
 *   etcd_response_free(&resp);
 */
etcd_status_t etcd_manager_list(const char *prefix, etcd_response_t *response);

/* ============================================================================
 * Optimistic Locking (PATCH semantics)
 * ============================================================================ */

/**
 * etcd_manager_patch - Update with version checking (Kubernetes PATCH semantics)
 *
 * Implements optimistic locking for concurrent updates.
 * Returns 409 Conflict (ETCD_CAS_FAILED) if current revision doesn't match expected.
 *
 * Used by PATCH handlers to prevent lost updates:
 *   1. GET returns pod with resourceVersion = 123
 *   2. PATCH with body: {"metadata": {"resourceVersion": "123"}, ...}
 *   3. If pod already modified, returns 409 Conflict
 *   4. Client retries with current version
 *
 * @param key: Key to update
 * @param new_value: New JSON value
 * @param expected_revision: Expected current revision (from GET)
 * @param response: Output response with new revision
 * @return: ETCD_OK on success, ETCD_CAS_FAILED on conflict, other errors
 *
 * Example:
 *   // Step 1: GET to get current revision
 *   etcd_response_t get_resp = {0};
 *   etcd_manager_get(key, &get_resp);
 *   uint64_t current_rev = get_resp.revision;
 *   etcd_response_free(&get_resp);
 *
 *   // Step 2: PATCH with version check
 *   etcd_response_t patch_resp = {0};
 *   status = etcd_manager_patch(key, new_json, current_rev, &patch_resp);
 *   
 *   if (status == ETCD_CAS_FAILED) {
 *       // Someone else modified it - return 409 Conflict to client
 *   } else if (status == ETCD_OK) {
 *       // Success - return updated resource
 *   }
 *   etcd_response_free(&patch_resp);
 */
etcd_status_t etcd_manager_patch(const char *key, const char *new_value,
                                 uint64_t expected_revision, etcd_response_t *response);

/* ============================================================================
 * Health & Monitoring
 * ============================================================================ */

/**
 * etcd_manager_is_healthy - Quick health check
 *
 * @return: true if at least one endpoint is reachable
 */
bool etcd_manager_is_healthy(void);

/**
 * etcd_manager_get_health_status - Detailed health status
 *
 * Returns human-readable health information.
 *
 * @return: Status string (static, do not free)
 *
 * Example output:
 *   "3 of 3 endpoints healthy (9/9 successful operations)"
 */
const char *etcd_manager_get_health_status(void);

/* ============================================================================
 * Statistics & Debugging
 * ============================================================================ */

/**
 * etcd_manager_stats_t - Statistics container
 */
typedef struct {
    uint64_t total_puts;            // Total PUT operations
    uint64_t total_gets;            // Total GET operations
    uint64_t total_deletes;         // Total DELETE operations
    uint64_t total_lists;           // Total LIST operations
    uint64_t total_patches;         // Total PATCH operations
    uint64_t total_errors;          // Total operation failures
    int healthy_endpoints;          // Currently healthy endpoints
    int total_endpoints;            // Total configured endpoints
    uint64_t last_error_time;       // Timestamp of last error
    char last_error_message[256];   // Last error description
} etcd_manager_stats_t;

/**
 * etcd_manager_get_stats - Get operation statistics
 *
 * @param stats: Output stats structure
 * @return: 0 on success
 *
 * Example:
 *   etcd_manager_stats_t stats = {0};
 *   etcd_manager_get_stats(&stats);
 *   printf("Total requests: %lu\n", stats.total_puts + stats.total_gets);
 *   printf("Error rate: %.2f%%\n", 100.0 * stats.total_errors / total);
 */
int etcd_manager_get_stats(etcd_manager_stats_t *stats);

/**
 * etcd_manager_reset_stats - Reset statistics
 *
 * Useful for testing or periodic metric rollover.
 */
void etcd_manager_reset_stats(void);

/* ============================================================================
 * Configuration
 * ============================================================================ */

/**
 * etcd_manager_set_timeout - Configure request timeout
 *
 * @param timeout_seconds: Timeout in seconds
 */
void etcd_manager_set_timeout(int timeout_seconds);

/**
 * etcd_manager_set_max_retries - Configure max retries
 *
 * @param max_retries: Number of retries on transient failure
 */
void etcd_manager_set_max_retries(int max_retries);

/**
 * etcd_manager_set_health_check_interval - Configure health check frequency
 *
 * @param interval_seconds: Seconds between health checks (0 = disable)
 */
void etcd_manager_set_health_check_interval(int interval_seconds);

/* ============================================================================
 * Internal Utilities (For Testing)
 * ============================================================================ */

/**
 * etcd_manager_get_client - Access underlying etcd_client (for advanced use)
 *
 * Warning: Use with caution - bypasses manager logic.
 *
 * @return: Pointer to global etcd_client_t, or NULL if not initialized
 */
etcd_client_t *etcd_manager_get_client(void);

/**
 * etcd_manager_trigger_health_check - Manually trigger health check
 *
 * Useful for testing or monitoring.
 *
 * @return: true if cluster is healthy
 */
bool etcd_manager_trigger_health_check(void);

#endif // ETCD_MANAGER_H
