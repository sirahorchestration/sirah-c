#ifndef ETCD_CLIENT_H
#define ETCD_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/**
 * etcd_client.h - etcd v3 HTTP API Client Library
 *
 * Provides C bindings to etcd v3.5+ key-value store with:
 * - Multi-endpoint failover (round-robin)
 * - Circuit breaker pattern (3-state FSM)
 * - Automatic retry on transient failures
 * - HTTP v3 API (PUT, GET, DELETE, LIST, RANGE, TXN)
 * - Resource versioning (etcd revision tracking)
 * - Optimistic locking (Compare-And-Swap semantics)
 *
 * Compatible with Kubernetes v1.28 etcd patterns.
 */

/* ============================================================================
 * Error Codes
 * ============================================================================ */

typedef enum {
    ETCD_OK = 0,
    ETCD_NOT_FOUND = 1,
    ETCD_CAS_FAILED = 2,           // Compare-and-swap version mismatch
    ETCD_NETWORK_ERROR = 3,
    ETCD_TIMEOUT = 4,
    ETCD_UNAVAILABLE = 5,
    ETCD_PERMISSION_DENIED = 6,
    ETCD_INVALID_ARGUMENT = 7,
    ETCD_OUT_OF_RANGE = 8,
    ETCD_INTERNAL = 9,
    ETCD_CLUSTER_UNAVAILABLE = 10,
    ETCD_CIRCUIT_BREAKER_OPEN = 11
} etcd_status_t;

/* ============================================================================
 * Circuit Breaker States
 * ============================================================================ */

typedef enum {
    ETCD_CB_CLOSED = 0,            // Normal operation
    ETCD_CB_OPEN = 1,              // Too many failures, rejecting requests
    ETCD_CB_HALF_OPEN = 2          // Testing if endpoint recovered
} etcd_circuit_breaker_state_t;

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/**
 * etcd_response_t - etcd API response container
 *
 * Holds the parsed response from etcd operations including value,
 * revision (for watch resumption and optimistic locking), and metadata.
 */
typedef struct {
    char *value;                    // GET/LIST response: key value (malloc'd)
    char *prev_value;               // Previous value for watch events
    uint64_t revision;              // etcd revision (opaque, use for CAS)
    uint64_t mod_revision;          // Object modification revision
    uint64_t create_revision;       // Object creation revision
    uint64_t version;               // Version counter for this key
    uint64_t lease_id;              // Lease ID if leased
    bool created;                   // True if key was just created
    int64_t ttl;                    // Remaining TTL in seconds (-1 if no lease)
    int kvs_count;                  // For RANGE: number of returned kvs
    char **kvs_keys;                // For RANGE: array of keys (malloc'd)
    char **kvs_values;              // For RANGE: array of values (malloc'd)
    int *kvs_versions;              // For RANGE: array of versions
} etcd_response_t;

/**
 * Circuit breaker per endpoint - tracks endpoint health
 */
typedef struct {
    etcd_circuit_breaker_state_t state;
    int failure_count;
    int success_count;
    time_t last_failure_time;
    time_t half_open_test_time;
    int failure_threshold;          // e.g., 5 consecutive failures
    int timeout_seconds;            // e.g., 30 seconds before half-open
} etcd_circuit_breaker_t;

/**
 * etcd_endpoint_t - Single etcd endpoint
 */
typedef struct {
    char *url;                      // "http://10.0.0.1:2379" or "https://..."
    etcd_circuit_breaker_t breaker;
    int connection_count;           // Active connections to this endpoint
    int total_requests;             // Lifetime requests to this endpoint
    int total_errors;               // Lifetime errors from this endpoint
} etcd_endpoint_t;

/**
 * etcd_client_t - etcd cluster client
 *
 * Manages connections to multiple etcd endpoints with automatic failover.
 * Thread-safe for concurrent operations.
 */
typedef struct etcd_client {
    etcd_endpoint_t *endpoints;     // Array of endpoints
    int endpoint_count;             // Number of endpoints
    int current_endpoint_index;     // Round-robin index
    
    int timeout_seconds;            // Request timeout
    int max_retries;                // Max retries on transient failure
    
    // Mutex for thread-safety
    void *mutex;                    // pthread_mutex_t* (opaque)
    
    // Statistics
    uint64_t total_requests;
    uint64_t total_errors;
    time_t last_error_time;
    char last_error_message[256];
} etcd_client_t;

/* ============================================================================
 * Client Lifecycle
 * ============================================================================ */

/**
 * etcd_client_create - Create new etcd client
 *
 * @param endpoints: Array of endpoint URLs (e.g., ["http://127.0.0.1:2379"])
 * @param endpoint_count: Number of endpoints
 * @return: Pointer to new etcd_client_t or NULL on error
 *
 * Example:
 *   const char *eps[] = {"http://127.0.0.1:2379", "http://127.0.0.1:2380"};
 *   etcd_client_t *client = etcd_client_create(eps, 2);
 */
