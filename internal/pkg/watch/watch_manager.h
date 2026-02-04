#ifndef WATCH_MANAGER_H
#define WATCH_MANAGER_H

#include <stdint.h>
#include <time.h>
#include <json-c/json.h>
#include <pthread.h>

/*
 * Watch Manager: Real-time event distribution system for Kubernetes API
 * 
 * Supports:
 * - Ring buffer event queuing with fixed capacity
 * - Per-subscription state tracking (resource version, filters)
 * - Event publishing with fan-out to all subscribers
 * - BOOKMARK events for client reconnection and sync
 * - Label/field selector filtering
 * - NDJSON streaming format output
 */

/* Forward declarations */
typedef struct watch_manager watch_manager_t;
typedef struct watch_subscription watch_subscription_t;
typedef struct watch_event watch_event_t;

/* Event type enumeration */
typedef enum {
  WATCH_EVENT_ADDED = 0,       /* Object added */
  WATCH_EVENT_MODIFIED = 1,    /* Object modified */
  WATCH_EVENT_DELETED = 2,     /* Object deleted */
  WATCH_EVENT_BOOKMARK = 3,    /* Bookmark for reconnection/sync */
  WATCH_EVENT_ERROR = 4,       /* Error event */
} watch_event_type_t;

/* Watch event structure */
struct watch_event {
  watch_event_type_t type;           /* Event type (ADDED, MODIFIED, etc.) */
  json_object *object;               /* The actual Kubernetes object (Pod, Service, etc.) */
  int64_t resource_version;          /* Resource version of the event */
  int64_t sequence_number;           /* Event sequence for ordering */
  time_t timestamp;                  /* When event was generated */
  char *error_message;               /* For ERROR events */
};

/* Filter types for subscriptions */
typedef enum {
  FILTER_TYPE_LABEL = 0,       /* Label selector */
  FILTER_TYPE_FIELD = 1,       /* Field selector (e.g., metadata.name=foo) */
  FILTER_TYPE_RESOURCE_VERSION = 2, /* Start from resource version */
} watch_filter_type_t;

/* Single filter condition */
typedef struct {
  watch_filter_type_t type;
  char *key;                    /* Label key or field path */
  char *value;                  /* Expected value */
  int match_any;                /* If true, value is a list (e.g., "foo,bar") */
} watch_filter_t;

/* Subscription state */
struct watch_subscription {
  int subscription_id;           /* Unique subscription identifier */
  int64_t resource_version;      /* Client's starting resource version */
  watch_filter_t *filters;       /* Array of filters */
  int filter_count;              /* Number of active filters */
  int64_t last_delivered_event;  /* Sequence of last delivered event */
  int closed;                    /* Whether subscription is closed */
  pthread_mutex_t lock;          /* Protects this subscription */
  void *user_context;            /* For HTTP handler integration */
};

/* Ring buffer management */
typedef struct {
  watch_event_t *events;         /* Circular buffer of events */
  int capacity;                  /* Total capacity of buffer */
  int size;                      /* Current number of events */
  int64_t head_index;            /* Index of oldest event */
  int64_t tail_index;            /* Index of next write position */
  int64_t global_sequence;       /* Global event sequence counter */
  pthread_mutex_t lock;          /* Protects ring buffer */
} watch_ring_buffer_t;

/* Main watch manager structure */
struct watch_manager {
  watch_ring_buffer_t ring_buffer;        /* Circular event buffer */
  watch_subscription_t **subscriptions;   /* Array of active subscriptions */
  int subscription_count;                 /* Number of active subscriptions */
  int max_subscriptions;                  /* Capacity of subscriptions array */
  int next_subscription_id;               /* For generating unique IDs */
  pthread_mutex_t subscriptions_lock;     /* Protects subscriptions array */
  int enabled;                            /* Whether watching is active */
};

/* === Core Watch Manager Functions === */

/**
 * Create a new watch manager with specified ring buffer capacity
 * 
 * @param buffer_capacity Number of events to buffer (recommended: 1000)
 * @return Allocated watch manager, or NULL on error
 */
watch_manager_t* watch_manager_create(int buffer_capacity);

/**
 * Destroy watch manager and free all resources
 * 
 * @param wm Watch manager to destroy
 */
void watch_manager_destroy(watch_manager_t *wm);

/**
 * Enable or disable watching (can be toggled at runtime)
 * 
 * @param wm Watch manager
 * @param enabled 1 to enable, 0 to disable
 */
void watch_manager_set_enabled(watch_manager_t *wm, int enabled);

/* === Event Publishing === */

/**
 * Publish an event to all subscribers
 * Called whenever a pod/service/etc is created, modified, or deleted
 * 
 * @param wm Watch manager
 * @param type Event type (ADDED, MODIFIED, DELETED)
 * @param object JSON object representing the resource
 * @param resource_version Resource version of the event
 * @return 0 on success, -1 on error
 */
int watch_manager_publish_event(watch_manager_t *wm,
                                 watch_event_type_t type,
                                 json_object *object,
                                 int64_t resource_version);

/**
 * Publish a bookmark event for client reconnection
 * Clients use this to know the latest resource version without processing all events
 * 
 * @param wm Watch manager
 * @param resource_version Current resource version
 * @return 0 on success, -1 on error
 */
