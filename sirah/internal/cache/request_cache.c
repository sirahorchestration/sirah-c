#include "request_cache.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

// Global cache instance
static k8s_request_cache_t* g_request_cache = NULL;

// ============ Initialization ============

k8s_request_cache_t* k8s_request_cache_new(int max_entries, int max_size_bytes, int ttl_seconds) {
    if (max_entries <= 0 || max_size_bytes <= 0 || ttl_seconds <= 0) return NULL;
    
    k8s_request_cache_t* cache = calloc(1, sizeof(k8s_request_cache_t));
    if (!cache) return NULL;
    
    cache->max_entries = max_entries;
    cache->max_size_bytes = max_size_bytes;
    cache->ttl_seconds = ttl_seconds;
    cache->enabled = true;
    cache->num_entries = 0;
    cache->current_size_bytes = 0;
    
    return cache;
}

void k8s_request_cache_free(k8s_request_cache_t* cache) {
    if (!cache) return;
    
    for (int i = 0; i < cache->num_entries; i++) {
        k8s_cache_entry_t* entry = cache->entries[i];
        if (entry) {
            free(entry->key);
            free(entry->value);
            free(entry);
        }
    }
    free(cache->entries);
    free(cache);
}

void k8s_request_cache_set_enabled(k8s_request_cache_t* cache, bool enabled) {
    if (cache) {
        cache->enabled = enabled;
    }
}

bool k8s_request_cache_is_enabled(k8s_request_cache_t* cache) {
    if (!cache) return false;
    return cache->enabled;
}

// ============ Helper Functions ============

static void k8s_cache_entry_free(k8s_cache_entry_t* entry) {
    if (!entry) return;
    
    free(entry->key);
    free(entry->value);
    free(entry);
}

static int k8s_cache_entry_size(k8s_cache_entry_t* entry) {
    if (!entry) return 0;
    
    return strlen(entry->key) + strlen(entry->value) + sizeof(k8s_cache_entry_t);
}

static bool k8s_cache_entry_expired(k8s_cache_entry_t* entry, int ttl_seconds) {
    if (!entry) return true;
    
    time_t now = time(NULL);
    return (now - entry->created_at) > ttl_seconds;
}

// ============ Cache Operations ============

int k8s_request_cache_put(k8s_request_cache_t* cache,
                          const char* key,
                          const char* value) {
    if (!cache || !key || !value) return -1;
    if (!cache->enabled) return 0;
    
    // Create new entry
    k8s_cache_entry_t* entry = calloc(1, sizeof(k8s_cache_entry_t));
    if (!entry) return -1;
    
    entry->key = malloc(strlen(key) + 1);
    entry->value = malloc(strlen(value) + 1);
    
    if (!entry->key || !entry->value) {
        k8s_cache_entry_free(entry);
        return -1;
    }
    
    strcpy(entry->key, key);
    strcpy(entry->value, value);
    entry->created_at = time(NULL);
    entry->expires_at = entry->created_at + cache->ttl_seconds;
    entry->hits = 0;
    entry->size = k8s_cache_entry_size(entry);
    
    // Check if cache is full
    if (cache->num_entries >= cache->max_entries ||
        cache->current_size_bytes + entry->size > cache->max_size_bytes) {
        
        // Remove least recently used entry (first one with fewest hits)
        int min_hits = INT_MAX;
        int min_idx = 0;
        
        for (int i = 0; i < cache->num_entries; i++) {
            if (cache->entries[i] && cache->entries[i]->hits < min_hits) {
                min_hits = cache->entries[i]->hits;
                min_idx = i;
            }
        }
        
        // Remove entry
        cache->current_size_bytes -= cache->entries[min_idx]->size;
        k8s_cache_entry_free(cache->entries[min_idx]);
        
        // Shift remaining entries
        for (int i = min_idx; i < cache->num_entries - 1; i++) {
            cache->entries[i] = cache->entries[i + 1];
        }
        cache->num_entries--;
    }
    
    // Add new entry
    k8s_cache_entry_t** new_entries = realloc(cache->entries,
                                               (cache->num_entries + 1) * sizeof(k8s_cache_entry_t*));
    if (!new_entries) {
        k8s_cache_entry_free(entry);
        return -1;
    }
    
    new_entries[cache->num_entries] = entry;
    cache->entries = new_entries;
    cache->num_entries++;
    cache->current_size_bytes += entry->size;
    
    return 0;
}

int k8s_request_cache_get(k8s_request_cache_t* cache,
                          const char* key,
                          char** value) {
    if (!cache || !key || !value) return -1;
    if (!cache->enabled) return -1;
    
    for (int i = 0; i < cache->num_entries; i++) {
        k8s_cache_entry_t* entry = cache->entries[i];
        if (!entry || strcmp(entry->key, key) != 0) continue;
        
        // Check if expired
        if (k8s_cache_entry_expired(entry, cache->ttl_seconds)) {
            // Remove expired entry
            cache->current_size_bytes -= entry->size;
            k8s_cache_entry_free(entry);
            
            for (int j = i; j < cache->num_entries - 1; j++) {
                cache->entries[j] = cache->entries[j + 1];
            }
            cache->num_entries--;
            return -1;  // Entry expired
        }
        
        // Return cached value and increment hits
        *value = entry->value;
        entry->hits++;
        return 0;  // Found
    }
    
    return -1;  // Not found
}

bool k8s_request_cache_contains(k8s_request_cache_t* cache, const char* key) {
    if (!cache || !key) return false;
    
    char* value = NULL;
    return k8s_request_cache_get(cache, key, &value) == 0;
}

int k8s_request_cache_remove(k8s_request_cache_t* cache, const char* key) {
    if (!cache || !key) return -1;
    
    for (int i = 0; i < cache->num_entries; i++) {
        k8s_cache_entry_t* entry = cache->entries[i];
        if (entry && strcmp(entry->key, key) == 0) {
            cache->current_size_bytes -= entry->size;
            k8s_cache_entry_free(entry);
            
            for (int j = i; j < cache->num_entries - 1; j++) {
                cache->entries[j] = cache->entries[j + 1];
            }
            cache->num_entries--;
            return 0;
        }
    }
    
    return -1;  // Not found
}

void k8s_request_cache_clear(k8s_request_cache_t* cache) {
    if (!cache) return;
    
    for (int i = 0; i < cache->num_entries; i++) {
        if (cache->entries[i]) {
            k8s_cache_entry_free(cache->entries[i]);
        }
    }
    free(cache->entries);
    
    cache->entries = NULL;
    cache->num_entries = 0;
    cache->current_size_bytes = 0;
}

// ============ Statistics ============

int k8s_request_cache_get_size(k8s_request_cache_t* cache) {
    if (!cache) return 0;
    return cache->current_size_bytes;
}

int k8s_request_cache_get_entries(k8s_request_cache_t* cache) {
    if (!cache) return 0;
    return cache->num_entries;
}

int k8s_request_cache_get_hits(k8s_request_cache_t* cache) {
    if (!cache) return 0;
    
    int total_hits = 0;
    for (int i = 0; i < cache->num_entries; i++) {
        if (cache->entries[i]) {
            total_hits += cache->entries[i]->hits;
        }
    }
    return total_hits;
}

// ============ Global Cache ============

k8s_request_cache_t* k8s_request_cache_global() {
    if (!g_request_cache) {
        // 100MB max size, 10000 max entries, 1 hour TTL
        g_request_cache = k8s_request_cache_new(10000, 100 * 1024 * 1024, 3600);
    }
    return g_request_cache;
}
