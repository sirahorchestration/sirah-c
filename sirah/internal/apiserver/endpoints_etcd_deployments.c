#include "endpoints_etcd_deployments.h"
#include "../../../internal/etcd/etcd_manager.h"
#include "../../pkg/types/deployment.h"
#include "http.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <json-c/json.h>

/* === Deployment CRUD Endpoints (etcd-backed) === */

/**
 * POST /api/v1/namespaces/{namespace}/deployments
 * Create a new deployment
 */
int endpoint_create_deployment_etcd(http_request_t *request,
                                     http_response_t *response) {
  const char *namespace = http_request_get_path_param(request, "namespace");
  if (namespace == NULL) {
    return http_response_error(response, 400, "Bad Request",
      "Namespace parameter required");
  }
  
  /* Parse request body */
  const char *body = http_request_get_body(request);
  if (body == NULL) {
    return http_response_error(response, 400, "Bad Request",
      "Request body required");
  }
  
  json_object *deployment_json = json_tokener_parse(body);
  if (deployment_json == NULL) {
    return http_response_error(response, 400, "Bad Request",
      "Invalid JSON");
  }
  
  /* Parse deployment from JSON */
  k8s_deployment_t *deployment = k8s_deployment_from_json(deployment_json);
  if (deployment == NULL) {
    json_object_put(deployment_json);
    return http_response_error(response, 400, "Bad Request",
      "Invalid deployment specification");
  }
  
  /* Generate resource version */
  deployment->metadata.resource_version = (int64_t)time(NULL);
  
  /* Store in etcd */
  char etcd_key[512];
  snprintf(etcd_key, sizeof(etcd_key),
    "/sirah/deployments/%s/%s", namespace, deployment->metadata.name);
  
  json_object *deployment_stored = k8s_deployment_to_json(deployment);
  const char *deployment_json_str = json_object_to_json_string(deployment_stored);
  
  etcd_manager_t *em = etcd_manager_get_instance();
  if (em == NULL) {
    return http_response_error(response, 503, "Service Unavailable",
      "etcd connection failed");
  }
  
  if (etcd_manager_set(em, etcd_key, deployment_json_str) != 0) {
    json_object_put(deployment_stored);
    k8s_deployment_free(deployment);
    json_object_put(deployment_json);
    return http_response_error(response, 500, "Internal Server Error",
      "Failed to store deployment");
  }
  
  /* Return created deployment with 201 status */
  http_response_set_status(response, 201);
  http_response_set_header(response, "Content-Type", "application/json");
  http_response_send_headers(response);
  
  const char *response_json = json_object_to_json_string(deployment_stored);
  http_response_write(response, response_json);
  
  json_object_put(deployment_stored);
  json_object_put(deployment_json);
  k8s_deployment_free(deployment);
  
  return 0;
}

/**
 * GET /api/v1/namespaces/{namespace}/deployments/{name}
 * Get a specific deployment
 */
int endpoint_get_deployment_etcd(http_request_t *request,
                                  http_response_t *response) {
  const char *namespace = http_request_get_path_param(request, "namespace");
  const char *name = http_request_get_path_param(request, "name");
  
  if (namespace == NULL || name == NULL) {
    return http_response_error(response, 400, "Bad Request",
      "Namespace and name parameters required");
  }
  
  char etcd_key[512];
  snprintf(etcd_key, sizeof(etcd_key),
    "/sirah/deployments/%s/%s", namespace, name);
  
  etcd_manager_t *em = etcd_manager_get_instance();
  if (em == NULL) {
    return http_response_error(response, 503, "Service Unavailable",
      "etcd connection failed");
  }
  
  etcd_response_t etcd_resp;
  if (etcd_manager_get(em, etcd_key, &etcd_resp) != 0) {
    return http_response_error(response, 500, "Internal Server Error",
      "Failed to retrieve deployment");
  }
  
  if (etcd_resp.found == 0) {
    return http_response_error(response, 404, "Not Found",
      "Deployment not found");
  }
  
  /* Parse and return deployment */
  json_object *deployment_json = json_tokener_parse(etcd_resp.value);
  if (deployment_json == NULL) {
    etcd_response_free(&etcd_resp);
    return http_response_error(response, 500, "Internal Server Error",
      "Invalid stored deployment");
  }
  
  http_response_set_status(response, 200);
  http_response_set_header(response, "Content-Type", "application/json");
  http_response_send_headers(response);
  
  const char *response_json = json_object_to_json_string(deployment_json);
  http_response_write(response, response_json);
  
  json_object_put(deployment_json);
  etcd_response_free(&etcd_resp);
  
  return 0;
}

