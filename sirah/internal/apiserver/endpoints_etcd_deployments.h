#ifndef ENDPOINTS_ETCD_DEPLOYMENTS_H
#define ENDPOINTS_ETCD_DEPLOYMENTS_H

#include "http.h"

/*
 * Deployment CRUD Endpoints (etcd-backed)
 * Implements Kubernetes Deployment API with persistent etcd storage
 * All operations support CAS (Compare-And-Swap) via resourceVersion
 */

/**
 * POST /api/v1/namespaces/{namespace}/deployments
 * Create a new deployment
 * Returns: 201 Created with deployment data
 */
int endpoint_create_deployment_etcd(http_request_t *request,
                                     http_response_t *response);

/**
 * GET /api/v1/namespaces/{namespace}/deployments/{name}
 * Get a specific deployment
 * Returns: 200 OK with deployment data, 404 if not found
 */
int endpoint_get_deployment_etcd(http_request_t *request,
                                  http_response_t *response);

/**
 * GET /api/v1/namespaces/{namespace}/deployments
 * List all deployments in a namespace
 * Returns: 200 OK with deployment array
 */
int endpoint_list_deployments_etcd(http_request_t *request,
                                    http_response_t *response);

/**
 * PATCH /api/v1/namespaces/{namespace}/deployments/{name}
 * Update deployment with optimistic locking (CAS)
 * Increments generation on successful update
 * Returns: 200 OK on success, 409 Conflict if resourceVersion mismatch
 */
int endpoint_patch_deployment_etcd(http_request_t *request,
                                    http_response_t *response);

/**
 * DELETE /api/v1/namespaces/{namespace}/deployments/{name}
 * Delete a deployment
 * Returns: 204 No Content
 */
int endpoint_delete_deployment_etcd(http_request_t *request,
                                     http_response_t *response);

#endif /* ENDPOINTS_ETCD_DEPLOYMENTS_H */
