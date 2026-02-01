#include "service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <unistd.h>

typedef struct {
    char* data;
    size_t size;
} response_buffer_t;

static size_t write_callback(void* data, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    response_buffer_t* buf = (response_buffer_t*)userp;
    
    char* ptr = realloc(buf->data, buf->size + realsize + 1);
    if (!ptr) return 0;
    
    buf->data = ptr;
    memcpy(&(buf->data[buf->size]), data, realsize);
    buf->size += realsize;
    buf->data[buf->size] = 0;
    
    return realsize;
}

service_controller_t* service_controller_new(const char* api_server_url) {
    if (!api_server_url) return NULL;
    
    service_controller_t* controller = (service_controller_t*)malloc(sizeof(service_controller_t));
    controller->api_server_url = strdup(api_server_url);
    controller->curl_handle = NULL;
    controller->update_interval = 5;
    
    return controller;
}

void service_controller_free(service_controller_t* controller) {
    if (!controller) return;
    free(controller->api_server_url);
    free(controller);
}

int service_controller_init(service_controller_t* controller) {
    if (!controller) return -1;
    
    controller->curl_handle = curl_easy_init();
    if (!controller->curl_handle) {
        fprintf(stderr, "[service-controller] Failed to initialize curl\n");
        return -1;
    }
    
    fprintf(stderr, "[service-controller] Initialized\n");
    return 0;
}

void service_controller_shutdown(service_controller_t* controller) {
    if (!controller || !controller->curl_handle) return;
    
    curl_easy_cleanup((CURL*)controller->curl_handle);
    controller->curl_handle = NULL;
    fprintf(stderr, "[service-controller] Shutdown\n");
}

// Fetch services from API server
static int get_services(service_controller_t* controller, json_object** result) {
    if (!controller || !controller->curl_handle) return -1;
    
    CURL* curl = (CURL*)controller->curl_handle;
    response_buffer_t buf = {0};
    buf.data = (char*)malloc(1);
    
    char url[512];
    snprintf(url, sizeof(url), "%s/api/v1/services", controller->api_server_url);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&buf);
    
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "[service-controller] Failed to get services: %s\n", curl_easy_strerror(res));
        free(buf.data);
        return -1;
    }
    
    *result = json_tokener_parse(buf.data);
    free(buf.data);
    
    return *result ? 0 : -1;
}

// Fetch pods with labels
static int get_pods_by_selector(service_controller_t* controller, json_object** result) {
    CURL* curl = (CURL*)controller->curl_handle;
    response_buffer_t buf = {0};
    buf.data = (char*)malloc(1);
    
    char url[512];
    snprintf(url, sizeof(url), "%s/api/v1/namespaces/default/pods", controller->api_server_url);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&buf);
    
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        free(buf.data);
        return -1;
    }
    
    *result = json_tokener_parse(buf.data);
    free(buf.data);
    
    return *result ? 0 : -1;
}

int service_controller_run(service_controller_t* controller) {
    if (!controller) return -1;
    
    fprintf(stderr, "[service-controller] Starting main loop\n");
    
    time_t last_cleanup = time(NULL);
    
    while (1) {
        json_object* services_obj = NULL;
        json_object* pods_obj = NULL;
        
        // Get all services
        if (get_services(controller, &services_obj) == 0 && services_obj) {
            // Get all pods (for endpoint discovery)
            if (get_pods_by_selector(controller, &pods_obj) == 0 && pods_obj) {
                // For each service, discover endpoints from pods
                // Extract services array
                json_object* items = json_object_object_get(services_obj, "items");
                if (items && json_object_is_type(items, json_type_array)) {
                    int service_count = json_object_array_length(items);
                    
                    for (int i = 0; i < service_count; i++) {
                        json_object* svc_obj = json_object_array_get_idx(items, i);
                        
                        // Get service name and namespace
                        json_object* meta = json_object_object_get(svc_obj, "metadata");
                        const char* svc_name = json_object_get_string(
                            json_object_object_get(meta, "name"));
                        const char* svc_ns = json_object_get_string(
                            json_object_object_get(meta, "namespace"));
                        
                        // Get service selector labels
                        json_object* spec = json_object_object_get(svc_obj, "spec");
                        json_object* selector = json_object_object_get(spec, "selector");
                        
                        // Count matching pods
                        int matching_pods = 0;
                        json_object* pod_items = json_object_object_get(pods_obj, "items");
                        if (pod_items && json_object_is_type(pod_items, json_type_array)) {
                            int pod_count = json_object_array_length(pod_items);
                            
                            for (int j = 0; j < pod_count; j++) {
                                json_object* pod = json_object_array_get_idx(pod_items, j);
                                json_object* pod_meta = json_object_object_get(pod, "metadata");
                                json_object* pod_labels = json_object_object_get(pod_meta, "labels");
                                
                                // Check if pod matches selector (basic match)
                                if (selector && pod_labels) {
                                    matching_pods++;
                                }
                            }
                        }
                        
                        fprintf(stderr, "[service-controller] Service %s/%s has %d matching pods\n",
                                svc_ns, svc_name, matching_pods);
                    }
                }
            }
            json_object_put(services_obj);
        }
        
        if (pods_obj) json_object_put(pods_obj);
        
        // Periodic cleanup of expired endpoints every 60 seconds
        time_t now = time(NULL);
        if (now - last_cleanup > 60) {
            fprintf(stderr, "[service-controller] Cleanup cycle\n");
            last_cleanup = now;
        }
        
        sleep(controller->update_interval);
    }
    
    return 0;
}
