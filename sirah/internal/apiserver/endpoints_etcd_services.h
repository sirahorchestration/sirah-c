#ifndef ENDPOINTS_ETCD_SERVICES_H
#define ENDPOINTS_ETCD_SERVICES_H

#include "http.h"

/*
 * Service CRUD Endpoints (etcd-backed)
 * Implements Kubernetes Service API with persistent etcd storage
 * All operations support CAS (Compare-And-Swap) via resourceVersion
 */

/**
 * POST /api/v1/namespaces/{namespace}/services
 * Create a new service
 * Returns: 201 Created with service data
 */
int endpoint_create_service_etcd(http_request_t *request,
                                  http_response_t *response);

/**
 * GET /api/v1/namespaces/{namespace}/services/{name}
 * Get a specific service
 * Returns: 200 OK with service data, 404 if not found
 */
int endpoint_get_service_etcd(http_request_t *request,
                               http_response_t *response);

/**
 * GET /api/v1/namespaces/{namespace}/services
 * List all services in a namespace
 * Returns: 200 OK with service array
 */
int endpoint_list_services_etcd(http_request_t *request,
                                 http_response_t *response);

/**
 * PATCH /api/v1/namespaces/{namespace}/services/{name}
 * Update service with optimistic locking (CAS)
 * Returns: 200 OK on success, 409 Conflict if resourceVersion mismatch
 */
int endpoint_patch_service_etcd(http_request_t *request,
                                 http_response_t *response);

/**
 * DELETE /api/v1/namespaces/{namespace}/services/{name}
 * Delete a service
 * Returns: 204 No Content
 */
int endpoint_delete_service_etcd(http_request_t *request,
                                  http_response_t *response);

#endif /* ENDPOINTS_ETCD_SERVICES_H */
