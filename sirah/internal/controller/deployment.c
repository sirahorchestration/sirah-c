// internal/controller/deployment.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include "deployment.h"

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

int deployment_controller_init(void) {
    curl_handle = curl_easy_init();
    if (!curl_handle) return -1;
    
    printf("Deployment controller initialized\n");
    return 0;
}

// Create a pod for a deployment
static int create_pod_for_deployment(const char* namespace, const char* name, 
                                     const char* image) {
    if (!curl_handle) return -1;
    
    // Build pod creation request
    json_object* pod = json_object_new_object();
    json_object_object_add(pod, "name", json_object_new_string(name));
    json_object_object_add(pod, "namespace", json_object_new_string(namespace));
    
    json_object* container = json_object_new_object();
    json_object_object_add(container, "image", json_object_new_string(image));
    json_object_object_add(pod, "image", json_object_new_string(image));
    
    const char* json_str = json_object_to_json_string(pod);
    
    // Call API server to create pod
    char url[512];
    snprintf(url, sizeof(url), "http://localhost:6443/api/v1/namespaces/%s/pods", namespace);
    
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
    json_object_put(pod);
    free(response.data);
    
    return (res == CURLE_OK) ? 0 : -1;
}

// Get list of deployments from API server
static json_object* get_deployments(void) {
    if (!curl_handle) return NULL;
    
    response_t response = {0};
    response.data = (char*)malloc(1);
    
    curl_easy_setopt(curl_handle, CURLOPT_URL, "http://localhost:6443/api/v1/deployments");
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, NULL);
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

int deployment_controller_run(void) {
    printf("Deployment controller running...\n");

    while (1) {
        // Reconciliation loop
        // 1. List all deployments from API server
        json_object* deployments_obj = get_deployments();
        
        if (deployments_obj) {
            json_object* items = NULL;
            if (json_object_object_get_ex(deployments_obj, "items", &items)) {
                int count = json_object_array_length(items);
                for (int i = 0; i < count; i++) {
                    json_object* deployment = json_object_array_get_idx(items, i);
                    
                    // Get deployment metadata
                    json_object* metadata = NULL;
                    if (json_object_object_get_ex(deployment, "metadata", &metadata)) {
                        json_object* name_obj = NULL;
                        json_object* namespace_obj = NULL;
                        
                        if (json_object_object_get_ex(metadata, "name", &name_obj) &&
                            json_object_object_get_ex(metadata, "namespace", &namespace_obj)) {
                            
                            const char* dep_name = json_object_get_string(name_obj);
                            const char* namespace = json_object_get_string(namespace_obj);
                            
                            // Get spec for desired replicas
                            json_object* spec = NULL;
                            if (json_object_object_get_ex(deployment, "spec", &spec)) {
                                json_object* replicas_obj = NULL;
                                if (json_object_object_get_ex(spec, "replicas", &replicas_obj)) {
                                    int desired_replicas = json_object_get_int(replicas_obj);
                                    
                                    // TODO: Get current replica count
                                    // For now, create pods up to desired count
                                    for (int r = 0; r < desired_replicas; r++) {
                                        char pod_name[256];
                                        snprintf(pod_name, sizeof(pod_name), "%s-pod-%d", 
                                                dep_name, r);
                                        
                                        // Create pod
                                        create_pod_for_deployment(namespace, pod_name, "nginx:latest");
                                        
                                        printf("  Created pod: %s\n", pod_name);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            json_object_put(deployments_obj);
        }

        // Sleep and repeat
        sleep(5);
    }

    return 0;
}

int deployment_controller_shutdown(void) {
    if (curl_handle) {
        curl_easy_cleanup(curl_handle);
        curl_handle = NULL;
    }
    printf("Deployment controller shutdown\n");
    return 0;
}
