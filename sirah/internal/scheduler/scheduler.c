// internal/scheduler/scheduler.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include "scheduler.h"

static const char* api_server_url = NULL;
static CURL* curl_handle = NULL;

typedef struct {
    char* data;
    size_t size;
} response_t;

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    response_t* mem = (response_t*)userp;
    
    char* ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) return 0;
    
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    
    return realsize;
}

int scheduler_init(const char* apiserver_url) {
    api_server_url = apiserver_url;
    curl_handle = curl_easy_init();
    if (!curl_handle) return -1;
    
    printf("Scheduler initialized with API Server: %s\n", apiserver_url ? apiserver_url : "localhost:6443");
    return 0;
}

// Get list of pending pods
static json_object* get_pending_pods(void) {
    if (!curl_handle) return NULL;
    
    response_t response = {0};
    response.data = (char*)malloc(1);
    
    curl_easy_setopt(curl_handle, CURLOPT_URL, "http://localhost:6443/api/v1/namespaces/default/pods");
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl_handle);
    
    if (res != CURLE_OK) {
        free(response.data);
        return NULL;
    }
    
    json_object* result = json_tokener_parse(response.data);
    free(response.data);
    
    return result;
}

// Get list of available nodes
static json_object* get_nodes(void) {
    if (!curl_handle) return NULL;
    
    response_t response = {0};
    response.data = (char*)malloc(1);
    
    curl_easy_setopt(curl_handle, CURLOPT_URL, "http://localhost:6443/api/v1/nodes");
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl_handle);
    
    if (res != CURLE_OK) {
        free(response.data);
        return NULL;
    }
    
    json_object* result = json_tokener_parse(response.data);
    free(response.data);
    
    return result;
}

// Select best node (round-robin for MVP)
static const char* select_best_node(json_object* nodes) {
    if (!nodes) return "control-plane";  // Default to control plane
    
    json_object* items = NULL;
    if (json_object_object_get_ex(nodes, "items", &items)) {
        if (json_object_array_length(items) > 0) {
            json_object* node = json_object_array_get_idx(items, 0);
            json_object* metadata = NULL;
            if (json_object_object_get_ex(node, "name", &metadata)) {
                return json_object_get_string(metadata);
            }
            if (json_object_object_get_ex(node, "metadata", &metadata)) {
                json_object* name_obj = NULL;
                if (json_object_object_get_ex(metadata, "name", &name_obj)) {
                    return json_object_get_string(name_obj);
                }
            }
        }
    }
    
    return "control-plane";
}

// Bind pod to node
static int bind_pod(const char* namespace, const char* pod_name, const char* node_name) {
    if (!curl_handle) return -1;
    
    json_object* binding = json_object_new_object();
    json_object_object_add(binding, "nodeName", json_object_new_string(node_name));
    json_object_object_add(binding, "status", json_object_new_string("Running"));
    
    const char* json_str = json_object_to_json_string(binding);
    
    char url[512];
    snprintf(url, sizeof(url), "http://localhost:6443/api/v1/namespaces/%s/pods/%s/bind", 
             namespace, pod_name);
    
    response_t response = {0};
    response.data = (char*)malloc(1);
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl_handle);
    
    curl_slist_free_all(headers);
    json_object_put(binding);
    free(response.data);
    
    return (res == CURLE_OK) ? 0 : -1;
}

int scheduler_run(void) {
    printf("Scheduler running...\n");

    while (1) {
        // Fetch pending pods and schedule them
        json_object* pods_obj = get_pending_pods();
        json_object* nodes_obj = get_nodes();
        
        if (pods_obj && nodes_obj) {
            json_object* items = NULL;
            if (json_object_object_get_ex(pods_obj, "items", &items)) {
                int pod_count = json_object_array_length(items);
                
                if (pod_count > 0) {
                    printf("[Scheduler] Found %d pods to schedule\n", pod_count);
                    
                    for (int i = 0; i < pod_count; i++) {
                        json_object* pod = json_object_array_get_idx(items, i);
                        
                        json_object* metadata = NULL;
                        if (json_object_object_get_ex(pod, "metadata", &metadata)) {
                            json_object* name_obj = NULL;
                            json_object* namespace_obj = NULL;
                            
                            if (json_object_object_get_ex(metadata, "name", &name_obj) &&
                                json_object_object_get_ex(metadata, "namespace", &namespace_obj)) {
                                
                                const char* pod_name = json_object_get_string(name_obj);
                                const char* namespace = json_object_get_string(namespace_obj);
                                
                                // Select best node
                                const char* best_node = select_best_node(nodes_obj);
                                
                                // Bind pod to node
                                if (bind_pod(namespace, pod_name, best_node) == 0) {
                                    printf("[Scheduler] Bound pod %s to node %s\n", pod_name, best_node);
                                }
                            }
                        }
                    }
                }
            }
            
            json_object_put(pods_obj);
            json_object_put(nodes_obj);
        }

        // Sleep and repeat
        sleep(5);
    }

    return 0;
}

void scheduler_shutdown(void) {
    if (curl_handle) {
        curl_easy_cleanup(curl_handle);
        curl_handle = NULL;
    }    printf("Scheduler shutdown\n");
}