/**
 * GET /api/v1/namespaces/{namespace}/deployments
 * List all deployments in namespace
 */
int endpoint_list_deployments_etcd(http_request_t *request,
                                    http_response_t *response) {
  const char *namespace = http_request_get_path_param(request, "namespace");
  if (namespace == NULL) {
    return http_response_error(response, 400, "Bad Request",
      "Namespace parameter required");
  }
  
  char etcd_prefix[512];
  snprintf(etcd_prefix, sizeof(etcd_prefix),
    "/sirah/deployments/%s/", namespace);
  
  etcd_manager_t *em = etcd_manager_get_instance();
  if (em == NULL) {
    return http_response_error(response, 503, "Service Unavailable",
      "etcd connection failed");
  }
  
  etcd_response_t etcd_resp;
  if (etcd_manager_get_prefix(em, etcd_prefix, &etcd_resp) != 0) {
    return http_response_error(response, 500, "Internal Server Error",
      "Failed to list deployments");
  }
  
  /* Build items array */
  json_object *items_array = json_object_new_array();
  
  for (int i = 0; i < etcd_resp.kvs_count; i++) {
    json_object *deployment_json = json_tokener_parse(etcd_resp.kvs_values[i]);
    if (deployment_json != NULL) {
      /* Add resourceVersion from etcd */
      json_object_object_add(deployment_json, "resourceVersion",
        json_object_new_int64(etcd_resp.kvs_modrevisions[i]));
      json_object_array_add(items_array, deployment_json);
    }
  }
  
  /* Build response */
  json_object *response_obj = json_object_new_object();
  json_object_object_add(response_obj, "apiVersion",
    json_object_new_string("apps/v1"));
  json_object_object_add(response_obj, "kind",
    json_object_new_string("DeploymentList"));
  json_object_object_add(response_obj, "items", items_array);
  
  http_response_set_status(response, 200);
  http_response_set_header(response, "Content-Type", "application/json");
  http_response_send_headers(response);
  
  const char *response_json = json_object_to_json_string(response_obj);
  http_response_write(response, response_json);
  
  json_object_put(response_obj);
  etcd_response_free(&etcd_resp);
  
  return 0;
}

/**
 * PATCH /api/v1/namespaces/{namespace}/deployments/{name}
 * Update deployment with CAS (Compare-And-Swap)
 */
