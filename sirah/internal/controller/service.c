// internal/controller/service.c
// Service Controller Implementation  
// Watches Service and Pod objects, discovers endpoints by matching selectors,
// implements load balancing strategies, and manages endpoint updates dynamically

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <unistd.h>
#include <pthread.h>
#include "service.h"

#define MAX_API_RESPONSE 1048576  // 1MB max response
#define SYNC_INTERVAL 3           // Check services every 3 seconds
#define MAX_ENDPOINTS 1000
#define MAX_TRACKED_SERVICES 100

// HTTP response buffer
typedef struct {
    char* data;
    size_t size;
    size_t capacity;
} http_response_t;

// Tracked service with endpoints cache
typedef struct {
    service_endpoint_config_t config;
    endpoint_t* endpoints[MAX_ENDPOINTS];
    int endpoint_count;
    time_t last_update;
    int round_robin_index;
    int connection_counts[MAX_ENDPOINTS];  // For least-connections strategy
} tracked_service_t;

static tracked_service_t tracked_services[MAX_TRACKED_SERVICES];
static int tracked_service_count = 0;
static pthread_mutex_t service_mutex = PTHREAD_MUTEX_INITIALIZER;

// Curl callback to accumulate response
static size_t write_callback(void* data, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    http_response_t* buf = (http_response_t*)userp;
    
    if (buf->size + realsize >= buf->capacity) {
        return 0;  // Response too large
    }
    
    memcpy(&(buf->data[buf->size]), data, realsize);
    buf->size += realsize;
    buf->data[buf->size] = 0;
    
    return realsize;
}

// Create new service controller
service_controller_t* service_controller_new(const char* api_server_url) {
    if (!api_server_url) return NULL;
    
    service_controller_t* controller = 
        (service_controller_t*)malloc(sizeof(service_controller_t));
    if (!controller) return NULL;
    
    controller->api_server_url = (char*)malloc(strlen(api_server_url) + 1);
    strcpy(controller->api_server_url, api_server_url);
    controller->curl_handle = NULL;
    controller->update_interval = 3;
    controller->load_balance_strategy = LB_ROUND_ROBIN;
    
    return controller;
}

// Free service controller
void service_controller_free(service_controller_t* controller) {
    if (!controller) return;
    free(controller->api_server_url);
    free(controller);
}

// Initialize service controller
int service_controller_init(service_controller_t* controller) {
    if (!controller) return -1;
    
    controller->curl_handle = curl_easy_init();
    if (!controller->curl_handle) {
        fprintf(stderr, "[service-controller] Failed to initialize curl\n");
        return -1;
    }
    
    fprintf(stderr, "[service-controller] Initialized (LB strategy: %d)\n",
            controller->load_balance_strategy);
    return 0;
}

// Shutdown service controller
void service_controller_shutdown(service_controller_t* controller) {
    if (!controller || !controller->curl_handle) return;
    
    curl_easy_cleanup((CURL*)controller->curl_handle);
    controller->curl_handle = NULL;
    fprintf(stderr, "[service-controller] Shutdown\n");
}

// Make HTTP request
static int api_request(service_controller_t* controller, const char* method,
                      const char* path, const char* body, http_response_t* response) {
    if (!controller || !controller->curl_handle) {
        return -1;
    }
    
    CURL* curl = (CURL*)controller->curl_handle;
    
    char url[1024];
    snprintf(url, sizeof(url), "%s%s", controller->api_server_url, path);
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)response);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    if (body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    }
    
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    
    return (res == CURLE_OK) ? 0 : -1;
}

// Get service from API
json_object* service_get_service(service_controller_t* controller,
                                const char* namespace, const char* service_name) {
    char path[512];
    snprintf(path, sizeof(path), "/api/v1/namespaces/%s/services/%s",
             namespace, service_name);
    
    http_response_t response = {0};
    response.data = (char*)malloc(MAX_API_RESPONSE);
    response.capacity = MAX_API_RESPONSE;
    
    if (api_request(controller, "GET", path, NULL, &response) != 0) {
        free(response.data);
        return NULL;
    }
    
    json_object* result = json_tokener_parse(response.data);
    free(response.data);
    return result;
}

