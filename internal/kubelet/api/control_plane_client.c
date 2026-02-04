/*
 * control_plane_client.c
 * 
 * Implementation of control plane client for agent-to-API server communication
 */

#include "control_plane_client.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <curl/curl.h>
#include <json-c/json.h>

/**
 * HTTP response buffer for curl callbacks
 */
typedef struct {
    char *buffer;
    size_t capacity;
    size_t size;
} http_response_t;

/**
 * Curl write callback to capture response body
 */
static size_t curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    http_response_t *resp = (http_response_t *)userp;
    
    if (resp->size + realsize >= resp->capacity) {
        // Double capacity until we have enough room
        while (resp->size + realsize >= resp->capacity) {
            resp->capacity = resp->capacity == 0 ? 1024 : resp->capacity * 2;
        }
        
        char *newbuffer = realloc(resp->buffer, resp->capacity);
        if (!newbuffer) {
            fprintf(stderr, "Not enough memory for HTTP response\n");
            return 0;
        }
        resp->buffer = newbuffer;
    }
    
    memcpy(&(resp->buffer[resp->size]), contents, realsize);
    resp->size += realsize;
    resp->buffer[resp->size] = 0;
    
    return realsize;
}

/**
 * Free HTTP response buffer
 */
static void http_response_free(http_response_t *resp) {
    if (resp && resp->buffer) {
        free(resp->buffer);
    }
}

/**
 * Make HTTP GET request to control plane
 */
static bool make_http_get(const char *url, http_response_t *out_response) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return false;
    }
    
    http_response_t response = {0};
    response.capacity = 4096;
    response.buffer = malloc(response.capacity);
    if (!response.buffer) {
        curl_easy_cleanup(curl);
        return false;
    }
    
    // Set curl options
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
    
    // Add User-Agent header
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "User-Agent: sirah-kubelet/1.0");
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    bool success = (res == CURLE_OK);
    
    if (success) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        success = (http_code >= 200 && http_code < 300);
    }
    
    if (success) {
        out_response->buffer = response.buffer;
        out_response->size = response.size;
        out_response->capacity = response.capacity;
    } else {
        http_response_free(&response);
    }
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return success;
}

/**
 * Make HTTP POST request to control plane
 */
static bool make_http_post(const char *url, const char *json_data, http_response_t *out_response) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return false;
    }
    
    http_response_t response = {0};
    response.capacity = 4096;
    response.buffer = malloc(response.capacity);
    if (!response.buffer) {
        curl_easy_cleanup(curl);
        return false;
    }
    
    // Set curl options
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
    
    // Add headers
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "User-Agent: sirah-kubelet/1.0");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    bool success = (res == CURLE_OK);
    
    if (success) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        success = (http_code >= 200 && http_code < 300);
    }
    
    if (success) {
        out_response->buffer = response.buffer;
        out_response->size = response.size;
        out_response->capacity = response.capacity;
    } else {
        http_response_free(&response);
    }
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return success;
}

/**
 * Make HTTP PATCH request to control plane
 */
static bool make_http_patch(const char *url, const char *json_data, http_response_t *out_response) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return false;
    }
    
    http_response_t response = {0};
    response.capacity = 4096;
    response.buffer = malloc(response.capacity);
    if (!response.buffer) {
        curl_easy_cleanup(curl);
        return false;
    }
    
    // Set curl options
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
    
    // Add headers
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "User-Agent: sirah-kubelet/1.0");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    bool success = (res == CURLE_OK);
    
    if (success) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        success = (http_code >= 200 && http_code < 300);
    }
    
    if (success) {
        out_response->buffer = response.buffer;
        out_response->size = response.size;
        out_response->capacity = response.capacity;
    } else {
        http_response_free(&response);
    }
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return success;
}

/**
 * Create control plane client
 */