int endpoint_patch_deployment_etcd(http_request_t *request,
                                    http_response_t *response) {
  const char *namespace = http_request_get_path_param(request, "namespace");
  const char *name = http_request_get_path_param(request, "name");
  
  if (namespace == NULL || name == NULL) {
    return http_response_error(response, 400, "Bad Request",
      "Namespace and name parameters required");
  }
  
  /* Parse patch body */
  const char *body = http_request_get_body(request);
  if (body == NULL) {
    return http_response_error(response, 400, "Bad Request",
      "Request body required");
  }
  
  json_object *patch_json = json_tokener_parse(body);
  if (patch_json == NULL) {
    return http_response_error(response, 400, "Bad Request",
      "Invalid JSON");
  }
  
  /* Extract resourceVersion from patch (for CAS) */
  int64_t patch_revision = 0;
  json_object *metadata_obj = json_object_object_get(patch_json, "metadata");
  if (metadata_obj != NULL) {
    json_object *rv_obj = json_object_object_get(metadata_obj, "resourceVersion");
    if (rv_obj != NULL) {
      patch_revision = json_object_get_int64(rv_obj);
    }
  }
  
  char etcd_key[512];
  snprintf(etcd_key, sizeof(etcd_key),
    "/sirah/deployments/%s/%s", namespace, name);
  
  /* Get current version for CAS comparison */
  etcd_manager_t *em = etcd_manager_get_instance();
  if (em == NULL) {
    json_object_put(patch_json);
    return http_response_error(response, 503, "Service Unavailable",
      "etcd connection failed");
  }
  
  etcd_response_t etcd_resp;
  if (etcd_manager_get(em, etcd_key, &etcd_resp) != 0) {
    json_object_put(patch_json);
    return http_response_error(response, 500, "Internal Server Error",
      "Failed to retrieve deployment");
  }
  
  if (etcd_resp.found == 0) {
    json_object_put(patch_json);
    return http_response_error(response, 404, "Not Found",
      "Deployment not found");
  }
  
  int64_t current_revision = etcd_resp.modrevision;
  
  /* CAS validation */
  if (patch_revision == 0 || patch_revision != current_revision) {
    json_object_put(patch_json);
    etcd_response_free(&etcd_resp);
    
    return http_response_json(response, 409,
      "{\"kind\":\"Status\",\"apiVersion\":\"v1\",\"metadata\":{},"
      "\"status\":\"Failure\",\"message\":\"Conflict\","
      "\"reason\":\"Conflict\",\"code\":409}");
  }
  
  /* Parse current deployment and apply patch */
  json_object *current_json = json_tokener_parse(etcd_resp.value);
  if (current_json == NULL) {
    json_object_put(patch_json);
    etcd_response_free(&etcd_resp);
    return http_response_error(response, 500, "Internal Server Error",
      "Invalid stored deployment");
  }
  
  /* Merge patch into current */
  json_object_iter iter;
  json_object_object_foreachC(patch_json, iter) {
    json_object_object_add(current_json, iter.key, json_object_get(iter.val));
  }
  
  /* Increment generation if spec changed */
  json_object_object_add(current_json, "generation",
    json_object_new_int64((int64_t)time(NULL)));
  
  /* Update resourceVersion */
  int64_t new_revision = (int64_t)time(NULL);
  json_object_object_add(current_json, "resourceVersion",
    json_object_new_int64(new_revision));
  
  /* Store updated deployment */
  const char *updated_json_str = json_object_to_json_string(current_json);
  if (etcd_manager_set(em, etcd_key, updated_json_str) != 0) {
    json_object_put(current_json);
    json_object_put(patch_json);
    etcd_response_free(&etcd_resp);
    return http_response_error(response, 500, "Internal Server Error",
      "Failed to update deployment");
  }
  
  /* Return updated deployment */
  http_response_set_status(response, 200);
  http_response_set_header(response, "Content-Type", "application/json");
  http_response_send_headers(response);
  http_response_write(response, updated_json_str);
  
  json_object_put(current_json);
  json_object_put(patch_json);
  etcd_response_free(&etcd_resp);
  
  return 0;
}

/**
 * DELETE /api/v1/namespaces/{namespace}/deployments/{name}
 * Delete a deployment
 */
int endpoint_delete_deployment_etcd(http_request_t *request,
                                     http_response_t *response) {
  const char *namespace = http_request_get_path_param(request, "namespace");
  const char *name = http_request_get_path_param(request, "name");
  
  if (namespace == NULL || name == NULL) {
    return http_response_error(response, 400, "Bad Request",
      "Namespace and name parameters required");
  }
  
  char etcd_key[512];
  snprintf(etcd_key, sizeof(etcd_key),
    "/sirah/deployments/%s/%s", namespace, name);
  
  etcd_manager_t *em = etcd_manager_get_instance();
  if (em == NULL) {
    return http_response_error(response, 503, "Service Unavailable",
      "etcd connection failed");
  }
  
  if (etcd_manager_delete(em, etcd_key) != 0) {
    return http_response_error(response, 500, "Internal Server Error",
      "Failed to delete deployment");
  }
  
  http_response_set_status(response, 204);
  http_response_send_headers(response);
  
  return 0;
}