etcd_client_t *etcd_client_create(const char **endpoints, int endpoint_count);

/**
 * etcd_client_set_timeout - Set request timeout
 *
 * @param client: Client pointer
 * @param timeout_seconds: Timeout in seconds (default 30)
 */
void etcd_client_set_timeout(etcd_client_t *client, int timeout_seconds);

/**
 * etcd_client_set_max_retries - Set max retries on transient failures
 *
 * @param client: Client pointer
 * @param max_retries: Number of retries (default 3)
 */
void etcd_client_set_max_retries(etcd_client_t *client, int max_retries);

/**
 * etcd_client_free - Free client and all resources
 *
 * @param client: Client pointer
 */
void etcd_client_free(etcd_client_t *client);

/* ============================================================================
 * Basic Operations (PUT, GET, DELETE)
 * ============================================================================ */

/**
 * etcd_put - PUT a key-value pair into etcd
 *
 * @param client: Client pointer
 * @param key: Key name (e.g., "/sirah/pods/default/my-pod")
 * @param value: JSON value to store
 * @param lease_id: Lease ID for TTL (0 for no lease)
 * @param response: Output response (caller allocates)
 * @return: Status code (ETCD_OK on success)
 *
 * Sets the revision in response->revision for use in PATCH/CAS operations.
 * Example:
 *   etcd_response_t resp = {0};
 *   status = etcd_put(client, "/sirah/pods/default/my-pod", "{...}", 0, &resp);
 *   if (status == ETCD_OK) {
 *       printf("Stored at revision %lu\n", resp.revision);
 *   }
 */
etcd_status_t etcd_put(etcd_client_t *client, const char *key, const char *value,
                       uint64_t lease_id, etcd_response_t *response);

/**
 * etcd_get - GET a single key from etcd
 *
 * @param client: Client pointer
 * @param key: Key name
 * @param response: Output response (caller allocates)
 * @return: Status code (ETCD_NOT_FOUND if key doesn't exist)
 *
 * Example:
 *   etcd_response_t resp = {0};
 *   status = etcd_get(client, "/sirah/pods/default/my-pod", &resp);
 *   if (status == ETCD_OK) {
 *       printf("Value: %s\n", resp.value);
 *       printf("Revision: %lu\n", resp.revision);
 *   }
 */
etcd_status_t etcd_get(etcd_client_t *client, const char *key,
                       etcd_response_t *response);

/**
 * etcd_delete - DELETE a key from etcd
 *
 * @param client: Client pointer
 * @param key: Key name
 * @param response: Output response (caller allocates)
 * @return: Status code
 *
 * Example:
 *   etcd_response_t resp = {0};
 *   status = etcd_delete(client, "/sirah/pods/default/my-pod", &resp);
 */
etcd_status_t etcd_delete(etcd_client_t *client, const char *key,
                          etcd_response_t *response);

/* ============================================================================
 * Range Operations (LIST with prefix matching)
 * ============================================================================ */

/**
 * etcd_list - GET all keys with given prefix (RANGE query)
 *
 * @param client: Client pointer
 * @param prefix: Key prefix (e.g., "/sirah/pods/default/")
 * @param response: Output response with arrays of keys/values
 * @return: Status code
 *
 * Sets response->kvs_keys, response->kvs_values, response->kvs_count.
 * Caller must free kvs_keys and kvs_values arrays.
 *
 * Example:
 *   etcd_response_t resp = {0};
 *   status = etcd_list(client, "/sirah/pods/default/", &resp);
 *   if (status == ETCD_OK) {
 *       for (int i = 0; i < resp.kvs_count; i++) {
 *           printf("Key: %s, Value: %s\n", resp.kvs_keys[i], resp.kvs_values[i]);
 *       }
 *   }
 */
etcd_status_t etcd_list(etcd_client_t *client, const char *prefix,
                        etcd_response_t *response);

/* ============================================================================
 * Optimistic Locking (Compare-And-Swap)
 * ============================================================================ */