control_plane_client_t* control_plane_client_new(const control_plane_client_config_t *config) {
    if (!config || !config->api_server_url || !config->node_name) {
        return NULL;
    }
    
    control_plane_client_t *client = malloc(sizeof(control_plane_client_t));
    if (!client) {
        return NULL;
    }
    
    memset(client, 0, sizeof(control_plane_client_t));
    
    // Copy config
    client->config.api_server_url = strdup(config->api_server_url);
    client->config.node_name = strdup(config->node_name);
    client->config.heartbeat_interval_seconds = config->heartbeat_interval_seconds;
    client->config.pod_sync_interval_seconds = config->pod_sync_interval_seconds;
    client->config.initial_retry_delay_ms = config->initial_retry_delay_ms;
    client->config.max_retry_delay_ms = config->max_retry_delay_ms;
    client->config.registration_timeout_seconds = config->registration_timeout_seconds;
    client->config.use_circuit_breaker = config->use_circuit_breaker;
    
    if (!client->config.api_server_url || !client->config.node_name) {
        free(client->config.api_server_url);
        free(client->config.node_name);
        free(client);
        return NULL;
    }
    
    // Initialize mutex
    if (pthread_mutex_init(&client->mutex, NULL) != 0) {
        free(client->config.api_server_url);
        free(client->config.node_name);
        free(client);
        return NULL;
    }
    
    // Initialize assignment capacity
    client->assignment_capacity = 32;
    client->assignments = malloc(sizeof(pod_assignment_t) * client->assignment_capacity);
    if (!client->assignments) {
        pthread_mutex_destroy(&client->mutex);
        free(client->config.api_server_url);
        free(client->config.node_name);
        free(client);
        return NULL;
    }
    
    return client;
}

/**
 * Free control plane client
 */
void control_plane_client_free(control_plane_client_t *client) {
    if (!client) return;
    
    pthread_mutex_destroy(&client->mutex);
    free(client->config.api_server_url);
    free(client->config.node_name);
    
    // Free assignments
    for (uint32_t i = 0; i < client->assignment_count; i++) {
        free(client->assignments[i].pod_name);
        free(client->assignments[i].pod_namespace);
        free(client->assignments[i].pod_uid);
        free(client->assignments[i].image);
        free(client->assignments[i].image_pull_policy);
        free(client->assignments[i].pod_spec_json);
    }
    free(client->assignments);
    
    free(client);
}

/**
 * Register this node with control plane
 */
