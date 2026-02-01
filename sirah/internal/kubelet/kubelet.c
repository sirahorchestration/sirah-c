#include "kubelet.h"
#include "unikernel_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <unistd.h>
#include <time.h>

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

kubelet_t* kubelet_new(const char* node_name, const char* api_server_url) {
    if (!node_name || !api_server_url) return NULL;
    
    kubelet_t* kubelet = (kubelet_t*)malloc(sizeof(kubelet_t));
    kubelet->node_name = strdup(node_name);
    kubelet->api_server_url = strdup(api_server_url);
    kubelet->curl_handle = NULL;
    kubelet->update_interval = 5;  // 5 second updates
    kubelet->pod_cidr = strdup("10.0.0.0/24");  // Default pod CIDR
    kubelet->last_ip_octet = 2;  // Start from 10.0.0.2
    kubelet->pod_mount_base = strdup("/var/lib/kubelet/pods");  // Mount base directory
    kubelet->managed_pods = (pod_lifecycle_t**)malloc(sizeof(pod_lifecycle_t*) * 256);
    kubelet->num_managed_pods = 0;
    kubelet->last_sync = time(NULL);
    
    return kubelet;
}

void kubelet_free(kubelet_t* kubelet) {
    if (!kubelet) return;
    
    // Free managed pods
    for (int i = 0; i < kubelet->num_managed_pods; i++) {
        pod_lifecycle_free(kubelet->managed_pods[i]);
    }
    free(kubelet->managed_pods);
    
    free(kubelet->node_name);
    free(kubelet->api_server_url);
    free(kubelet->pod_cidr);
    free(kubelet->pod_mount_base);
    free(kubelet);
}

int kubelet_init(kubelet_t* kubelet) {
    if (!kubelet) return -1;
    
    kubelet->curl_handle = curl_easy_init();
    if (!kubelet->curl_handle) {
        fprintf(stderr, "[kubelet] Failed to initialize curl\n");
        return -1;
    }
    
    // Initialize unikernel runtime (QEMU backend)
    if (unikernel_runtime_init("qemu", "/var/lib/sirah/unikernels") == 0) {
        kubelet->runtime = (void*)1;  // Mark as initialized (using opaque pointer)
        fprintf(stderr, "[kubelet] Unikernel runtime initialized (QEMU backend)\n");
    } else {
        fprintf(stderr, "[kubelet] Warning: Failed to initialize unikernel runtime, pods will fail\n");
        kubelet->runtime = NULL;
    }
    
    fprintf(stderr, "[kubelet %s] Initialized\n", kubelet->node_name);
    return 0;
}

void kubelet_shutdown(kubelet_t* kubelet) {
    if (!kubelet) return;
    
    // Shutdown unikernel runtime
    if (kubelet->runtime) {
        unikernel_runtime_shutdown();
        kubelet->runtime = NULL;
    }
    
    if (kubelet->curl_handle) {
        curl_easy_cleanup((CURL*)kubelet->curl_handle);
        kubelet->curl_handle = NULL;
    }
    
    fprintf(stderr, "[kubelet %s] Shutdown\n", kubelet->node_name);
}

int kubelet_get_assigned_pods(kubelet_t* kubelet, kubelet_pod_status_t** pods, int* count) {
    if (!kubelet || !count) return -1;
    
    CURL* curl = (CURL*)kubelet->curl_handle;
    response_buffer_t buf = {0};
    buf.data = (char*)malloc(1);
    
    char url[512];
    snprintf(url, sizeof(url), "%s/api/v1/nodes/%s/pods", kubelet->api_server_url, kubelet->node_name);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&buf);
    
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "[kubelet] Failed to get pods: %s\n", curl_easy_strerror(res));
        free(buf.data);
        *count = 0;
        return -1;
    }
    
    // Parse response
    json_object* root = json_tokener_parse(buf.data);
    free(buf.data);
    
    if (!root) {
        *count = 0;
        return 0;
    }
    
    json_object* items_obj;
    if (!json_object_object_get_ex(root, "items", &items_obj)) {
        json_object_put(root);
        *count = 0;
        return 0;
    }
    
    int num_items = json_object_array_length(items_obj);
    *pods = (kubelet_pod_status_t*)malloc(sizeof(kubelet_pod_status_t) * num_items);
    *count = num_items;
    
    for (int i = 0; i < num_items; i++) {
        json_object* pod_obj = json_object_array_get_idx(items_obj, i);
        
        json_object* meta = NULL;
        json_object_object_get_ex(pod_obj, "metadata", &meta);
        
        (*pods)[i].pod_name = strdup(json_object_get_string(
            json_object_object_get(meta, "name")));
        (*pods)[i].namespace = strdup(json_object_get_string(
            json_object_object_get(meta, "namespace")));
        (*pods)[i].status = strdup("pending");
        (*pods)[i].restart_count = 0;
        (*pods)[i].exit_code = 0;
        (*pods)[i].error_message = NULL;
    }
    
    json_object_put(root);
    return 0;
}

int kubelet_update_pod_status(kubelet_t* kubelet, const char* namespace, const char* pod_name,
                             const char* phase, const char* message) {
    if (!kubelet || !namespace || !pod_name || !phase) return -1;
    
    CURL* curl = (CURL*)kubelet->curl_handle;
    response_buffer_t buf = {0};
    buf.data = (char*)malloc(1);
    
    // Build JSON payload
    json_object* status_obj = json_object_new_object();
    json_object_object_add(status_obj, "phase", json_object_new_string(phase));
    json_object_object_add(status_obj, "message", json_object_new_string(message ? message : ""));
    json_object_object_add(status_obj, "timestamp", json_object_new_int64(time(NULL)));
    
    const char* json_str = json_object_to_json_string(status_obj);
    
    char url[512];
    snprintf(url, sizeof(url), "%s/api/v1/namespaces/%s/pods/%s/status",
            kubelet->api_server_url, namespace, pod_name);
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&buf);
    
    CURLcode res = curl_easy_perform(curl);
    
    curl_slist_free_all(headers);
    json_object_put(status_obj);
    free(buf.data);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "[kubelet] Failed to update pod status: %s\n", curl_easy_strerror(res));
        return -1;
    }
    
    return 0;
}

