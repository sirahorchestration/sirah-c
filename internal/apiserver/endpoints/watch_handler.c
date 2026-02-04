#include "watch_handler.h"
#include "../../watch/watch_manager.h"
#include "../../../pkg/types/pod.h"
#include "../../../etcd/etcd_manager.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* Global watch manager instance (should be created once at startup) */
static watch_manager_t *g_watch_manager = NULL;

void watch_handler_init(watch_manager_t *wm) {
  g_watch_manager = wm;
}

void watch_handler_cleanup(void) {
  if (g_watch_manager != NULL) {
    watch_manager_destroy(g_watch_manager);
    g_watch_manager = NULL;
  }
}

/* === Watch HTTP Handler === */

/**
 * GET /api/v1/namespaces/{namespace}/pods?watch=true
 * 
 * Streaming endpoint that returns pod changes as NDJSON events.
 * Supports label and field selectors for filtering.
 * 
 * Query Parameters:
 * - watch=true       : Enable watch mode (required)
 * - resourceVersion  : Start from this resource version (for reconnection)
 * - labelSelector    : Filter by label (e.g., labelSelector=app=web)
 * - fieldSelector    : Filter by field (e.g., fieldSelector=metadata.name=my-pod)
 * 
 * Response Format (NDJSON):
 * {"type":"ADDED","object":{...pod...}}
 * {"type":"MODIFIED","object":{...pod...}}
 * {"type":"DELETED","object":{...pod...}}
 * {"type":"BOOKMARK","object":{"metadata":{"resourceVersion":"12345"}}}
 */
int endpoint_watch_pods(http_request_t *request, http_response_t *response) {
  if (g_watch_manager == NULL) {
    return http_response_error(response, 503,
      "Service Unavailable", "Watch service not initialized");
  }
  
  /* Check if watch parameter is present */
  const char *watch_param = http_request_get_query_param(request, "watch");
  if (watch_param == NULL || strcmp(watch_param, "true") != 0) {
    return http_response_error(response, 400,
      "Bad Request", "watch=true parameter is required");
  }
  
  /* Extract resource version (optional, for reconnection) */
  int64_t resource_version = 0;
  const char *rv_param = http_request_get_query_param(request, "resourceVersion");
  if (rv_param != NULL) {
    resource_version = strtoll(rv_param, NULL, 10);
  }
  
  /* Create subscription */
  watch_subscription_t *sub = watch_manager_subscribe(g_watch_manager, resource_version);
  if (sub == NULL) {
    return http_response_error(response, 500,
      "Internal Server Error", "Failed to create watch subscription");
  }
  
  /* Add label selector filter if provided */
  const char *label_selector = http_request_get_query_param(request, "labelSelector");
  if (label_selector != NULL) {
    /* Parse label selector (format: key=value,key2=value2) */
    char *selector_copy = strdup(label_selector);
    char *saveptr = NULL;
    char *part = strtok_r(selector_copy, ",", &saveptr);
    
    while (part != NULL) {
      char *eq = strchr(part, '=');
      if (eq != NULL) {
        *eq = '\0';
        const char *key = part;
        const char *value = eq + 1;
        watch_subscription_add_filter(sub, FILTER_TYPE_LABEL, key, value);
      }
      part = strtok_r(NULL, ",", &saveptr);
    }
    
    free(selector_copy);
  }
  
  /* Add field selector filter if provided */
  const char *field_selector = http_request_get_query_param(request, "fieldSelector");
  if (field_selector != NULL) {
    /* Parse field selector (format: metadata.name=foo) */
    char *selector_copy = strdup(field_selector);
    char *saveptr = NULL;
    char *part = strtok_r(selector_copy, ",", &saveptr);
    
    while (part != NULL) {
      char *eq = strchr(part, '=');
      if (eq != NULL) {
        *eq = '\0';
        const char *path = part;
        const char *value = eq + 1;
        watch_subscription_add_filter(sub, FILTER_TYPE_FIELD, path, value);
      }
      part = strtok_r(NULL, ",", &saveptr);
    }
    
    free(selector_copy);
  }
  
  /* Set up HTTP response for streaming */
  response->status_code = 200;
  response->content_type = "application/x-ndjson";
  response->chunked = 1; /* Enable chunked transfer encoding */
  
  http_response_send_headers(response);
  
  /* Send BOOKMARK event with current resource version */
  watch_event_t *bookmark = watch_event_create_bookmark(resource_version);
  char *ndjson = watch_event_to_ndjson(bookmark);
  http_response_write(response, ndjson);
  free(ndjson);
  watch_event_free(bookmark);
  
  /* Stream events to client */
  while (!sub->closed) {
    /* Get next event (with 30 second timeout) */
    watch_event_t *event = watch_manager_get_event(g_watch_manager, sub, 30000);
    
    if (event == NULL) {
      /* Timeout - send periodic BOOKMARK to keep connection alive */
      int64_t current_rv = 0;
      int subscriptions = 0;
      int buffer_usage = 0;
      watch_manager_get_stats(g_watch_manager, &current_rv, &subscriptions, &buffer_usage);
      
      watch_event_t *keep_alive = watch_event_create_bookmark(current_rv);
      char *ndjson = watch_event_to_ndjson(keep_alive);
      if (http_response_write(response, ndjson) < 0) {
        free(ndjson);
        watch_event_free(keep_alive);
        break; /* Client disconnected */
      }
      free(ndjson);
      watch_event_free(keep_alive);
      continue;
    }
    
    /* Convert event to NDJSON and send */
    char *ndjson = watch_event_to_ndjson(event);
    if (http_response_write(response, ndjson) < 0) {
      free(ndjson);
      watch_event_free(event);
      break; /* Client disconnected */
    }
    free(ndjson);
    watch_event_free(event);
  }
  
  /* Clean up subscription */
  watch_manager_unsubscribe(g_watch_manager, sub);
  
  return 0;
}