bool control_plane_client_register_node(control_plane_client_t *client,
                                       const node_registration_request_t *req) {
    if (!client || !req || !req->node_name || !req->node_id) {
        return false;
    }
    
    // Build node registration JSON
    json_object *node_obj = json_object_new_object();
    json_object *metadata = json_object_new_object();
    json_object *status = json_object_new_object();
    
    // Metadata
    json_object_object_add(metadata, "name", json_object_new_string(req->node_name));
    json_object_object_add(metadata, "labels", json_tokener_parse(req->labels_json ? req->labels_json : "{}"));
    
    // Status with allocatable resources
    json_object *allocatable = json_object_new_object();
    char cpu_str[64], memory_str[64];
    snprintf(cpu_str, sizeof(cpu_str), "%um", req->allocatable_cpu_cores);
    snprintf(memory_str, sizeof(memory_str), "%luBi", req->allocatable_memory_bytes);
    json_object_object_add(allocatable, "cpu", json_object_new_string(cpu_str));
    json_object_object_add(allocatable, "memory", json_object_new_string(memory_str));
    json_object_object_add(status, "allocatable", allocatable);
    
    // Pod CIDR
    if (req->pod_cidr) {
        json_object_object_add(status, "podCIDR", json_object_new_string(req->pod_cidr));
    }
    
    // Node object
    json_object_object_add(node_obj, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(node_obj, "kind", json_object_new_string("Node"));
    json_object_object_add(node_obj, "metadata", metadata);
    json_object_object_add(node_obj, "status", status);
    
    const char *json_str = json_object_to_json_string(node_obj);
    if (!json_str) {
        json_object_put(node_obj);
        return false;
    }
    
    // Build URL
    char url[512];
    snprintf(url, sizeof(url), "%s/api/v1/nodes", client->config.api_server_url);
    
    // Send registration request
    http_response_t response = {0};
    bool success = make_http_post(url, json_str, &response);
    
    if (success) {
        pthread_mutex_lock(&client->mutex);
        client->registered = true;
        client->registration_time = time(NULL);
        client->total_requests++;
        client->successful_requests++;
        pthread_mutex_unlock(&client->mutex);
    } else {
        pthread_mutex_lock(&client->mutex);
        client->total_requests++;
        client->failed_requests++;
        pthread_mutex_unlock(&client->mutex);
    }
    
    http_response_free(&response);
    json_object_put(node_obj);
    
    return success;
}

/**
 * Send heartbeat to control plane
 */
bool control_plane_client_send_heartbeat(control_plane_client_t *client,
                                         const heartbeat_request_t *req) {
    if (!client || !req || !req->node_name) {
        return false;
    }
    
    // Build heartbeat JSON
    json_object *update_obj = json_object_new_object();
    json_object *status = json_object_new_object();
    
    // Status fields
    json_object_object_add(status, "phase", json_object_new_string(req->status));
    if (req->status_message) {
        json_object_object_add(status, "message", json_object_new_string(req->status_message));
    }
    json_object_object_add(status, "podCount", json_object_new_int(req->pod_count));
    json_object_object_add(status, "timestamp", json_object_new_int64((int64_t)req->timestamp));
    
    // Allocatable resources
    json_object *allocatable = json_object_new_object();
    char cpu_str[64], memory_str[64];
    snprintf(cpu_str, sizeof(cpu_str), "%um", req->allocatable_cpu_cores);
    snprintf(memory_str, sizeof(memory_str), "%luBi", req->allocatable_memory_bytes);
    json_object_object_add(allocatable, "cpu", json_object_new_string(cpu_str));
    json_object_object_add(allocatable, "memory", json_object_new_string(memory_str));
    json_object_object_add(status, "allocatable", allocatable);
    
    json_object_object_add(update_obj, "status", status);
    
    const char *json_str = json_object_to_json_string(update_obj);
    if (!json_str) {
        json_object_put(update_obj);
        return false;
    }
    
    // Build URL
    char url[512];
    snprintf(url, sizeof(url), "%s/api/v1/nodes/%s/status", 
             client->config.api_server_url, req->node_name);
    
    // Send heartbeat
    http_response_t response = {0};
    bool success = make_http_patch(url, json_str, &response);
    
    if (success) {
        pthread_mutex_lock(&client->mutex);
        client->last_heartbeat = time(NULL);
        client->failed_heartbeats = 0;
        client->total_requests++;
        client->successful_requests++;
        pthread_mutex_unlock(&client->mutex);
    } else {
        pthread_mutex_lock(&client->mutex);
        client->failed_heartbeats++;
        client->total_requests++;
        client->failed_requests++;
        pthread_mutex_unlock(&client->mutex);
    }
    
    http_response_free(&response);
    json_object_put(update_obj);
    
    return success;
}

/**
 * Poll for new pod assignments
 */
bool control_plane_client_poll_assignments(control_plane_client_t *client,
                                           pod_assignment_t **out_assignments,
                                           uint32_t *out_count) {
    if (!client || !out_assignments || !out_count) {
        return false;
    }
    
    // Build URL to list pods assigned to this node
    char url[512];
    snprintf(url, sizeof(url), "%s/api/v1/namespaces/default/pods?fieldSelector=spec.nodeName=%s",
             client->config.api_server_url, client->config.node_name);
    
    // Get pod list
    http_response_t response = {0};
    bool success = make_http_get(url, &response);
    
    if (success && response.buffer) {
        // Parse response JSON
        json_object *pods_obj = json_tokener_parse(response.buffer);
        if (pods_obj) {
            json_object *items = NULL;
            if (json_object_object_get_ex(pods_obj, "items", &items) && json_object_is_type(items, json_type_array)) {
                int pod_count = json_object_array_length(items);
                
                // Allocate assignment array
                pod_assignment_t *new_assignments = malloc(sizeof(pod_assignment_t) * pod_count);
                if (new_assignments) {
                    int new_count = 0;
                    
                    for (int i = 0; i < pod_count; i++) {
                        json_object *pod = json_object_array_get_idx(items, i);
                        json_object *metadata = NULL, *spec = NULL;
                        
                        if (json_object_object_get_ex(pod, "metadata", &metadata) &&
                            json_object_object_get_ex(pod, "spec", &spec)) {
                            
                            json_object *name_obj = NULL, *namespace_obj = NULL, *uid_obj = NULL;
                            json_object_object_get_ex(metadata, "name", &name_obj);
                            json_object_object_get_ex(metadata, "namespace", &namespace_obj);
                            json_object_object_get_ex(metadata, "uid", &uid_obj);
                            
                            if (name_obj && namespace_obj && uid_obj) {
                                memset(&new_assignments[new_count], 0, sizeof(pod_assignment_t));
                                new_assignments[new_count].pod_name = strdup(json_object_get_string(name_obj));
                                new_assignments[new_count].pod_namespace = strdup(json_object_get_string(namespace_obj));
                                new_assignments[new_count].pod_uid = strdup(json_object_get_string(uid_obj));
                                new_assignments[new_count].image = strdup("unknown");
                                new_assignments[new_count].image_pull_policy = strdup("IfNotPresent");
                                new_assignments[new_count].pod_spec_json = strdup(json_object_to_json_string(spec));
                                
                                // Extract container image if present
                                json_object *containers = NULL;
                                if (json_object_object_get_ex(spec, "containers", &containers) &&
                                    json_object_is_type(containers, json_type_array) &&
                                    json_object_array_length(containers) > 0) {
                                    
                                    json_object *container = json_object_array_get_idx(containers, 0);
                                    json_object *image_obj = NULL;
                                    if (json_object_object_get_ex(container, "image", &image_obj)) {
                                        free(new_assignments[new_count].image);
                                        new_assignments[new_count].image = strdup(json_object_get_string(image_obj));
                                    }
                                }
                                
                                new_count++;
                            }
                        }
                    }
                    
                    *out_assignments = new_assignments;
                    *out_count = new_count;
                    
                    pthread_mutex_lock(&client->mutex);
                    client->total_requests++;
                    client->successful_requests++;
                    pthread_mutex_unlock(&client->mutex);
                } else {
                    success = false;
                }
            }
            json_object_put(pods_obj);
        }
    } else {
        pthread_mutex_lock(&client->mutex);
        client->total_requests++;
        client->failed_requests++;
        pthread_mutex_unlock(&client->mutex);
    }
    
    http_response_free(&response);
    return success;
}

/**
 * Report pod status to control plane
 */
bool control_plane_client_update_pod_status(control_plane_client_t *client,
                                            const char *pod_name,
                                            const char *pod_namespace,
                                            const char *phase,
                                            const char *container_state) {
    if (!client || !pod_name || !pod_namespace || !phase) {
        return false;
    }
    
    // Build status update JSON
    json_object *update_obj = json_object_new_object();
    json_object *status = json_object_new_object();
    
    json_object_object_add(status, "phase", json_object_new_string(phase));
    json_object_object_add(status, "timestamp", json_object_new_int64((int64_t)time(NULL)));
    
    if (container_state) {
        json_object *container_status = json_tokener_parse(container_state);
        if (container_status) {
            json_object_object_add(status, "containerStatuses", container_status);
        }
    }
    
    json_object_object_add(update_obj, "status", status);
    
    const char *json_str = json_object_to_json_string(update_obj);
    if (!json_str) {
        json_object_put(update_obj);
        return false;
    }
    
    // Build URL
    char url[512];
    snprintf(url, sizeof(url), "%s/api/v1/namespaces/%s/pods/%s/status",
             client->config.api_server_url, pod_namespace, pod_name);
    
    // Send status update
    http_response_t response = {0};
    bool success = make_http_patch(url, json_str, &response);
    
    if (success) {
        pthread_mutex_lock(&client->mutex);
        client->total_requests++;
        client->successful_requests++;
        pthread_mutex_unlock(&client->mutex);
    } else {
        pthread_mutex_lock(&client->mutex);
        client->total_requests++;
        client->failed_requests++;
        pthread_mutex_unlock(&client->mutex);
    }
    
    http_response_free(&response);
    json_object_put(update_obj);
    
    return success;
}

/**
 * Check for pod deletion requests
 */
bool control_plane_client_check_deletions(control_plane_client_t *client,
                                          char **out_pod_names,
                                          uint32_t *out_count) {
    if (!client || !out_pod_names || !out_count) {
        return false;
    }
    
    // For now, no deletions
    *out_count = 0;
    *out_pod_names = NULL;
    
    pthread_mutex_lock(&client->mutex);
    client->total_requests++;
    pthread_mutex_unlock(&client->mutex);
    
    return true;
}

/**
 * Check if client is registered with control plane
 */
bool control_plane_client_is_registered(control_plane_client_t *client) {
    if (!client) return false;
    
    pthread_mutex_lock(&client->mutex);
    bool registered = client->registered;
    pthread_mutex_unlock(&client->mutex);
    
    return registered;
}

/**
 * Get connection statistics
 */
void control_plane_client_get_stats(control_plane_client_t *client,
                                    uint32_t *out_total,
                                    uint32_t *out_success,
                                    uint32_t *out_failures) {
    if (!client) return;
    
    pthread_mutex_lock(&client->mutex);
    if (out_total) *out_total = client->total_requests;
    if (out_success) *out_success = client->successful_requests;
    if (out_failures) *out_failures = client->failed_requests;
    pthread_mutex_unlock(&client->mutex);
}

/**
 * Free pod assignment array
 */
void control_plane_client_free_assignments(pod_assignment_t *assignments, uint32_t count) {
    if (!assignments) return;
    
    for (uint32_t i = 0; i < count; i++) {
        free(assignments[i].pod_name);
        free(assignments[i].pod_namespace);
        free(assignments[i].pod_uid);
        free(assignments[i].image);
        free(assignments[i].image_pull_policy);
        free(assignments[i].pod_spec_json);
    }
    free(assignments);
}

/**
 * Free pod name array
 */
void control_plane_client_free_pod_names(char **pod_names, uint32_t count) {
    if (!pod_names) return;
    
    for (uint32_t i = 0; i < count; i++) {
        free(pod_names[i]);
    }
    free(pod_names);
}
