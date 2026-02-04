#include "watch_manager.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* === Ring Buffer Implementation === */

static void watch_ring_buffer_init(watch_ring_buffer_t *rb, int capacity) {
  rb->events = calloc(capacity, sizeof(watch_event_t));
  rb->capacity = capacity;
  rb->size = 0;
  rb->head_index = 0;
  rb->tail_index = 0;
  rb->global_sequence = 0;
  pthread_mutex_init(&rb->lock, NULL);
}

static void watch_ring_buffer_cleanup(watch_ring_buffer_t *rb) {
  pthread_mutex_lock(&rb->lock);
  for (int i = 0; i < rb->size; i++) {
    int idx = (rb->head_index + i) % rb->capacity;
    watch_event_free(&rb->events[idx]);
  }
  free(rb->events);
  pthread_mutex_unlock(&rb->lock);
  pthread_mutex_destroy(&rb->lock);
}

static void watch_ring_buffer_push(watch_ring_buffer_t *rb, watch_event_t *event) {
  pthread_mutex_lock(&rb->lock);
  
  event->sequence_number = rb->global_sequence++;
  
  /* If buffer is full, overwrite oldest event */
  if (rb->size == rb->capacity) {
    watch_event_free(&rb->events[rb->head_index]);
    rb->head_index = (rb->head_index + 1) % rb->capacity;
  } else {
    rb->size++;
  }
  
  /* Add new event at tail */
  rb->events[rb->tail_index] = *event;
  rb->tail_index = (rb->tail_index + 1) % rb->capacity;
  
  pthread_mutex_unlock(&rb->lock);
}

static watch_event_t* watch_ring_buffer_get_by_sequence(watch_ring_buffer_t *rb,
                                                         int64_t sequence) {
  pthread_mutex_lock(&rb->lock);
  
  for (int i = 0; i < rb->size; i++) {
    int idx = (rb->head_index + i) % rb->capacity;
    if (rb->events[idx].sequence_number == sequence) {
      watch_event_t *result = malloc(sizeof(watch_event_t));
      *result = rb->events[idx];
      
      /* Deep copy JSON object */
      if (result->object != NULL) {
        result->object = json_object_get(result->object);
      }
      if (result->error_message != NULL) {
        result->error_message = strdup(result->error_message);
      }
      
      pthread_mutex_unlock(&rb->lock);
      return result;
    }
  }
  
  pthread_mutex_unlock(&rb->lock);
  return NULL;
}

static watch_event_t** watch_ring_buffer_get_range(watch_ring_buffer_t *rb,
                                                     int64_t start_sequence,
                                                     int max_events) {
  pthread_mutex_lock(&rb->lock);
  
  watch_event_t **result = calloc(max_events + 1, sizeof(watch_event_t *));
  int count = 0;
  
  for (int i = 0; i < rb->size && count < max_events; i++) {
    int idx = (rb->head_index + i) % rb->capacity;
    if (rb->events[idx].sequence_number >= start_sequence) {
      result[count] = malloc(sizeof(watch_event_t));
      *result[count] = rb->events[idx];
      
      /* Deep copy */
      if (result[count]->object != NULL) {
        result[count]->object = json_object_get(result[count]->object);
      }
      if (result[count]->error_message != NULL) {
        result[count]->error_message = strdup(result[count]->error_message);
      }
      
      count++;
    }
  }
  
  result[count] = NULL;
  
  pthread_mutex_unlock(&rb->lock);
  return result;
}

/* === Watch Manager Functions === */

watch_manager_t* watch_manager_create(int buffer_capacity) {
  if (buffer_capacity <= 0) {
    return NULL;
  }
  
  watch_manager_t *wm = malloc(sizeof(watch_manager_t));
  if (wm == NULL) {
    return NULL;
  }
  
  watch_ring_buffer_init(&wm->ring_buffer, buffer_capacity);
  wm->max_subscriptions = 100;
  wm->subscriptions = calloc(wm->max_subscriptions, sizeof(watch_subscription_t *));
  wm->subscription_count = 0;
  wm->next_subscription_id = 1;
  wm->enabled = 1;
  pthread_mutex_init(&wm->subscriptions_lock, NULL);
  
  return wm;
}

void watch_manager_destroy(watch_manager_t *wm) {
  if (wm == NULL) {
    return;
  }
  
  pthread_mutex_lock(&wm->subscriptions_lock);
  for (int i = 0; i < wm->subscription_count; i++) {
    watch_subscription_t *sub = wm->subscriptions[i];
    for (int j = 0; j < sub->filter_count; j++) {
      free(sub->filters[j].key);
      free(sub->filters[j].value);
    }
    free(sub->filters);
    pthread_mutex_destroy(&sub->lock);
    free(sub);
  }
  free(wm->subscriptions);
  pthread_mutex_unlock(&wm->subscriptions_lock);
  
  watch_ring_buffer_cleanup(&wm->ring_buffer);
  pthread_mutex_destroy(&wm->subscriptions_lock);
  free(wm);
}