/**
 * etcd_put_if_revision_matches - CAS (Compare-And-Swap) operation
 *
 * Only updates if current revision matches expected_revision.
 * Returns ETCD_CAS_FAILED if mismatch, ETCD_OK if successful.
 *
 * @param client: Client pointer
 * @param key: Key name
 * @param new_value: New JSON value
 * @param expected_revision: Expected current revision (from previous GET)
 * @param response: Output response with new revision
 * @return: ETCD_OK if successful, ETCD_CAS_FAILED if revision mismatch
 *
 * This is used for optimistic locking in PATCH operations:
 *   1. GET /api/v1/pods/default/my-pod (returns resourceVersion 123)
 *   2. PATCH with resourceVersion: 123
 *   3. If meanwhile someone else modified it (revision != 123), return 409
 *
 * Example:
 *   // First GET to get current revision
 *   etcd_response_t get_resp = {0};
 *   etcd_get(client, key, &get_resp);
 *   uint64_t current_revision = get_resp.revision;
 *
 *   // Later, try to update with CAS
 *   etcd_response_t put_resp = {0};
 *   status = etcd_put_if_revision_matches(client, key, new_json,
 *                                         current_revision, &put_resp);
 *   if (status == ETCD_CAS_FAILED) {
 *       // Conflict! Someone else modified it. Return 409 Conflict to client
 *   } else if (status == ETCD_OK) {
 *       // Success! New revision is in put_resp.revision
 *   }
 */
etcd_status_t etcd_put_if_revision_matches(etcd_client_t *client, const char *key,
                                           const char *new_value,
                                           uint64_t expected_revision,
                                           etcd_response_t *response);

/* ============================================================================
 * Health & Status
 * ============================================================================ */

/**
 * etcd_is_healthy - Check if at least one endpoint is reachable
 *
 * @param client: Client pointer
 * @return: true if at least one endpoint is healthy
 */
bool etcd_is_healthy(etcd_client_t *client);

/**
 * etcd_get_leader - Get current etcd cluster leader
 *
 * @param client: Client pointer
 * @param leader_id: Output leader ID (malloc'd, caller must free)
 * @return: Status code
 */
etcd_status_t etcd_get_leader(etcd_client_t *client, char **leader_id);

/**
 * etcd_get_stats - Get etcd cluster statistics
 *
 * @param client: Client pointer
 * @param stats_json: Output JSON with stats (malloc'd, caller must free)
 * @return: Status code
 */
etcd_status_t etcd_get_stats(etcd_client_t *client, char **stats_json);

/**
 * etcd_get_member_list - Get etcd cluster members
 *
 * @param client: Client pointer
 * @param members_json: Output JSON array of members (malloc'd)
 * @return: Status code
 */
etcd_status_t etcd_get_member_list(etcd_client_t *client, char **members_json);

/* ============================================================================
 * Leases (TTL support)
 * ============================================================================ */

/**
 * etcd_lease_grant - Grant a lease (TTL)
 *
 * @param client: Client pointer
 * @param ttl_seconds: Time-to-live in seconds
 * @param lease_id: Output lease ID
 * @return: Status code
 *
 * Example:
 *   uint64_t lease_id;
 *   etcd_lease_grant(client, 30, &lease_id);  // 30-second lease
 *   etcd_put(client, "/sirah/leases/my-lease", "{...}", lease_id, &resp);
 */
etcd_status_t etcd_lease_grant(etcd_client_t *client, int ttl_seconds,
                               uint64_t *lease_id);

/**
 * etcd_lease_revoke - Revoke a lease
 *
 * @param client: Client pointer
 * @param lease_id: Lease to revoke
 * @return: Status code
 */
etcd_status_t etcd_lease_revoke(etcd_client_t *client, uint64_t lease_id);

/**
 * etcd_lease_keep_alive - Keep a lease alive (refresh TTL)
 *
 * @param client: Client pointer
 * @param lease_id: Lease to keep alive
 * @return: Status code
 */
etcd_status_t etcd_lease_keep_alive(etcd_client_t *client, uint64_t lease_id);

/* ============================================================================
 * Response Cleanup
 * ============================================================================ */

/**
 * etcd_response_free - Free allocated fields in response
 *
 * Must be called after each operation to avoid leaks.
 *
 * @param response: Response to free
 *
 * Example:
 *   etcd_response_t resp = {0};
 *   etcd_get(client, key, &resp);
 *   printf("Value: %s\n", resp.value);
 *   etcd_response_free(&resp);  // Must free!
 */
void etcd_response_free(etcd_response_t *response);

/* ============================================================================
 * Debugging & Statistics
 * ============================================================================ */

/**
 * etcd_client_get_stats - Get client-level statistics
 *
 * @param client: Client pointer
 * @return: Pointer to stats (do not modify)
 */
typedef struct {
    uint64_t total_requests;
    uint64_t total_errors;
    int healthy_endpoints;
    int total_endpoints;
    const char *last_error;
} etcd_client_stats_t;

etcd_client_stats_t *etcd_client_get_stats(etcd_client_t *client);

/**
 * etcd_strerror - Convert error code to human-readable string
 *
 * @param status: Status code
 * @return: Error string (static, do not free)
 */
const char *etcd_strerror(etcd_status_t status);

#endif // ETCD_CLIENT_H
