// internal/controller/deployment.c
// Deployment Controller Implementation
// Watches Deployment objects, reconciles replica counts, implements rolling updates,
// tracks status, and generates events for visibility

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <pthread.h>
#include "deployment.h"

#define SYNC_INTERVAL 5           // Reconcile every 5 seconds
#define MAX_API_RESPONSE 1048576  // 1MB max response
#define MAX_DEPLOYMENTS 100
#define MAX_DEPLOYMENT_HISTORY 10

// HTTP response buffer
typedef struct {
    char* data;
    size_t size;
    size_t capacity;
} http_response_t;

// Deployment version tracking for rolling updates
typedef struct {
    char image[512];
    int replicas;
    int ready_replicas;
    time_t created_at;
} deployment_revision_t;

// Tracking deployments to avoid recreation
typedef struct {
    deployment_t deployment;
    deployment_revision_t revisions[MAX_DEPLOYMENT_HISTORY];
    int revision_count;
    int current_revision;
} tracked_deployment_t;

static const char* api_server_url = NULL;
static CURL* curl_handle = NULL;
static pthread_mutex_t deployment_mutex = PTHREAD_MUTEX_INITIALIZER;
static tracked_deployment_t tracked_deployments[MAX_DEPLOYMENTS];
static int tracked_deployment_count = 0;

// Curl callback to accumulate response
static size_t deployment_curl_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    http_response_t* mem = (http_response_t*)userp;
    
    if (mem->size + realsize >= mem->capacity) {
        return 0;  // Response too large
    }
    
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    
    return realsize;
}

// Make HTTP request to API server
static int api_request(const char* method, const char* path, const char* body, 
                       http_response_t* response) {
    if (!curl_handle || !api_server_url) {
        return -1;
    }
    
    char url[1024];
    snprintf(url, sizeof(url), "%s%s", api_server_url, path);
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, deployment_curl_write_callback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*)response);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, method);
    
    if (body) {
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, body);
    }
    
    CURLcode res = curl_easy_perform(curl_handle);
    curl_slist_free_all(headers);
    
    return (res == CURLE_OK) ? 0 : -1;
}

// List all deployments from API server
static json_object* get_deployments(void) {
    http_response_t response = {0};
    response.data = (char*)malloc(MAX_API_RESPONSE);
    response.capacity = MAX_API_RESPONSE;
    
    if (api_request("GET", "/api/v1/deployments", NULL, &response) != 0) {
        free(response.data);
        return NULL;
    }
    
    json_object* result = json_tokener_parse(response.data);
    free(response.data);
    return result;
}

// List pods in a namespace
static json_object* get_pods_in_namespace(const char* namespace) {
    char path[512];
    snprintf(path, sizeof(path), "/api/v1/namespaces/%s/pods", namespace);
    
    http_response_t response = {0};
    response.data = (char*)malloc(MAX_API_RESPONSE);
    response.capacity = MAX_API_RESPONSE;
    
    if (api_request("GET", path, NULL, &response) != 0) {
        free(response.data);
        return NULL;
    }
    
    json_object* result = json_tokener_parse(response.data);
    free(response.data);
    return result;
}