void watch_manager_set_enabled(watch_manager_t *wm, int enabled) {
  if (wm != NULL) {
    wm->enabled = enabled;
  }
}

/* === Event Publishing === */

int watch_manager_publish_event(watch_manager_t *wm,
                                 watch_event_type_t type,
                                 json_object *object,
                                 int64_t resource_version) {
  if (wm == NULL || !wm->enabled || object == NULL) {
    return -1;
  }
  
  watch_event_t event = {0};
  event.type = type;
  event.object = json_object_get(object); /* Increment reference count */
  event.resource_version = resource_version;
  event.timestamp = time(NULL);
  
  watch_ring_buffer_push(&wm->ring_buffer, &event);
  
  return 0;
}

int watch_manager_publish_bookmark(watch_manager_t *wm, int64_t resource_version) {
  if (wm == NULL || !wm->enabled) {
    return -1;
  }
  
  watch_event_t event = {0};
  event.type = WATCH_EVENT_BOOKMARK;
  event.object = NULL;
  event.resource_version = resource_version;
  event.timestamp = time(NULL);
  
  watch_ring_buffer_push(&wm->ring_buffer, &event);
  
  return 0;
}

int watch_manager_publish_error(watch_manager_t *wm, const char *error_message) {
  if (wm == NULL || !wm->enabled || error_message == NULL) {
    return -1;
  }
  
  watch_event_t event = {0};
  event.type = WATCH_EVENT_ERROR;
  event.object = NULL;
  event.error_message = strdup(error_message);
  event.timestamp = time(NULL);
  
  watch_ring_buffer_push(&wm->ring_buffer, &event);
  
  return 0;
}

/* === Subscription Management === */

watch_subscription_t* watch_manager_subscribe(watch_manager_t *wm,
                                               int64_t resource_version) {
  if (wm == NULL) {
    return NULL;
  }
  
  pthread_mutex_lock(&wm->subscriptions_lock);
  
  if (wm->subscription_count >= wm->max_subscriptions) {
    /* Expand subscriptions array */
    wm->max_subscriptions *= 2;
    wm->subscriptions = realloc(wm->subscriptions,
                                 wm->max_subscriptions * sizeof(watch_subscription_t *));
  }
  
  watch_subscription_t *sub = calloc(1, sizeof(watch_subscription_t));
  if (sub == NULL) {
    pthread_mutex_unlock(&wm->subscriptions_lock);
    return NULL;
  }
  
  sub->subscription_id = wm->next_subscription_id++;
  sub->resource_version = resource_version;
  sub->filters = calloc(10, sizeof(watch_filter_t));
  sub->filter_count = 0;
  sub->last_delivered_event = -1;
  sub->closed = 0;
  pthread_mutex_init(&sub->lock, NULL);
  
  wm->subscriptions[wm->subscription_count++] = sub;
  
  pthread_mutex_unlock(&wm->subscriptions_lock);
  
  return sub;
}

void watch_manager_unsubscribe(watch_manager_t *wm,
                                watch_subscription_t *subscription) {
  if (wm == NULL || subscription == NULL) {
    return;
  }
  
  pthread_mutex_lock(&wm->subscriptions_lock);
  
  for (int i = 0; i < wm->subscription_count; i++) {
    if (wm->subscriptions[i] == subscription) {
      /* Mark as closed */
      subscription->closed = 1;
      
      /* Remove from array */
      for (int j = i; j < wm->subscription_count - 1; j++) {
        wm->subscriptions[j] = wm->subscriptions[j + 1];
      }
      wm->subscription_count--;
      
      /* Free resources */
      for (int j = 0; j < subscription->filter_count; j++) {
        free(subscription->filters[j].key);
        free(subscription->filters[j].value);
      }
      free(subscription->filters);
      pthread_mutex_destroy(&subscription->lock);
      free(subscription);
      
      break;
    }
  }
  
  pthread_mutex_unlock(&wm->subscriptions_lock);
}