/**
 * GET /api/v1/pods/{namespace}/{name}?watch=true
 * 
 * Watch a single pod for changes.
 * Useful for monitoring a specific pod's lifecycle.
 */
int endpoint_watch_pod(http_request_t *request, http_response_t *response) {
  if (g_watch_manager == NULL) {
    return http_response_error(response, 503,
      "Service Unavailable", "Watch service not initialized");
  }
  
  /* Check if watch parameter is present */
  const char *watch_param = http_request_get_query_param(request, "watch");
  if (watch_param == NULL || strcmp(watch_param, "true") != 0) {
    return http_response_error(response, 400,
      "Bad Request", "watch=true parameter is required");
  }
  
  /* Extract pod name and namespace from request path */
  const char *pod_name = http_request_get_path_param(request, "name");
  const char *namespace = http_request_get_path_param(request, "namespace");
  
  if (pod_name == NULL || namespace == NULL) {
    return http_response_error(response, 400,
      "Bad Request", "Missing pod name or namespace");
  }
  
  /* Create subscription for this specific pod */
  int64_t resource_version = 0;
  const char *rv_param = http_request_get_query_param(request, "resourceVersion");
  if (rv_param != NULL) {
    resource_version = strtoll(rv_param, NULL, 10);
  }
  
  watch_subscription_t *sub = watch_manager_subscribe(g_watch_manager, resource_version);
  if (sub == NULL) {
    return http_response_error(response, 500,
      "Internal Server Error", "Failed to create watch subscription");
  }
  
  /* Add filters for specific pod */
  watch_subscription_add_filter(sub, FILTER_TYPE_FIELD, "metadata.name", pod_name);
  watch_subscription_add_filter(sub, FILTER_TYPE_FIELD, "metadata.namespace", namespace);
  
  /* Set up HTTP response for streaming */
  response->status_code = 200;
  response->content_type = "application/x-ndjson";
  response->chunked = 1;
  
  http_response_send_headers(response);
  
  /* Send BOOKMARK event */
  watch_event_t *bookmark = watch_event_create_bookmark(resource_version);
  char *ndjson = watch_event_to_ndjson(bookmark);
  http_response_write(response, ndjson);
  free(ndjson);
  watch_event_free(bookmark);
  
  /* Stream events for this pod */
  while (!sub->closed) {
    watch_event_t *event = watch_manager_get_event(g_watch_manager, sub, 30000);
    
    if (event == NULL) {
      /* Keep-alive BOOKMARK */
      int64_t current_rv = 0;
      int subscriptions = 0;
      int buffer_usage = 0;
      watch_manager_get_stats(g_watch_manager, &current_rv, &subscriptions, &buffer_usage);
      
      watch_event_t *keep_alive = watch_event_create_bookmark(current_rv);
      char *ndjson = watch_event_to_ndjson(keep_alive);
      if (http_response_write(response, ndjson) < 0) {
        free(ndjson);
        watch_event_free(keep_alive);
        break;
      }
      free(ndjson);
      watch_event_free(keep_alive);
      continue;
    }
    
    char *ndjson = watch_event_to_ndjson(event);
    if (http_response_write(response, ndjson) < 0) {
      free(ndjson);
      watch_event_free(event);
      break;
    }
    free(ndjson);
    watch_event_free(event);
  }
  
  watch_manager_unsubscribe(g_watch_manager, sub);
  
  return 0;
}