// Create a pod for deployment
static int create_pod_for_deployment(const char* namespace, const char* deployment_name,
                                     const char* image, int pod_index) {
    // Generate pod name from deployment name
    char pod_name[256];
    snprintf(pod_name, sizeof(pod_name), "%s-pod-%d", deployment_name, pod_index);
    
    // Build pod spec
    json_object* pod = json_object_new_object();
    json_object_object_add(pod, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(pod, "kind", json_object_new_string("Pod"));
    
    // Metadata
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(pod_name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    
    // Labels with deployment owner reference
    json_object* labels = json_object_new_object();
    json_object_object_add(labels, "deployment", json_object_new_string(deployment_name));
    json_object_object_add(labels, "pod-index", json_object_new_int(pod_index));
    json_object_object_add(metadata, "labels", labels);
    json_object_object_add(pod, "metadata", metadata);
    
    // Spec
    json_object* spec = json_object_new_object();
    json_object* containers = json_object_new_array();
    
    json_object* container = json_object_new_object();
    json_object_object_add(container, "name", json_object_new_string("app"));
    json_object_object_add(container, "image", json_object_new_string(image));
    json_object_array_add(containers, container);
    
    json_object_object_add(spec, "containers", containers);
    json_object_object_add(pod, "spec", spec);
    
    const char* json_str = json_object_to_json_string_ext(pod, JSON_C_TO_STRING_PLAIN);
    
    // POST to create pod
    char path[512];
    snprintf(path, sizeof(path), "/api/v1/namespaces/%s/pods", namespace);
    
    http_response_t response = {0};
    response.data = (char*)malloc(MAX_API_RESPONSE);
    response.capacity = MAX_API_RESPONSE;
    
    int result = api_request("POST", path, json_str, &response);
    
    fprintf(stderr, "[deployment-controller] Created pod %s/%s (image: %s)\n", 
            namespace, pod_name, image);
    
    json_object_put(pod);
    free(response.data);
    
    return result;
}

// Delete a pod
static int delete_pod(const char* namespace, const char* pod_name) {
    char path[512];
    snprintf(path, sizeof(path), "/api/v1/namespaces/%s/pods/%s", namespace, pod_name);
    
    http_response_t response = {0};
    response.data = (char*)malloc(MAX_API_RESPONSE);
    response.capacity = MAX_API_RESPONSE;
    
    int result = api_request("DELETE", path, NULL, &response);
    
    fprintf(stderr, "[deployment-controller] Deleted pod %s/%s\n", namespace, pod_name);
    
    free(response.data);
    return result;
}

// Count pods belonging to a deployment
static int count_deployment_pods(const char* namespace, const char* deployment_name,
                                int* out_total, int* out_ready) {
    json_object* pods_obj = get_pods_in_namespace(namespace);
    if (!pods_obj) {
        return -1;
    }
    
    int total = 0;
    int ready = 0;
    
    json_object* items = NULL;
    if (json_object_object_get_ex(pods_obj, "items", &items) &&
        json_object_is_type(items, json_type_array)) {
        
        int count = json_object_array_length(items);
        for (int i = 0; i < count; i++) {
            json_object* pod = json_object_array_get_idx(items, i);
            json_object* metadata = NULL;
            
            if (json_object_object_get_ex(pod, "metadata", &metadata)) {
                json_object* labels = NULL;
                if (json_object_object_get_ex(metadata, "labels", &labels)) {
                    json_object* dep_label = NULL;
                    if (json_object_object_get_ex(labels, "deployment", &dep_label)) {
                        const char* dep_name = json_object_get_string(dep_label);
                        if (dep_name && strcmp(dep_name, deployment_name) == 0) {
                            total++;
                            
                            // Check if ready
                            json_object* status = NULL;
                            if (json_object_object_get_ex(pod, "status", &status)) {
                                json_object* phase = NULL;
                                if (json_object_object_get_ex(status, "phase", &phase)) {
                                    const char* phase_str = json_object_get_string(phase);
                                    if (phase_str && strcmp(phase_str, "Running") == 0) {
                                        ready++;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    json_object_put(pods_obj);
    
    if (out_total) *out_total = total;
    if (out_ready) *out_ready = ready;
    
    return 0;
}

// Create pods for deployment replicas
int deployment_create_replicas(const char* namespace, const char* deployment_name,
                               const char* image, int desired_count) {
    int total, ready;
    if (count_deployment_pods(namespace, deployment_name, &total, &ready) != 0) {
        return -1;
    }
    
    // Create missing pods
    int to_create = desired_count - total;
    for (int i = 0; i < to_create; i++) {
        create_pod_for_deployment(namespace, deployment_name, image, total + i);
        usleep(100000);  // 100ms delay between pod creations
    }
    
    return 0;
}

// Delete excess pods from deployment
int deployment_delete_excess_pods(const char* namespace, const char* deployment_name,
                                  int desired_count) {
    json_object* pods_obj = get_pods_in_namespace(namespace);
    if (!pods_obj) {
        return -1;
    }
    
    int total = 0;
    int deleted = 0;
    
    json_object* items = NULL;
    if (json_object_object_get_ex(pods_obj, "items", &items) &&
        json_object_is_type(items, json_type_array)) {
        
        int count = json_object_array_length(items);
        for (int i = 0; i < count && deleted < (count - desired_count); i++) {
            json_object* pod = json_object_array_get_idx(items, i);
            json_object* metadata = NULL;
            
            if (json_object_object_get_ex(pod, "metadata", &metadata)) {
                json_object* pod_name_obj = NULL;
                json_object* labels = NULL;
                
                if (json_object_object_get_ex(metadata, "name", &pod_name_obj) &&
                    json_object_object_get_ex(metadata, "labels", &labels)) {
                    
                    json_object* dep_label = NULL;
                    if (json_object_object_get_ex(labels, "deployment", &dep_label)) {
                        const char* dep_name = json_object_get_string(dep_label);
                        if (dep_name && strcmp(dep_name, deployment_name) == 0) {
                            total++;
                            if (total > desired_count) {
                                const char* pod_name = json_object_get_string(pod_name_obj);
                                delete_pod(namespace, pod_name);
                                deleted++;
                            }
                        }
                    }
                }
            }
        }
    }
    
    json_object_put(pods_obj);
    return 0;
}

// Rolling update - gradually replace pods
int deployment_rolling_update(const char* namespace, const char* deployment_name,
                              const char* new_image, int max_surge, int max_unavailable) {
    fprintf(stderr, "[deployment-controller] Starting rolling update for %s/%s → %s\n",
            namespace, deployment_name, new_image);
    
    int total, ready;
    if (count_deployment_pods(namespace, deployment_name, &total, &ready) != 0) {
        return -1;
    }
    
    int desired_replicas = total;
    int updated = 0;
    
    // Gradually update pods (1 at a time by default)
    while (updated < desired_replicas) {
        // Create one pod with new image
        create_pod_for_deployment(namespace, deployment_name, new_image, 
                                desired_replicas + updated);
        updated++;
        
        // Wait for new pod to be ready (simplified - just wait a bit)
        sleep(2);
        
        // Delete oldest pod with old image
        // (In real implementation, would check actual image and gracefully drain connections)
        if (updated < desired_replicas) {
            json_object* pods_obj = get_pods_in_namespace(namespace);
            if (pods_obj) {
                // Find and delete first old pod (basic strategy)
                json_object* items = NULL;
                if (json_object_object_get_ex(pods_obj, "items", &items)) {
                    int count = json_object_array_length(items);
                    for (int i = 0; i < count; i++) {
                        json_object* pod = json_object_array_get_idx(items, i);
                        json_object* metadata = NULL;
                        if (json_object_object_get_ex(pod, "metadata", &metadata)) {
                            json_object* pod_name_obj = NULL;
                            json_object* labels = NULL;
                            if (json_object_object_get_ex(metadata, "name", &pod_name_obj) &&
                                json_object_object_get_ex(metadata, "labels", &labels)) {
                                
                                json_object* dep_label = NULL;
                                if (json_object_object_get_ex(labels, "deployment", &dep_label)) {
                                    const char* dep_name = json_object_get_string(dep_label);
                                    if (dep_name && strcmp(dep_name, deployment_name) == 0) {
                                        const char* pod_name = json_object_get_string(pod_name_obj);
                                        delete_pod(namespace, pod_name);
                                        break;  // Delete one at a time
                                    }
                                }
                            }
                        }
                    }
                }
                json_object_put(pods_obj);
            }
        }
    }
    
    fprintf(stderr, "[deployment-controller] Rolling update complete for %s/%s\n",
            namespace, deployment_name);
    return 0;
}

// Update deployment status in API server
int deployment_update_status(const char* namespace, const char* deployment_name,
                            int current, int ready, int updated) {
    char path[512];
    snprintf(path, sizeof(path), "/api/v1/namespaces/%s/deployments/%s/status",
             namespace, deployment_name);
    
    char body[512];
    snprintf(body, sizeof(body),
             "{"
             "\"status\":{"
             "\"replicas\":%d,"
             "\"readyReplicas\":%d,"
             "\"updatedReplicas\":%d"
             "}"
             "}", current, ready, updated);
    
    http_response_t response = {0};
    response.data = (char*)malloc(MAX_API_RESPONSE);
    response.capacity = MAX_API_RESPONSE;
    
    int result = api_request("PATCH", path, body, &response);
    
    free(response.data);
    return result;
}

// Generate event for deployment status changes
int deployment_emit_event(const char* namespace, const char* deployment_name,
                         const char* reason, const char* message) {
    fprintf(stderr, "[deployment-controller] Event: %s/%s - %s: %s\n",
            namespace, deployment_name, reason, message);
    
    // In full implementation, would create an Event object in API server
    // For now, just log it
    return 0;
}

// Reconciliation loop
static void* deployment_reconcile_loop(void* arg) {
    (void)arg;
    
    fprintf(stderr, "[deployment-controller] Starting reconciliation loop\n");
    
    while (1) {
        json_object* deployments_obj = get_deployments();
        
        if (deployments_obj) {
            json_object* items = NULL;
            if (json_object_object_get_ex(deployments_obj, "items", &items) &&
                json_object_is_type(items, json_type_array)) {
                
                int count = json_object_array_length(items);
                for (int i = 0; i < count; i++) {
                    json_object* dep = json_object_array_get_idx(items, i);
                    json_object* metadata = NULL;
                    json_object* spec = NULL;
                    
                    if (json_object_object_get_ex(dep, "metadata", &metadata) &&
                        json_object_object_get_ex(dep, "spec", &spec)) {
                        
                        json_object* name_obj = NULL;
                        json_object* namespace_obj = NULL;
                        json_object* replicas_obj = NULL;
                        json_object* template_obj = NULL;
                        
                        if (json_object_object_get_ex(metadata, "name", &name_obj) &&
                            json_object_object_get_ex(metadata, "namespace", &namespace_obj) &&
                            json_object_object_get_ex(spec, "replicas", &replicas_obj) &&
                            json_object_object_get_ex(spec, "template", &template_obj)) {
                            
                            const char* dep_name = json_object_get_string(name_obj);
                            const char* namespace = json_object_get_string(namespace_obj);
                            int desired = json_object_get_int(replicas_obj);
                            
                            // Extract image from template
                            json_object* pod_spec = NULL;
                            json_object_object_get_ex(template_obj, "spec", &pod_spec);
                            const char* image = "nginx:latest";  // Default
                            
                            if (pod_spec) {
                                json_object* containers = NULL;
                                if (json_object_object_get_ex(pod_spec, "containers", &containers) &&
                                    json_object_is_type(containers, json_type_array) &&
                                    json_object_array_length(containers) > 0) {
                                    
                                    json_object* first_container = 
                                        json_object_array_get_idx(containers, 0);
                                    json_object* image_obj = NULL;
                                    if (json_object_object_get_ex(first_container, "image", &image_obj)) {
                                        image = json_object_get_string(image_obj);
                                    }
                                }
                            }
                            
                            // Count current pods
                            int current, ready;
                            if (count_deployment_pods(namespace, dep_name, &current, &ready) == 0) {
                                
                                // Reconcile: create or delete pods to match desired count
                                if (current < desired) {
                                    deployment_create_replicas(namespace, dep_name, image,
                                                             desired);
                                    deployment_emit_event(namespace, dep_name, "ReplicaCreated",
                                                        "Creating missing replicas");
                                } else if (current > desired) {
                                    deployment_delete_excess_pods(namespace, dep_name, desired);
                                    deployment_emit_event(namespace, dep_name, "ReplicaDeleted",
                                                        "Deleting excess replicas");
                                }
                                
                                // Update deployment status
                                deployment_update_status(namespace, dep_name, current, ready,
                                                       current);
                            }
                        }
                    }
                }
            }
            json_object_put(deployments_obj);
        }
        
        sleep(SYNC_INTERVAL);
    }
    
    return NULL;
}

// Initialize deployment controller
int deployment_controller_init(void) {
    curl_handle = curl_easy_init();
    if (!curl_handle) {
        fprintf(stderr, "[deployment-controller] Failed to initialize curl\n");
        return -1;
    }
    
    fprintf(stderr, "[deployment-controller] Initialized\n");
    return 0;
}

// Run deployment controller (starts in background thread)
int deployment_controller_run(void) {
    pthread_t tid;
    if (pthread_create(&tid, NULL, deployment_reconcile_loop, NULL) != 0) {
        fprintf(stderr, "[deployment-controller] Failed to create thread\n");
        return -1;
    }
    
    pthread_detach(tid);
    return 0;
}

// Shutdown deployment controller
int deployment_controller_shutdown(void) {
    if (curl_handle) {
        curl_easy_cleanup(curl_handle);
        curl_handle = NULL;
    }
    fprintf(stderr, "[deployment-controller] Shutdown\n");
    return 0;
}

// Manual reconciliation (can be called externally)
int deployment_controller_reconcile(deployment_t* deployment) {
    if (!deployment) {
        return -1;
    }
    
    return 0;
}
