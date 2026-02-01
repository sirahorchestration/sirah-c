#ifndef K8S_REQUEST_CACHE_H
#define K8S_REQUEST_CACHE_H

#include <stdbool.h>
#include <time.h>

// Cache entry
typedef struct {
    char* key;           // Request identifier
    char* value;         // Cached response data
    time_t created_at;
    time_t expires_at;
    int hits;            // Number of cache hits
    int size;            // Size of cached data in bytes
} k8s_cache_entry_t;

// Request cache
typedef struct {
    k8s_cache_entry_t** entries;
    int num_entries;
    int max_entries;
    int max_size_bytes;    // Max total cache size
    int current_size_bytes; // Current total size
    int ttl_seconds;       // Time to live for cache entries
    bool enabled;
} k8s_request_cache_t;

// ============ Cache Management ============

k8s_request_cache_t* k8s_request_cache_new(int max_entries, int max_size_bytes, int ttl_seconds);
void k8s_request_cache_free(k8s_request_cache_t* cache);

void k8s_request_cache_set_enabled(k8s_request_cache_t* cache, bool enabled);
bool k8s_request_cache_is_enabled(k8s_request_cache_t* cache);

// ============ Cache Operations ============

// Store a response in cache
int k8s_request_cache_put(k8s_request_cache_t* cache,
                          const char* key,
                          const char* value);

// Get cached response
int k8s_request_cache_get(k8s_request_cache_t* cache,
                          const char* key,
                          char** value);

// Check if key is cached
bool k8s_request_cache_contains(k8s_request_cache_t* cache, const char* key);

// Remove entry
int k8s_request_cache_remove(k8s_request_cache_t* cache, const char* key);

// Clear all entries
void k8s_request_cache_clear(k8s_request_cache_t* cache);

// ============ Cache Statistics ============

int k8s_request_cache_get_size(k8s_request_cache_t* cache);
int k8s_request_cache_get_entries(k8s_request_cache_t* cache);
int k8s_request_cache_get_hits(k8s_request_cache_t* cache);

// ============ Global Cache ============

k8s_request_cache_t* k8s_request_cache_global();

#endif