/* === Integration with CRUD Endpoints === */

/**
 * Call this after creating a pod to notify watchers
 */
void watch_notify_pod_created(const k8s_pod_t *pod) {
  if (g_watch_manager == NULL || pod == NULL) {
    return;
  }
  
  json_object *pod_json = k8s_pod_to_json(pod);
  watch_manager_publish_event(g_watch_manager, WATCH_EVENT_ADDED, pod_json,
                               pod->metadata.resource_version);
  json_object_put(pod_json);
}

/**
 * Call this after updating a pod to notify watchers
 */
void watch_notify_pod_modified(const k8s_pod_t *pod) {
  if (g_watch_manager == NULL || pod == NULL) {
    return;
  }
  
  json_object *pod_json = k8s_pod_to_json(pod);
  watch_manager_publish_event(g_watch_manager, WATCH_EVENT_MODIFIED, pod_json,
                               pod->metadata.resource_version);
  json_object_put(pod_json);
}

/**
 * Call this after deleting a pod to notify watchers
 */
void watch_notify_pod_deleted(const k8s_pod_t *pod) {
  if (g_watch_manager == NULL || pod == NULL) {
    return;
  }
  
  json_object *pod_json = k8s_pod_to_json(pod);
  watch_manager_publish_event(g_watch_manager, WATCH_EVENT_DELETED, pod_json,
                               pod->metadata.resource_version);
  json_object_put(pod_json);
}

/**
 * Send periodic BOOKMARK events to all watchers
 * This allows clients to perform reconnections without losing state
 */
int watch_send_bookmarks(void) {
  if (g_watch_manager == NULL) {
    return -1;
  }
  
  /* Get current global resource version */
  int64_t current_rv = 0;
  int subscriptions = 0;
  int buffer_usage = 0;
  
  if (watch_manager_get_stats(g_watch_manager, &current_rv, &subscriptions,
                               &buffer_usage) < 0) {
    return -1;
  }
  
  /* Publish bookmark to all watchers */
  return watch_manager_publish_bookmark(g_watch_manager, current_rv);
}

/* === Health Check for Watch Service === */

int endpoint_watch_health(http_request_t *request, http_response_t *response) {
  if (g_watch_manager == NULL) {
    return http_response_json(response, 503,
      "{\"status\":\"unhealthy\",\"message\":\"Watch service not initialized\"}");
  }
  
  int64_t total_events = 0;
  int subscriptions = 0;
  int buffer_usage = 0;
  
  if (watch_manager_get_stats(g_watch_manager, &total_events, &subscriptions,
                               &buffer_usage) < 0) {
    return http_response_json(response, 503,
      "{\"status\":\"unhealthy\",\"message\":\"Failed to get watch statistics\"}");
  }
  
  char status_json[512];
  snprintf(status_json, sizeof(status_json),
    "{\"status\":\"healthy\","
    "\"totalEvents\":%lld,"
    "\"activeSubscriptions\":%d,"
    "\"bufferUsage\":%d,"
    "\"timestamp\":\"%ld\"}",
    (long long)total_events, subscriptions, buffer_usage, time(NULL));
  
  return http_response_json(response, 200, status_json);
}