// Get all services
static json_object* service_get_all_services(service_controller_t* controller) {
    http_response_t response = {0};
    response.data = (char*)malloc(MAX_API_RESPONSE);
    response.capacity = MAX_API_RESPONSE;
    
    if (api_request(controller, "GET", "/api/v1/services", NULL, &response) != 0) {
        free(response.data);
        return NULL;
    }
    
    json_object* result = json_tokener_parse(response.data);
    free(response.data);
    return result;
}

// Get pods by namespace
static json_object* service_get_pods_in_namespace(service_controller_t* controller,
                                                 const char* namespace) {
    char path[512];
    snprintf(path, sizeof(path), "/api/v1/namespaces/%s/pods", namespace);
    
    http_response_t response = {0};
    response.data = (char*)malloc(MAX_API_RESPONSE);
    response.capacity = MAX_API_RESPONSE;
    
    if (api_request(controller, "GET", path, NULL, &response) != 0) {
        free(response.data);
        return NULL;
    }
    
    json_object* result = json_tokener_parse(response.data);
    free(response.data);
    return result;
}

// Check if pod matches selector labels
int service_pod_matches_selector(json_object* pod, json_object* selector) {
    if (!pod || !selector) return 0;
    
    // Get pod labels
    json_object* metadata = NULL;
    if (!json_object_object_get_ex(pod, "metadata", &metadata)) {
        return 0;
    }
    
    json_object* pod_labels = NULL;
    if (!json_object_object_get_ex(metadata, "labels", &pod_labels)) {
        return 0;
    }
    
    // Check if all selector labels are present in pod labels with same values
    json_object_object_foreach(selector, key, val) {
        json_object* pod_val = NULL;
        if (!json_object_object_get_ex(pod_labels, key, &pod_val)) {
            return 0;  // Label key not found in pod
        }
        
        const char* sel_str = json_object_get_string(val);
        const char* pod_str = json_object_get_string(pod_val);
        
        if (!sel_str || !pod_str || strcmp(sel_str, pod_str) != 0) {
            return 0;  // Label value doesn't match
        }
    }
    
    return 1;  // All labels match
}

// Discover endpoints for a service
endpoint_t** service_discover_endpoints(service_controller_t* controller,
                                       const char* namespace, const char* service_name,
                                       json_object* selector, int* out_count) {
    if (!controller || !namespace || !service_name) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    
    // Get pods in namespace
    json_object* pods_obj = service_get_pods_in_namespace(controller, namespace);
    if (!pods_obj) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    
    // Allocate endpoint array
    endpoint_t** endpoints = (endpoint_t**)malloc(sizeof(endpoint_t*) * MAX_ENDPOINTS);
    int endpoint_count = 0;
    
    json_object* items = NULL;
    if (json_object_object_get_ex(pods_obj, "items", &items) &&
        json_object_is_type(items, json_type_array)) {
        
        int count = json_object_array_length(items);
        for (int i = 0; i < count && endpoint_count < MAX_ENDPOINTS; i++) {
            json_object* pod = json_object_array_get_idx(items, i);
            
            // Check if pod matches selector
            if (!service_pod_matches_selector(pod, selector)) {
                continue;
            }
            
            // Extract pod info
            json_object* metadata = NULL;
            json_object* status = NULL;
            
            if (json_object_object_get_ex(pod, "metadata", &metadata) &&
                json_object_object_get_ex(pod, "status", &status)) {
                
                json_object* pod_name = NULL;
                json_object* pod_ip = NULL;
                json_object* phase = NULL;
                
                if (json_object_object_get_ex(metadata, "name", &pod_name) &&
                    json_object_object_get_ex(status, "podIP", &pod_ip) &&
                    json_object_object_get_ex(status, "phase", &phase)) {
                    
                    const char* name = json_object_get_string(pod_name);
                    const char* ip = json_object_get_string(pod_ip);
                    const char* phase_str = json_object_get_string(phase);
                    
                    if (name && ip && phase_str && strcmp(phase_str, "Running") == 0) {
                        // Create endpoint
                        endpoint_t* endpoint = (endpoint_t*)malloc(sizeof(endpoint_t));
                        strncpy(endpoint->pod_name, name, sizeof(endpoint->pod_name) - 1);
                        strncpy(endpoint->pod_namespace, namespace, sizeof(endpoint->pod_namespace) - 1);
                        strncpy(endpoint->pod_ip, ip, sizeof(endpoint->pod_ip) - 1);
                        endpoint->port = 8080;  // Default, would extract from service spec
                        endpoint->ready = 1;
                        endpoint->added_at = time(NULL);
                        
                        endpoints[endpoint_count++] = endpoint;
                        
                        fprintf(stderr, "[service-controller] Endpoint discovered: %s (%s) for %s/%s\n",
                                name, ip, namespace, service_name);
                    }
                }
            }
        }
    }
    
    json_object_put(pods_obj);
    
    if (out_count) *out_count = endpoint_count;
    return (endpoint_count > 0) ? endpoints : NULL;
}