int watch_subscription_add_filter(watch_subscription_t *subscription,
                                   watch_filter_type_t filter_type,
                                   const char *key,
                                   const char *value) {
  if (subscription == NULL || key == NULL || value == NULL) {
    return -1;
  }
  
  pthread_mutex_lock(&subscription->lock);
  
  /* Expand filter array if needed */
  if (subscription->filter_count >= 10) {
    watch_filter_t *new_filters = realloc(subscription->filters,
                                           (subscription->filter_count + 10) * sizeof(watch_filter_t));
    if (new_filters == NULL) {
      pthread_mutex_unlock(&subscription->lock);
      return -1;
    }
    subscription->filters = new_filters;
  }
  
  watch_filter_t *filter = &subscription->filters[subscription->filter_count++];
  filter->type = filter_type;
  filter->key = strdup(key);
  filter->value = strdup(value);
  filter->match_any = 0;
  
  pthread_mutex_unlock(&subscription->lock);
  
  return 0;
}

/* === Event Retrieval === */

watch_event_t* watch_manager_get_event(watch_manager_t *wm,
                                        watch_subscription_t *subscription,
                                        int timeout_ms) {
  if (wm == NULL || subscription == NULL || subscription->closed) {
    return NULL;
  }
  
  pthread_mutex_lock(&subscription->lock);
  int64_t next_sequence = subscription->last_delivered_event + 1;
  pthread_mutex_unlock(&subscription->lock);
  
  /* Simple polling loop (production code would use condition variables) */
  time_t start = time(NULL);
  while (1) {
    watch_event_t *event = watch_ring_buffer_get_by_sequence(&wm->ring_buffer,
                                                               next_sequence);
    if (event != NULL) {
      /* Check if event passes filters */
      if (event->type != WATCH_EVENT_BOOKMARK && event->type != WATCH_EVENT_ERROR) {
        if (!watch_object_matches_filters(event->object, subscription)) {
          watch_event_free(event);
          next_sequence++;
          continue;
        }
      }
      
      /* Update last delivered */
      pthread_mutex_lock(&subscription->lock);
      subscription->last_delivered_event = event->sequence_number;
      pthread_mutex_unlock(&subscription->lock);
      
      return event;
    }
    
    /* Check timeout */
    if (timeout_ms >= 0) {
      time_t now = time(NULL);
      if ((now - start) * 1000 >= timeout_ms) {
        return NULL;
      }
    }
    
    /* Sleep briefly before retry */
    usleep(10000); /* 10ms */
  }
  
  return NULL;
}

watch_event_t** watch_manager_get_events_batch(watch_manager_t *wm,
                                                watch_subscription_t *subscription,
                                                int64_t start_version,
                                                int max_events) {
  if (wm == NULL || subscription == NULL) {
    return calloc(1, sizeof(watch_event_t *)); /* Return empty array */
  }
  
  /* Get events from ring buffer starting after this version */
  return watch_ring_buffer_get_range(&wm->ring_buffer, start_version, max_events);
}

/* === Filtering === */

int watch_object_matches_filters(json_object *object,
                                  watch_subscription_t *subscription) {
  if (object == NULL || subscription == NULL) {
    return 1; /* No filters = match all */
  }
  
  pthread_mutex_lock(&subscription->lock);
  
  for (int i = 0; i < subscription->filter_count; i++) {
    watch_filter_t *filter = &subscription->filters[i];
    
    int matches = 0;
    switch (filter->type) {
      case FILTER_TYPE_LABEL:
        matches = watch_object_matches_label(object, filter->key, filter->value);
        break;
      case FILTER_TYPE_FIELD:
        matches = watch_object_matches_field(object, filter->key, filter->value);
        break;
      case FILTER_TYPE_RESOURCE_VERSION:
        /* Version filtering handled separately */
        matches = 1;
        break;
    }
    
    if (!matches) {
      pthread_mutex_unlock(&subscription->lock);
      return 0; /* Doesn't match this filter (AND logic) */
    }
  }
  
  pthread_mutex_unlock(&subscription->lock);
  return 1; /* Matches all filters */
}

int watch_object_matches_label(json_object *object,
                                const char *label_key,
                                const char *label_value) {
  if (object == NULL || label_key == NULL || label_value == NULL) {
    return 1;
  }
  
  json_object *metadata = json_object_object_get(object, "metadata");
  if (metadata == NULL) {
    return 0;
  }
  
  json_object *labels = json_object_object_get(metadata, "labels");
  if (labels == NULL) {
    return 0;
  }
  
  json_object *label_val = json_object_object_get(labels, label_key);
  if (label_val == NULL) {
    return 0;
  }
  
  const char *actual = json_object_get_string(label_val);
  return actual != NULL && strcmp(actual, label_value) == 0;
}

int watch_object_matches_field(json_object *object,
                                const char *field_path,
                                const char *expected_value) {
  if (object == NULL || field_path == NULL || expected_value == NULL) {
    return 1;
  }
  
  /* Parse dot-separated path (e.g., "metadata.name") */
  char *path = strdup(field_path);
  char *saveptr = NULL;
  char *part = strtok_r(path, ".", &saveptr);
  
  json_object *current = object;
  while (part != NULL && current != NULL) {
    current = json_object_object_get(current, part);
    part = strtok_r(NULL, ".", &saveptr);
  }
  
  free(path);
  
  if (current == NULL) {
    return 0;
  }
  
  const char *actual = json_object_get_string(current);
  if (actual == NULL) {
    return 0;
  }
  
  return strcmp(actual, expected_value) == 0;
}

