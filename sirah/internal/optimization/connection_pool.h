#ifndef K8S_CONNECTION_POOL_H
#define K8S_CONNECTION_POOL_H

#include <stdbool.h>

// Connection wrapper
typedef struct {
    int fd;           // File descriptor
    char* remote_ip;
    int remote_port;
    bool in_use;
    long created_at;
    long last_used_at;
} k8s_connection_t;

// Connection pool
typedef struct {
    k8s_connection_t** connections;
    int num_connections;
    int max_connections;
    int idle_timeout_seconds;
    bool enabled;
} k8s_connection_pool_t;

// ============ Pool Management ============

k8s_connection_pool_t* k8s_connection_pool_new(int max_connections, int idle_timeout_seconds);
void k8s_connection_pool_free(k8s_connection_pool_t* pool);

void k8s_connection_pool_set_enabled(k8s_connection_pool_t* pool, bool enabled);
bool k8s_connection_pool_is_enabled(k8s_connection_pool_t* pool);

// ============ Connection Operations ============

// Get or create connection
k8s_connection_t* k8s_connection_pool_acquire(k8s_connection_pool_t* pool,
                                               const char* remote_ip,
                                               int remote_port);

// Return connection to pool
int k8s_connection_pool_release(k8s_connection_pool_t* pool, k8s_connection_t* conn);

// Close and remove connection
int k8s_connection_pool_remove(k8s_connection_pool_t* pool, k8s_connection_t* conn);

// Close idle connections
int k8s_connection_pool_cleanup_idle(k8s_connection_pool_t* pool);

// ============ Statistics ============

int k8s_connection_pool_get_active(k8s_connection_pool_t* pool);
int k8s_connection_pool_get_idle(k8s_connection_pool_t* pool);

// ============ Global Pool ============

k8s_connection_pool_t* k8s_connection_pool_global();

#endif
