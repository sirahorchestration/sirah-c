#ifndef WATCH_HANDLER_H
#define WATCH_HANDLER_H

#include "../http/http.h"
#include "../../watch/watch_manager.h"
#include "../../pkg/types/pod.h"

/*
 * Watch HTTP Handler
 * 
 * Provides HTTP endpoints for real-time pod updates via streaming.
 * Integration point between watch_manager and HTTP API.
 */

/**
 * Initialize watch handler with global watch manager
 * Call once at API server startup
 */
void watch_handler_init(watch_manager_t *wm);

/**
 * Cleanup watch handler
 * Call at API server shutdown
 */
void watch_handler_cleanup(void);

/* === Watch Endpoints === */

/**
 * GET /api/v1/namespaces/{namespace}/pods?watch=true
 * 
 * Stream pod events in NDJSON format.
 * Supports label and field selectors for filtering.
 * Supports resource version for reconnection.
 */
int endpoint_watch_pods(http_request_t *request, http_response_t *response);

/**
 * GET /api/v1/namespaces/{namespace}/pods/{name}?watch=true
 * 
 * Watch a single pod for changes.
 */
int endpoint_watch_pod(http_request_t *request, http_response_t *response);

/**
 * GET /api/v1/watch-health
 * 
 * Check health of watch service.
 * Returns statistics about buffer usage and active subscriptions.
 */
int endpoint_watch_health(http_request_t *request, http_response_t *response);

/* === Integration with CRUD Endpoints === */

/**
 * Notify all watchers that a pod was created
 * Call from endpoint_create_pod_etcd() after successful creation
 */
void watch_notify_pod_created(const k8s_pod_t *pod);

/**
 * Notify all watchers that a pod was modified
 * Call from endpoint_patch_pod_etcd() after successful update
 */
void watch_notify_pod_modified(const k8s_pod_t *pod);

/**
 * Notify all watchers that a pod was deleted
 * Call from endpoint_delete_pod_etcd() after successful deletion
 */
void watch_notify_pod_deleted(const k8s_pod_t *pod);

/**
 * Send periodic BOOKMARK events to all subscribers
 * Should be called periodically (e.g., every 30 seconds) by a timer thread
 * Allows clients to handle reconnection gracefully
 */
int watch_send_bookmarks(void);

#endif /* WATCH_HANDLER_H */