// Update endpoints for a service
int service_update_endpoints(service_controller_t* controller,
                            const char* namespace, const char* service_name,
                            endpoint_t** endpoints, int endpoint_count) {
    char path[512];
    snprintf(path, sizeof(path), "/api/v1/namespaces/%s/endpoints/%s",
             namespace, service_name);
    
    // Build endpoints JSON
    json_object* ep_obj = json_object_new_object();
    json_object_object_add(ep_obj, "kind", json_object_new_string("Endpoints"));
    json_object_object_add(ep_obj, "apiVersion", json_object_new_string("v1"));
    
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(service_name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(ep_obj, "metadata", metadata);
    
    // Add subsets with addresses
    json_object* subsets = json_object_new_array();
    json_object* subset = json_object_new_object();
    json_object* addresses = json_object_new_array();
    
    for (int i = 0; i < endpoint_count; i++) {
        if (!endpoints[i]) continue;
        
        json_object* addr = json_object_new_object();
        json_object_object_add(addr, "ip", 
                             json_object_new_string(endpoints[i]->pod_ip));
        
        json_object* target_ref = json_object_new_object();
        json_object_object_add(target_ref, "name", 
                             json_object_new_string(endpoints[i]->pod_name));
        json_object_object_add(target_ref, "namespace",
                             json_object_new_string(endpoints[i]->pod_namespace));
        json_object_object_add(addr, "targetRef", target_ref);
        
        json_object_array_add(addresses, addr);
    }
    
    json_object_object_add(subset, "addresses", addresses);
    json_object_array_add(subsets, subset);
    json_object_object_add(ep_obj, "subsets", subsets);
    
    const char* json_str = json_object_to_json_string_ext(ep_obj, JSON_C_TO_STRING_PLAIN);
    
    http_response_t response = {0};
    response.data = (char*)malloc(MAX_API_RESPONSE);
    response.capacity = MAX_API_RESPONSE;
    
    int result = api_request(controller, "PUT", path, json_str, &response);
    
    fprintf(stderr, "[service-controller] Updated endpoints for %s/%s (%d endpoints)\n",
            namespace, service_name, endpoint_count);
    
    json_object_put(ep_obj);
    free(response.data);
    
    return result;
}

// Select endpoint using load balancing strategy
endpoint_t* service_select_endpoint(endpoint_t** endpoints, int count, int strategy) {
    if (!endpoints || count <= 0) return NULL;
    
    switch (strategy) {
        case LB_ROUND_ROBIN: {
            // Simple round-robin (thread-unsafe, would need per-service state in production)
            static int rr_index = 0;
            endpoint_t* selected = endpoints[rr_index % count];
            rr_index++;
            return selected;
        }
        
        case LB_LEAST_CONNECTIONS: {
            // Select endpoint with fewest active connections (simplified)
            // In production, would track actual connection counts
            endpoint_t* best = endpoints[0];
            for (int i = 1; i < count; i++) {
                // All assumed to have 0 connections for now
                if (rand() % 2 == 0) {
                    best = endpoints[i];
                }
            }
            return best;
        }
        
        case LB_SESSION_AFFINITY: {
            // Session affinity would use hashing based on client IP (not implemented here)
            // For now, just return first endpoint
            return endpoints[0];
        }
        
        default:
            return endpoints[0];
    }
}

// Main service controller loop
static void* service_reconcile_loop(void* arg) {
    service_controller_t* controller = (service_controller_t*)arg;
    if (!controller) return NULL;
    
    fprintf(stderr, "[service-controller] Starting reconciliation loop\n");
    
    while (1) {
        json_object* services_obj = service_get_all_services(controller);
        
        if (services_obj) {
            json_object* items = NULL;
            if (json_object_object_get_ex(services_obj, "items", &items) &&
                json_object_is_type(items, json_type_array)) {
                
                int count = json_object_array_length(items);
                for (int i = 0; i < count; i++) {
                    json_object* svc = json_object_array_get_idx(items, i);
                    json_object* metadata = NULL;
                    json_object* spec = NULL;
                    
                    if (json_object_object_get_ex(svc, "metadata", &metadata) &&
                        json_object_object_get_ex(svc, "spec", &spec)) {
                        
                        json_object* name_obj = NULL;
                        json_object* namespace_obj = NULL;
                        json_object* selector = NULL;
                        json_object* type_obj = NULL;
                        
                        if (json_object_object_get_ex(metadata, "name", &name_obj) &&
                            json_object_object_get_ex(metadata, "namespace", &namespace_obj) &&
                            json_object_object_get_ex(spec, "selector", &selector) &&
                            json_object_object_get_ex(spec, "type", &type_obj)) {
                            
                            const char* svc_name = json_object_get_string(name_obj);
                            const char* namespace = json_object_get_string(namespace_obj);
                            const char* svc_type = json_object_get_string(type_obj);
                            
                            if (!svc_type) svc_type = "ClusterIP";
                            
                            // Discover endpoints matching selector
                            int endpoint_count = 0;
                            endpoint_t** endpoints = service_discover_endpoints(
                                controller, namespace, svc_name, selector, &endpoint_count);
                            
                            // Update service endpoints in API
                            if (endpoint_count > 0) {
                                service_update_endpoints(controller, namespace, svc_name,
                                                       endpoints, endpoint_count);
                                
                                // Free endpoints
                                for (int j = 0; j < endpoint_count; j++) {
                                    free(endpoints[j]);
                                }
                                free(endpoints);
                            }
                            
                            fprintf(stderr, "[service-controller] Service %s/%s (type: %s) → %d endpoints\n",
                                    namespace, svc_name, svc_type, endpoint_count);
                        }
                    }
                }
            }
            json_object_put(services_obj);
        }
        
        sleep(controller->update_interval);
    }
    
    return NULL;
}

// Run service controller
int service_controller_run(service_controller_t* controller) {
    if (!controller) return -1;
    
    pthread_t tid;
    if (pthread_create(&tid, NULL, service_reconcile_loop, (void*)controller) != 0) {
        fprintf(stderr, "[service-controller] Failed to create thread\n");
        return -1;
    }
    
    pthread_detach(tid);
    return 0;
}

// Get pods by labels (not currently used but provided for completeness)
json_object* service_get_pods_by_labels(service_controller_t* controller,
                                       const char* namespace, json_object* labels) {
    // In production, this would filter pods by labels during listing
    // For now, just return all pods and filter locally
    return service_get_pods_in_namespace(controller, namespace);
}