/* === Event Utilities === */

watch_event_t* watch_event_create(watch_event_type_t type,
                                   json_object *object,
                                   int64_t resource_version) {
  watch_event_t *event = malloc(sizeof(watch_event_t));
  if (event == NULL) {
    return NULL;
  }
  
  event->type = type;
  event->object = object != NULL ? json_object_get(object) : NULL;
  event->resource_version = resource_version;
  event->sequence_number = 0;
  event->timestamp = time(NULL);
  event->error_message = NULL;
  
  return event;
}

watch_event_t* watch_event_create_bookmark(int64_t resource_version) {
  watch_event_t *event = malloc(sizeof(watch_event_t));
  if (event == NULL) {
    return NULL;
  }
  
  event->type = WATCH_EVENT_BOOKMARK;
  event->object = NULL;
  event->resource_version = resource_version;
  event->sequence_number = 0;
  event->timestamp = time(NULL);
  event->error_message = NULL;
  
  return event;
}

watch_event_t* watch_event_create_error(const char *error_message) {
  watch_event_t *event = malloc(sizeof(watch_event_t));
  if (event == NULL) {
    return NULL;
  }
  
  event->type = WATCH_EVENT_ERROR;
  event->object = NULL;
  event->resource_version = 0;
  event->sequence_number = 0;
  event->timestamp = time(NULL);
  event->error_message = error_message != NULL ? strdup(error_message) : NULL;
  
  return event;
}

char* watch_event_to_ndjson(watch_event_t *event) {
  if (event == NULL) {
    return NULL;
  }
  
  json_object *watch_obj = json_object_new_object();
  
  /* Add type field */
  const char *type_str = NULL;
  switch (event->type) {
    case WATCH_EVENT_ADDED:
      type_str = "ADDED";
      break;
    case WATCH_EVENT_MODIFIED:
      type_str = "MODIFIED";
      break;
    case WATCH_EVENT_DELETED:
      type_str = "DELETED";
      break;
    case WATCH_EVENT_BOOKMARK:
      type_str = "BOOKMARK";
      break;
    case WATCH_EVENT_ERROR:
      type_str = "ERROR";
      break;
  }
  
  json_object_object_add(watch_obj, "type",
                         json_object_new_string(type_str));
  
  /* Add object field */
  if (event->object != NULL) {
    json_object_object_add(watch_obj, "object",
                           json_object_get(event->object));
  } else if (event->type == WATCH_EVENT_ERROR && event->error_message != NULL) {
    /* For errors, include the message */
    json_object *error_obj = json_object_new_object();
    json_object_object_add(error_obj, "message",
                           json_object_new_string(event->error_message));
    json_object_object_add(watch_obj, "object", error_obj);
  }
  
  /* Convert to NDJSON (newline-delimited JSON) */
  const char *json_str = json_object_to_json_string_ext(watch_obj,
                                                         JSON_C_TO_STRING_PLAIN);
  char *result = malloc(strlen(json_str) + 2);
  sprintf(result, "%s\n", json_str);
  
  json_object_put(watch_obj);
  
  return result;
}

void watch_event_free(watch_event_t *event) {
  if (event == NULL) {
    return;
  }
  
  if (event->object != NULL) {
    json_object_put(event->object);
  }
  if (event->error_message != NULL) {
    free(event->error_message);
  }
}

void watch_events_free(watch_event_t **events) {
  if (events == NULL) {
    return;
  }
  
  for (int i = 0; events[i] != NULL; i++) {
    watch_event_free(events[i]);
    free(events[i]);
  }
  
  free(events);
}

/* === Statistics === */

int watch_manager_get_stats(watch_manager_t *wm,
                             int64_t *total_events,
                             int *subscriptions,
                             int *buffer_usage) {
  if (wm == NULL) {
    return -1;
  }
  
  pthread_mutex_lock(&wm->ring_buffer.lock);
  *total_events = wm->ring_buffer.global_sequence;
  *buffer_usage = (wm->ring_buffer.size * 100) / wm->ring_buffer.capacity;
  pthread_mutex_unlock(&wm->ring_buffer.lock);
  
  pthread_mutex_lock(&wm->subscriptions_lock);
  *subscriptions = wm->subscription_count;
  pthread_mutex_unlock(&wm->subscriptions_lock);
  
  return 0;
}