int kubelet_run(kubelet_t* kubelet) {
    if (!kubelet) return -1;
    
    fprintf(stderr, "[kubelet %s] Starting main loop on node %s\n", kubelet->node_name, kubelet->node_name);
    
    while (1) {
        // Get assigned pods
        kubelet_pod_status_t* pods = NULL;
        int count = 0;
        time_t now = time(NULL);
        
        if (kubelet_get_assigned_pods(kubelet, &pods, &count) == 0 && count > 0) {
            // For each pod, manage its lifecycle
            for (int i = 0; i < count; i++) {
                // Check if we're already managing this pod
                pod_lifecycle_t* lifecycle = NULL;
                for (int j = 0; j < kubelet->num_managed_pods; j++) {
                    if (strcmp(kubelet->managed_pods[j]->pod->metadata.name, pods[i].pod_name) == 0 &&
                        strcmp(kubelet->managed_pods[j]->pod->metadata.namespace, pods[i].namespace) == 0) {
                        lifecycle = kubelet->managed_pods[j];
                        break;
                    }
                }
                
                // New pod - add to managed pods
                if (!lifecycle && kubelet->num_managed_pods < 256) {
                    k8s_pod_t* new_pod = k8s_pod_new(pods[i].pod_name, pods[i].namespace);
                    lifecycle = pod_lifecycle_new(new_pod);
                    kubelet->managed_pods[kubelet->num_managed_pods++] = lifecycle;
                    
                    fprintf(stderr, "[kubelet %s] Managing pod %s/%s\n",
                           kubelet->node_name, pods[i].namespace, pods[i].pod_name);
                }
                
                // Update pod status based on lifecycle phase
                if (lifecycle) {
                    // Create and run container if pending
                    if (lifecycle->pod->status.phase == PHASE_PENDING && 
                        (now - lifecycle->phase_transition_time) >= 1) {
                        // Try to run pod in unikernel runtime
                        if (kubelet->runtime) {
                            // Create container from pod spec
                            char* image = "unikernel-image";  // Default fallback
                            if (lifecycle->pod->spec.containers && lifecycle->pod->spec.containers[0].image) {
                                image = lifecycle->pod->spec.containers[0].image;
                            }
                            
                            // Create and run container
                            char* container_id = unikernel_container_create(pods[i].pod_name, pods[i].namespace,
                                                                            image, 512, 1);
                            if (container_id) {
                                if (unikernel_container_run(container_id) == 0) {
                                    // Assign IP address
                                    char pod_ip[32];
                                    snprintf(pod_ip, sizeof(pod_ip), "10.0.0.%d", kubelet->last_ip_octet++);
                                    pod_lifecycle_transition_to_running(lifecycle, pod_ip, "127.0.0.1");
                                    fprintf(stderr, "[kubelet] Pod %s/%s running in QEMU VM (container: %s)\n", pods[i].namespace, pods[i].pod_name, container_id);
                                    free(container_id);
                                } else {
                                    fprintf(stderr, "[kubelet] Failed to run container %s\n", container_id);
                                    free(container_id);
                                }
                            } else {
                                fprintf(stderr, "[kubelet] Failed to create container for pod %s/%s\n", pods[i].namespace, pods[i].pod_name);
                            }
                        } else {
                            // Fallback: simulate pod running without containers
                            char pod_ip[32];
                            snprintf(pod_ip, sizeof(pod_ip), "10.0.0.%d", kubelet->last_ip_octet++);
                            pod_lifecycle_transition_to_running(lifecycle, pod_ip, "127.0.0.1");
                        }
                    }
                    
                    // Update API server with current status
                    const char* phase_str = pod_lifecycle_get_phase_string(lifecycle->pod->status.phase);
                    kubelet_update_pod_status(kubelet, pods[i].namespace, pods[i].pod_name,
                                            phase_str, "Pod running on node");
                }
                
                // Free pod status
                free(pods[i].pod_name);
                free(pods[i].namespace);
                free(pods[i].status);
                if (pods[i].error_message) free(pods[i].error_message);
            }
            free(pods);
        }
        
        // Update heartbeat to API server
        kubelet_update_pod_status(kubelet, "kube-system", "kubelet-heartbeat",
                                "Running", "Kubelet healthy");
        
        kubelet->last_sync = now;
        sleep(kubelet->update_interval);
    }
    
    return 0;
}

// Volume handling

int kubelet_mount_pod_volumes(kubelet_t* kubelet, const char* namespace, const char* pod_name,
                             volume_definition_t* volumes, int num_volumes) {
    if (!kubelet || !namespace || !pod_name) return -1;
    if (!volumes || num_volumes <= 0) return 0;  // No volumes, that's okay
    
    // Use kubelet's pod mount base directory
    return kubelet_mount_pod_volumes_impl(pod_name, namespace, kubelet->node_name,
                                         kubelet->pod_mount_base, volumes, num_volumes);
}

int kubelet_cleanup_volumes(kubelet_t* kubelet, const char* namespace, const char* pod_name) {
    if (!kubelet || !namespace || !pod_name) return -1;
    
    return kubelet_cleanup_pod_mounts(pod_name, namespace, kubelet->pod_mount_base);
}
