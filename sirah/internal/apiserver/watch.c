// internal/apiserver/watch.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include <pthread.h>
#include <time.h>
#include "watch.h"
#include "query_parser.h"

// Helper function for JSON deep copy compatibility
static json_object* json_dup(json_object* obj) {
    if (!obj) return NULL;
    const char* str = json_object_to_json_string(obj);
    return json_tokener_parse(str);
}
#define MAX_WATCHES 100
#define MAX_EVENTS_PER_WATCH 1000

typedef struct {
    int active;
    char resource_type[64];
    char namespace[64];
    list_query_params_t filters;
    
    // Event queue
    watch_event_t events[MAX_EVENTS_PER_WATCH];
    int event_count;
    int event_read_idx;
    
    pthread_mutex_t lock;
    time_t created_at;
    unsigned long resource_version;
} watch_session_t;

static watch_session_t watches[MAX_WATCHES];
static pthread_mutex_t watches_lock = PTHREAD_MUTEX_INITIALIZER;

static int initialized = 0;

// Initialize watch subsystem
static void watch_init(void) {
    if (initialized) return;
    
    pthread_mutex_lock(&watches_lock);
    for (int i = 0; i < MAX_WATCHES; i++) {
        memset(&watches[i], 0, sizeof(watch_session_t));
        pthread_mutex_init(&watches[i].lock, NULL);
    }
    initialized = 1;
    pthread_mutex_unlock(&watches_lock);
}

// Start watching
int watch_start(const char* resource_type, const char* namespace,
                const char* label_selector, const char* field_selector) {
    watch_init();
    
    pthread_mutex_lock(&watches_lock);
    
    // Find free slot
    int watch_id = -1;
    for (int i = 0; i < MAX_WATCHES; i++) {
        if (!watches[i].active) {
            watch_id = i;
            break;
        }
    }
    
    if (watch_id < 0) {
        pthread_mutex_unlock(&watches_lock);
        return -1;  // No free slots
    }
    
    // Initialize watch session
    watches[watch_id].active = 1;
    strncpy(watches[watch_id].resource_type, resource_type, sizeof(watches[watch_id].resource_type) - 1);
    strncpy(watches[watch_id].namespace, namespace, sizeof(watches[watch_id].namespace) - 1);
    watches[watch_id].event_count = 0;
    watches[watch_id].event_read_idx = 0;
    watches[watch_id].created_at = time(NULL);
    watches[watch_id].resource_version = 0;
    
    // Parse filters
    if (label_selector) {
        strncpy(watches[watch_id].filters.label_selector, label_selector, 
                sizeof(watches[watch_id].filters.label_selector) - 1);
    }
    if (field_selector) {
        strncpy(watches[watch_id].filters.field_selector, field_selector,
                sizeof(watches[watch_id].filters.field_selector) - 1);
    }
    
    pthread_mutex_unlock(&watches_lock);
    return watch_id;
}

// Get next event
int watch_next_event(int watch_id, watch_event_t* event, int timeout_ms) {
    if (watch_id < 0 || watch_id >= MAX_WATCHES) {
        return -1;
    }
    
    watch_session_t* ws = &watches[watch_id];
    time_t timeout_end = time(NULL) + (timeout_ms / 1000);
    
    while (time(NULL) < timeout_end) {
        pthread_mutex_lock(&ws->lock);
        
        if (ws->event_read_idx < ws->event_count) {
            // Event available
            memcpy(event, &ws->events[ws->event_read_idx], sizeof(watch_event_t));
            ws->event_read_idx++;
            pthread_mutex_unlock(&ws->lock);
            return 0;
        }
        
        pthread_mutex_unlock(&ws->lock);
        
        // Wait a bit before checking again
        usleep(100000);  // 100ms
    }
    
    // Timeout
    return 1;
}

// Stop watching
int watch_stop(int watch_id) {
    if (watch_id < 0 || watch_id >= MAX_WATCHES) {
        return -1;
    }
    
    watch_session_t* ws = &watches[watch_id];
    pthread_mutex_lock(&ws->lock);
    ws->active = 0;
    pthread_mutex_unlock(&ws->lock);
    
    return 0;
}

// Send event to all matching watchers
int watch_notify_all(const char* resource_type, const char* namespace,
                     watch_event_type_t event_type, json_object* object) {
    watch_init();
    
    pthread_mutex_lock(&watches_lock);
    
    for (int i = 0; i < MAX_WATCHES; i++) {
        watch_session_t* ws = &watches[i];
        
        if (!ws->active) continue;
        
        // Check if this watcher cares about this resource
        if (strcmp(ws->resource_type, resource_type) != 0) continue;
        
        // Check namespace (empty namespace = all namespaces)
        if (strlen(ws->namespace) > 0 && strcmp(ws->namespace, namespace) != 0) continue;
        
        // Check filters
        if (strlen(ws->filters.label_selector) > 0 && 
            !matches_label_selector(object, ws->filters.label_selector)) {
            continue;
        }
        
        if (strlen(ws->filters.field_selector) > 0 &&
            !matches_field_selector(object, ws->filters.field_selector)) {
            continue;
        }
        
        // Add event to queue
        pthread_mutex_lock(&ws->lock);
        
        if (ws->event_count < MAX_EVENTS_PER_WATCH) {
            ws->events[ws->event_count].type = event_type;
            ws->events[ws->event_count].object = json_dup(object);
            ws->events[ws->event_count].resource_version = ws->resource_version++;
            ws->event_count++;
        }
        
        pthread_mutex_unlock(&ws->lock);
    }
    
    pthread_mutex_unlock(&watches_lock);
    return 0;
}