int watch_manager_publish_bookmark(watch_manager_t *wm, int64_t resource_version);

/**
 * Publish an error event
 * 
 * @param wm Watch manager
 * @param error_message Description of error
 * @return 0 on success, -1 on error
 */
int watch_manager_publish_error(watch_manager_t *wm, const char *error_message);

/* === Subscription Management === */

/**
 * Create a new subscription
 * 
 * @param wm Watch manager
 * @param resource_version Start watching from this resource version (0 for all events)
 * @return New subscription object, or NULL on error
 */
watch_subscription_t* watch_manager_subscribe(watch_manager_t *wm,
                                               int64_t resource_version);

/**
 * Close a subscription and remove it from the manager
 * 
 * @param wm Watch manager
 * @param subscription Subscription to close
 */
void watch_manager_unsubscribe(watch_manager_t *wm,
                                watch_subscription_t *subscription);

/**
 * Add a filter to a subscription (e.g., label=foo)
 * Filters are combined with AND logic (all must match)
 * 
 * @param subscription Subscription to add filter to
 * @param filter_type Type of filter (LABEL, FIELD, RESOURCE_VERSION)
 * @param key Field key or label key
 * @param value Expected value
 * @return 0 on success, -1 on error
 */
int watch_subscription_add_filter(watch_subscription_t *subscription,
                                   watch_filter_type_t filter_type,
                                   const char *key,
                                   const char *value);

/* === Event Retrieval === */

/**
 * Get next event for a subscription (blocking)
 * Returns events in order with proper filtering applied
 * Returns BOOKMARK events to notify client of current resource version
 * 
 * @param wm Watch manager
 * @param subscription Subscription to get event from
 * @param timeout_ms Timeout in milliseconds (0 = non-blocking, -1 = infinite)
 * @return Next event for this subscription, or NULL if none available
 *         Caller must free using watch_event_free()
 */
watch_event_t* watch_manager_get_event(watch_manager_t *wm,
                                        watch_subscription_t *subscription,
                                        int timeout_ms);

/**
 * Get all events since last delivery (batch operation)
 * Useful for initial sync when client provides resource version
 * 
 * @param wm Watch manager
 * @param subscription Subscription to fetch events for
 * @param start_version Start from this resource version
 * @param max_events Maximum number of events to return
 * @return Array of events, NULL-terminated
 *         Caller must free array and each event
 */
watch_event_t** watch_manager_get_events_batch(watch_manager_t *wm,
                                                watch_subscription_t *subscription,
                                                int64_t start_version,
                                                int max_events);

/* === Filtering === */

/**
 * Check if an object matches all filters in a subscription
 * 
 * @param object JSON object to check
 * @param subscription Subscription with filters
 * @return 1 if matches all filters, 0 if doesn't match
 */
int watch_object_matches_filters(json_object *object,
                                  watch_subscription_t *subscription);

/**
 * Check if object matches a specific label selector
 * 
 * @param object JSON object
 * @param label_key Label key to check
 * @param label_value Expected label value
 * @return 1 if matches, 0 if doesn't
 */
int watch_object_matches_label(json_object *object,
                                const char *label_key,
                                const char *label_value);

/**
 * Check if object matches a field selector (e.g., metadata.name=foo)
 * 
 * @param object JSON object
 * @param field_path Dot-separated path (e.g., "metadata.name")
 * @param expected_value Expected value
 * @return 1 if matches, 0 if doesn't
 */
int watch_object_matches_field(json_object *object,
                                const char *field_path,
                                const char *expected_value);

/* === Event Utilities === */

/**
 * Create a new watch event
 * 
 * @param type Event type
 * @param object JSON object (will be referenced, not copied)
 * @param resource_version Resource version
 * @return Allocated event, or NULL on error
 */
watch_event_t* watch_event_create(watch_event_type_t type,
                                   json_object *object,
                                   int64_t resource_version);

/**
 * Create a bookmark event
 * 
 * @param resource_version Current resource version
 * @return Allocated bookmark event
 */
watch_event_t* watch_event_create_bookmark(int64_t resource_version);

/**
 * Create an error event
 * 
 * @param error_message Error description
 * @return Allocated error event
 */
watch_event_t* watch_event_create_error(const char *error_message);

/**
 * Convert event to NDJSON format (single line JSON with newline)
 * Format compatible with kubectl and other Kubernetes clients
 * 
 * @param event Event to convert
 * @return JSON string with embedded "type" and "object" fields
 *         Caller must free using free()
 */
char* watch_event_to_ndjson(watch_event_t *event);

/**
 * Free a watch event
 * 
 * @param event Event to free
 */
void watch_event_free(watch_event_t *event);

/**
 * Free array of watch events (NULL-terminated)
 * 
 * @param events Array to free
 */
void watch_events_free(watch_event_t **events);

/* === Statistics === */

/**
 * Get manager statistics for monitoring
 * 
 * @param wm Watch manager
 * @param total_events Output: total events in buffer
 * @param subscriptions Output: number of active subscriptions
 * @param buffer_usage Output: percentage of buffer used (0-100)
 * @return 0 on success, -1 on error
 */
int watch_manager_get_stats(watch_manager_t *wm,
                             int64_t *total_events,
                             int *subscriptions,
                             int *buffer_usage);

#endif /* WATCH_MANAGER_H */
