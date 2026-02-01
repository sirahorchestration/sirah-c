#include "connection_pool.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

// Global connection pool
static k8s_connection_pool_t* g_connection_pool = NULL;

// ============ Initialization ============

k8s_connection_pool_t* k8s_connection_pool_new(int max_connections, int idle_timeout_seconds) {
    if (max_connections <= 0 || idle_timeout_seconds <= 0) return NULL;
    
    k8s_connection_pool_t* pool = calloc(1, sizeof(k8s_connection_pool_t));
    if (!pool) return NULL;
    
    pool->max_connections = max_connections;
    pool->idle_timeout_seconds = idle_timeout_seconds;
    pool->num_connections = 0;
    pool->enabled = true;
    
    return pool;
}

void k8s_connection_pool_free(k8s_connection_pool_t* pool) {
    if (!pool) return;
    
    for (int i = 0; i < pool->num_connections; i++) {
        k8s_connection_t* conn = pool->connections[i];
        if (conn) {
            free(conn->remote_ip);
            free(conn);
        }
    }
    free(pool->connections);
    free(pool);
}

void k8s_connection_pool_set_enabled(k8s_connection_pool_t* pool, bool enabled) {
    if (pool) {
        pool->enabled = enabled;
    }
}

bool k8s_connection_pool_is_enabled(k8s_connection_pool_t* pool) {
    if (!pool) return false;
    return pool->enabled;
}

// ============ Connection Operations ============

k8s_connection_t* k8s_connection_pool_acquire(k8s_connection_pool_t* pool,
                                               const char* remote_ip,
                                               int remote_port) {
    if (!pool || !remote_ip || remote_port <= 0) return NULL;
    if (!pool->enabled) return NULL;
    
    // Search for idle connection to same remote
    for (int i = 0; i < pool->num_connections; i++) {
        k8s_connection_t* conn = pool->connections[i];
        if (conn && !conn->in_use &&
            strcmp(conn->remote_ip, remote_ip) == 0 &&
            conn->remote_port == remote_port) {
            
            conn->in_use = true;
            conn->last_used_at = time(NULL);
            return conn;
        }
    }
    
    // Create new connection if under limit
    if (pool->num_connections < pool->max_connections) {
        k8s_connection_t* conn = calloc(1, sizeof(k8s_connection_t));
        if (!conn) return NULL;
        
        conn->remote_ip = malloc(strlen(remote_ip) + 1);
        if (!conn->remote_ip) {
            free(conn);
            return NULL;
        }
        
        strcpy(conn->remote_ip, remote_ip);
        conn->remote_port = remote_port;
        conn->in_use = true;
        conn->fd = -1;  // Placeholder for actual socket
        conn->created_at = time(NULL);
        conn->last_used_at = conn->created_at;
        
        k8s_connection_t** new_conns = realloc(pool->connections,
                                                (pool->num_connections + 1) * sizeof(k8s_connection_t*));
        if (!new_conns) {
            free(conn->remote_ip);
            free(conn);
            return NULL;
        }
        
        new_conns[pool->num_connections] = conn;
        pool->connections = new_conns;
        pool->num_connections++;
        
        return conn;
    }
    
    return NULL;  // Pool full, no idle connections
}

int k8s_connection_pool_release(k8s_connection_pool_t* pool, k8s_connection_t* conn) {
    if (!pool || !conn) return -1;
    
    // Find and mark as idle
    for (int i = 0; i < pool->num_connections; i++) {
        if (pool->connections[i] == conn) {
            conn->in_use = false;
            conn->last_used_at = time(NULL);
            return 0;
        }
    }
    
    return -1;  // Connection not in pool
}

int k8s_connection_pool_remove(k8s_connection_pool_t* pool, k8s_connection_t* conn) {
    if (!pool || !conn) return -1;
    
    for (int i = 0; i < pool->num_connections; i++) {
        if (pool->connections[i] == conn) {
            free(conn->remote_ip);
            free(conn);
            
            for (int j = i; j < pool->num_connections - 1; j++) {
                pool->connections[j] = pool->connections[j + 1];
            }
            pool->num_connections--;
            return 0;
        }
    }
    
    return -1;  // Not found
}

int k8s_connection_pool_cleanup_idle(k8s_connection_pool_t* pool) {
    if (!pool) return -1;
    
    time_t now = time(NULL);
    int removed = 0;
    
    for (int i = 0; i < pool->num_connections; i++) {
        k8s_connection_t* conn = pool->connections[i];
        if (conn && !conn->in_use &&
            (now - conn->last_used_at) > pool->idle_timeout_seconds) {
            
            free(conn->remote_ip);
            free(conn);
            
            for (int j = i; j < pool->num_connections - 1; j++) {
                pool->connections[j] = pool->connections[j + 1];
            }
            pool->num_connections--;
            i--;
            removed++;
        }
    }
    
    return removed;
}

// ============ Statistics ============

int k8s_connection_pool_get_active(k8s_connection_pool_t* pool) {
    if (!pool) return 0;
    
    int active = 0;
    for (int i = 0; i < pool->num_connections; i++) {
        if (pool->connections[i] && pool->connections[i]->in_use) {
            active++;
        }
    }
    return active;
}

int k8s_connection_pool_get_idle(k8s_connection_pool_t* pool) {
    if (!pool) return 0;
    
    int idle = 0;
    for (int i = 0; i < pool->num_connections; i++) {
        if (pool->connections[i] && !pool->connections[i]->in_use) {
            idle++;
        }
    }
    return idle;
}

// ============ Global Pool ============

k8s_connection_pool_t* k8s_connection_pool_global() {
    if (!g_connection_pool) {
        // 1000 max connections, 5 minute idle timeout
        g_connection_pool = k8s_connection_pool_new(1000, 300);
    }
    return g_connection_pool;
}
