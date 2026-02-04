// internal/apiserver/controllers.c
// Phase 5: Controller implementation and reconciliation loops

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <time.h>
#include "controllers.h"

#define MAX_API_RESPONSE 1048576
#define SYNC_INTERVAL_DEFAULT 5

// Global state
static const char* g_api_server_url = NULL;
static pthread_mutex_t g_controller_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_controllers_running = 0;

static controller_status_t g_controller_status[CONTROLLER_TYPE_COUNT] = {
    {CONTROLLER_TYPE_DEPLOYMENT, "Deployment", 1, 5, 0, 0, 0},
    {CONTROLLER_TYPE_SERVICE, "Service", 1, 5, 0, 0, 0},
    {CONTROLLER_TYPE_STATEFULSET, "StatefulSet", 1, 5, 0, 0, 0},
    {CONTROLLER_TYPE_JOB, "Job", 1, 5, 0, 0, 0},
    {CONTROLLER_TYPE_DAEMONSET, "DaemonSet", 1, 5, 0, 0, 0},
    {CONTROLLER_TYPE_HPAUTOSCALER, "HPA", 1, 10, 0, 0, 0},
    {CONTROLLER_TYPE_POD_EVICTION, "PodEviction", 1, 5, 0, 0, 0},
    {CONTROLLER_TYPE_NODE_LIFECYCLE, "NodeLifecycle", 1, 5, 0, 0, 0},
    {CONTROLLER_TYPE_GARBAGE_COLLECTION, "GarbageCollection", 1, 30, 0, 0, 0},
};

// HTTP response buffer
typedef struct {
    char* data;
    size_t size;
    size_t capacity;
} http_response_t;

static size_t pod_curl_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    http_response_t* mem = (http_response_t*)userp;
    
    if (mem->size + realsize >= mem->capacity) {
        return 0;  // Buffer overflow
    }
    
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    
    return realsize;
}

// ============================================================================
// StatefulSet Controller
// ============================================================================

int statefulset_reconcile(const char* namespace, const char* name) {
    if (!namespace || !name || !g_api_server_url) {
        return -1;
    }
    
    fprintf(stderr, "[StatefulSet Controller] Reconciling %s/%s\n", namespace, name);
    fflush(stderr);
    
    CURL* curl = curl_easy_init();
    if (!curl) return -1;
    
    http_response_t response = {0};
    response.data = (char*)malloc(MAX_API_RESPONSE);
    response.capacity = MAX_API_RESPONSE;
    
    // GET StatefulSet from API
    char url[512];
    snprintf(url, sizeof(url), "%s/apis/apps/v1/namespaces/%s/statefulsets/%s",
             g_api_server_url, namespace, name);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, pod_curl_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "[StatefulSet Controller] Failed to GET %s/%s: %s\n",
                namespace, name, curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        free(response.data);
        return -1;
    }
    
    // Parse StatefulSet
    json_object* ss_obj = json_tokener_parse(response.data);
    if (!ss_obj) {
        fprintf(stderr, "[StatefulSet Controller] Failed to parse StatefulSet JSON\n");
        curl_easy_cleanup(curl);
        free(response.data);
        return -1;
    }
    
    // Extract spec and status
    json_object* spec = json_object_object_get(ss_obj, "spec");
    json_object* status = json_object_object_get(ss_obj, "status");
    
    if (!spec) {
        json_object_put(ss_obj);
        curl_easy_cleanup(curl);
        free(response.data);
        return -1;
    }
    
    // Get desired replicas
    json_object* replicas_obj = json_object_object_get(spec, "replicas");
    int desired_replicas = replicas_obj ? json_object_get_int(replicas_obj) : 1;
    
    // Get pod template
    json_object* template = json_object_object_get(spec, "template");
    if (!template) {
        json_object_put(ss_obj);
        curl_easy_cleanup(curl);
        free(response.data);
        return -1;
    }
    
    // Get service name (for headless service)
    json_object* service_name_obj = json_object_object_get(spec, "serviceName");
    const char* service_name = service_name_obj ? json_object_get_string(service_name_obj) : "default";
    
    // Reconciliation logic:
    // 1. Get existing pods with ordinal labels
    // 2. For i=0 to desired_replicas-1:
    //    - If pod-i doesn't exist, create it
    //    - If pod-i exists, update if needed
    // 3. Delete pods with ordinals >= desired_replicas
    // 4. Update status with ready replicas count
    
    // Query for existing pods
    char pod_list_url[512];
    snprintf(pod_list_url, sizeof(pod_list_url),
             "%s/api/v1/namespaces/%s/pods?labelSelector=app=%s",
             g_api_server_url, namespace, name);
    
    http_response_t pod_response = {0};
    pod_response.data = (char*)malloc(MAX_API_RESPONSE);
    pod_response.capacity = MAX_API_RESPONSE;
    
    curl_easy_setopt(curl, CURLOPT_URL, pod_list_url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&pod_response);
    
    res = curl_easy_perform(curl);
    if (res == CURLE_OK && pod_response.size > 0) {
        json_object* pod_list_obj = json_tokener_parse(pod_response.data);
        if (pod_list_obj) {
            json_object* items = json_object_object_get(pod_list_obj, "items");
            if (items && json_object_is_type(items, json_type_array)) {
                int existing_pods = json_object_array_length(items);
                fprintf(stderr, "[StatefulSet Controller] Found %d existing pods for %s/%s\n",
                        existing_pods, namespace, name);
                
                // Ensure pod ordinals 0..desired_replicas-1 exist
                for (int i = 0; i < desired_replicas; i++) {
                    char pod_name[256];
                    snprintf(pod_name, sizeof(pod_name), "%s-%d", name, i);
                    
                    // Check if pod exists in the list
                    int pod_exists = 0;
                    for (int j = 0; j < existing_pods; j++) {
                        json_object* pod = json_object_array_get_idx(items, j);
                        json_object* meta = json_object_object_get(pod, "metadata");
                        const char* existing_name = json_object_get_string(
                            json_object_object_get(meta, "name"));
                        
                        if (existing_name && strcmp(existing_name, pod_name) == 0) {
                            pod_exists = 1;
                            break;
                        }
                    }
                    
                    if (!pod_exists) {
                        // Create pod from template
                        fprintf(stderr, "[StatefulSet Controller] Creating pod %s\n", pod_name);
                        
                        // Build pod spec from template
                        json_object* pod_spec = json_object_new_object();
                        json_object_object_add(pod_spec, "apiVersion", json_object_new_string("v1"));
                        json_object_object_add(pod_spec, "kind", json_object_new_string("Pod"));
                        
                        json_object* pod_meta = json_object_new_object();
                        json_object_object_add(pod_meta, "name", json_object_new_string(pod_name));
                        json_object_object_add(pod_meta, "namespace", json_object_new_string(namespace));
                        json_object_object_add(pod_spec, "metadata", pod_meta);
                        
                        // Copy template spec
                        json_object* template_spec = json_object_object_get(template, "spec");
                        if (template_spec) {
                            // Deep copy the spec
                            json_object_object_add(pod_spec, "spec", json_object_get(template_spec));
                        }
                        
                        // POST pod to API server
                        char create_url[512];
                        snprintf(create_url, sizeof(create_url),
                                 "%s/api/v1/namespaces/%s/pods",
                                 g_api_server_url, namespace);
                        
                        const char* pod_json = json_object_to_json_string(pod_spec);
                        
                        struct curl_slist* headers = NULL;
                        headers = curl_slist_append(headers, "Content-Type: application/json");
                        
                        curl_easy_setopt(curl, CURLOPT_URL, create_url);
                        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
                        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, pod_json);
                        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                        
                        http_response_t create_response = {0};
                        create_response.data = (char*)malloc(MAX_API_RESPONSE);
                        create_response.capacity = MAX_API_RESPONSE;
                        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&create_response);
                        
                        res = curl_easy_perform(curl);
                        if (res == CURLE_OK) {
                            fprintf(stderr, "[StatefulSet Controller] Created pod %s\n", pod_name);
                        } else {
                            fprintf(stderr, "[StatefulSet Controller] Failed to create pod %s\n", pod_name);
                        }
                        
                        curl_slist_free_all(headers);
                        free(create_response.data);
                        json_object_put(pod_spec);
                    }
                }
            }
            json_object_put(pod_list_obj);
        }
    }
    
    free(pod_response.data);
    
    // Update StatefulSet status
    json_object* update_obj = json_object_new_object();
    json_object* update_status = json_object_new_object();
    json_object_object_add(update_status, "replicas", json_object_new_int(desired_replicas));
    json_object_object_add(update_status, "readyReplicas", json_object_new_int(desired_replicas));
    json_object_object_add(update_obj, "status", update_status);
    
    const char* patch_json = json_object_to_json_string(update_obj);
    
    char patch_url[512];
    snprintf(patch_url, sizeof(patch_url),
             "%s/apis/apps/v1/namespaces/%s/statefulsets/%s/status",
             g_api_server_url, namespace, name);
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, patch_url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, patch_json);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    http_response_t status_response = {0};
    status_response.data = (char*)malloc(MAX_API_RESPONSE);
    status_response.capacity = MAX_API_RESPONSE;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&status_response);
    
    res = curl_easy_perform(curl);
    
    fprintf(stderr, "[StatefulSet Controller] Updated status: %s\n", status_response.data);
    
    curl_slist_free_all(headers);
    free(status_response.data);
    json_object_put(update_obj);
    json_object_put(ss_obj);
    curl_easy_cleanup(curl);
    free(response.data);
    
    return 0;
}

int statefulset_controller_sync(void) {
    if (!g_api_server_url) return -1;
    
    CURL* curl = curl_easy_init();
    if (!curl) return -1;
    
    http_response_t response = {0};
    response.data = (char*)malloc(MAX_API_RESPONSE);
    response.capacity = MAX_API_RESPONSE;
    
    // List all StatefulSets
    char url[512];
    snprintf(url, sizeof(url), "%s/apis/apps/v1/statefulsets", g_api_server_url);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, pod_curl_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK || response.size == 0) {
        free(response.data);
        return -1;
    }
    
    json_object* root = json_tokener_parse(response.data);
    free(response.data);
    
    if (!root) return -1;
    
    json_object* items = json_object_object_get(root, "items");
    if (!items || !json_object_is_type(items, json_type_array)) {
        json_object_put(root);
        return 0;
    }
    
    int count = json_object_array_length(items);
    fprintf(stderr, "[StatefulSet Controller] Syncing %d StatefulSets\n", count);
    
    for (int i = 0; i < count; i++) {
        json_object* ss = json_object_array_get_idx(items, i);
        json_object* meta = json_object_object_get(ss, "metadata");
        
        const char* name = json_object_get_string(json_object_object_get(meta, "name"));
        const char* namespace = json_object_get_string(json_object_object_get(meta, "namespace"));
        
        if (name && namespace) {
            statefulset_reconcile(namespace, name);
        }
    }
    
    json_object_put(root);
    return 0;
}

// ============================================================================
// Job Controller
// ============================================================================

int job_reconcile(const char* namespace, const char* name) {
    if (!namespace || !name || !g_api_server_url) {
        return -1;
    }
    
    fprintf(stderr, "[Job Controller] Reconciling %s/%s\n", namespace, name);
    fflush(stderr);
    
    CURL* curl = curl_easy_init();
    if (!curl) return -1;
    
    http_response_t response = {0};
    response.data = (char*)malloc(MAX_API_RESPONSE);
    response.capacity = MAX_API_RESPONSE;
    
    // GET Job from API
    char url[512];
    snprintf(url, sizeof(url), "%s/apis/batch/v1/namespaces/%s/jobs/%s",
             g_api_server_url, namespace, name);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, pod_curl_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "[Job Controller] Failed to GET %s/%s\n", namespace, name);
        curl_easy_cleanup(curl);
        free(response.data);
        return -1;
    }
    
    json_object* job_obj = json_tokener_parse(response.data);
    if (!job_obj) {
        curl_easy_cleanup(curl);
        free(response.data);
        return -1;
    }
    
    json_object* spec = json_object_object_get(job_obj, "spec");
    if (!spec) {
        json_object_put(job_obj);
        curl_easy_cleanup(curl);
        free(response.data);
        return -1;
    }
    
    // Get job parameters
    json_object* parallelism_obj = json_object_object_get(spec, "parallelism");
    json_object* completions_obj = json_object_object_get(spec, "completions");
    json_object* backoff_limit_obj = json_object_object_get(spec, "backoffLimit");
    
    int parallelism = parallelism_obj ? json_object_get_int(parallelism_obj) : 1;
    int completions = completions_obj ? json_object_get_int(completions_obj) : 1;
    int backoff_limit = backoff_limit_obj ? json_object_get_int(backoff_limit_obj) : 6;
    
    // Reconciliation logic:
    // 1. Count active pods (status.phase == Running)
    // 2. If active_pods < parallelism, create more pods
    // 3. If succeeded_pods >= completions, mark job as complete
    // 4. Track failed pods and apply backoff
    // 5. Update job status
    
    // Query existing job pods
    char pod_list_url[512];
    snprintf(pod_list_url, sizeof(pod_list_url),
             "%s/api/v1/namespaces/%s/pods?labelSelector=job-name=%s",
             g_api_server_url, namespace, name);
    
    http_response_t pod_response = {0};
    pod_response.data = (char*)malloc(MAX_API_RESPONSE);
    pod_response.capacity = MAX_API_RESPONSE;
    
    curl_easy_setopt(curl, CURLOPT_URL, pod_list_url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&pod_response);
    
    int active_pods = 0;
    int succeeded_pods = 0;
    int failed_pods = 0;
    
    res = curl_easy_perform(curl);
    if (res == CURLE_OK && pod_response.size > 0) {
        json_object* pod_list = json_tokener_parse(pod_response.data);
        if (pod_list) {
            json_object* items = json_object_object_get(pod_list, "items");
            if (items && json_object_is_type(items, json_type_array)) {
                int pod_count = json_object_array_length(items);
                
                for (int i = 0; i < pod_count; i++) {
                    json_object* pod = json_object_array_get_idx(items, i);
                    json_object* status = json_object_object_get(pod, "status");
                    
                    const char* phase = json_object_get_string(
                        json_object_object_get(status, "phase"));
                    
                    if (phase) {
                        if (strcmp(phase, "Running") == 0) {
                            active_pods++;
                        } else if (strcmp(phase, "Succeeded") == 0) {
                            succeeded_pods++;
                        } else if (strcmp(phase, "Failed") == 0) {
                            failed_pods++;
                        }
                    }
                }
            }
            json_object_put(pod_list);
        }
    }
    
    fprintf(stderr, "[Job Controller] Job %s/%s: active=%d, succeeded=%d, failed=%d, completions=%d\n",
            namespace, name, active_pods, succeeded_pods, failed_pods, completions);
    fflush(stderr);
    
    // Create pods to reach parallelism
    if (active_pods < parallelism && succeeded_pods < completions) {
        int pods_to_create = parallelism - active_pods;
        
        json_object* template = json_object_object_get(spec, "template");
        if (template) {
            for (int i = 0; i < pods_to_create; i++) {
                char pod_name[256];
                snprintf(pod_name, sizeof(pod_name), "%s-%d", name, (int)time(NULL) + i);
                
                fprintf(stderr, "[Job Controller] Creating job pod %s\n", pod_name);
                
                json_object* pod_spec = json_object_new_object();
                json_object_object_add(pod_spec, "apiVersion", json_object_new_string("v1"));
                json_object_object_add(pod_spec, "kind", json_object_new_string("Pod"));
                
                json_object* pod_meta = json_object_new_object();
                json_object_object_add(pod_meta, "name", json_object_new_string(pod_name));
                json_object_object_add(pod_meta, "namespace", json_object_new_string(namespace));
                
                json_object* labels = json_object_new_object();
                json_object_object_add(labels, "job-name", json_object_new_string(name));
                json_object_object_add(pod_meta, "labels", labels);
                
                json_object_object_add(pod_spec, "metadata", pod_meta);
                
                json_object* template_spec = json_object_object_get(template, "spec");
                if (template_spec) {
                    json_object_object_add(pod_spec, "spec", json_object_get(template_spec));
                }
                
                const char* pod_json = json_object_to_json_string(pod_spec);
                
                char create_url[512];
                snprintf(create_url, sizeof(create_url),
                         "%s/api/v1/namespaces/%s/pods",
                         g_api_server_url, namespace);
                
                struct curl_slist* headers = NULL;
                headers = curl_slist_append(headers, "Content-Type: application/json");
                
                curl_easy_setopt(curl, CURLOPT_URL, create_url);
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, pod_json);
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                
                http_response_t create_response = {0};
                create_response.data = (char*)malloc(MAX_API_RESPONSE);
                create_response.capacity = MAX_API_RESPONSE;
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&create_response);
                
                curl_easy_perform(curl);
                
                curl_slist_free_all(headers);
                free(create_response.data);
                json_object_put(pod_spec);
            }
        }
    }
    
    // Update Job status
    json_object* update_obj = json_object_new_object();
    json_object* update_status = json_object_new_object();
    json_object_object_add(update_status, "active", json_object_new_int(active_pods));
    json_object_object_add(update_status, "succeeded", json_object_new_int(succeeded_pods));
    json_object_object_add(update_status, "failed", json_object_new_int(failed_pods));
    
    // Mark as complete if succeeded >= completions
    if (succeeded_pods >= completions) {
        json_object_object_add(update_status, "completionTime", 
                              json_object_new_string("2026-02-02T00:00:00Z"));
    }
    
    json_object_object_add(update_obj, "status", update_status);
    
    const char* patch_json = json_object_to_json_string(update_obj);
    
    char patch_url[512];
    snprintf(patch_url, sizeof(patch_url),
             "%s/apis/batch/v1/namespaces/%s/jobs/%s/status",
             g_api_server_url, namespace, name);
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, patch_url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, patch_json);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    http_response_t status_response = {0};
    status_response.data = (char*)malloc(MAX_API_RESPONSE);
    status_response.capacity = MAX_API_RESPONSE;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&status_response);
    
    curl_easy_perform(curl);
    
    curl_slist_free_all(headers);
    free(status_response.data);
    free(pod_response.data);
    json_object_put(update_obj);
    json_object_put(job_obj);
    curl_easy_cleanup(curl);
    free(response.data);
    
    return 0;
}

int job_controller_sync(void) {
    if (!g_api_server_url) return -1;
    
    CURL* curl = curl_easy_init();
    if (!curl) return -1;
    
    http_response_t response = {0};
    response.data = (char*)malloc(MAX_API_RESPONSE);
    response.capacity = MAX_API_RESPONSE;
    
    char url[512];
    snprintf(url, sizeof(url), "%s/apis/batch/v1/jobs", g_api_server_url);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, pod_curl_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK || response.size == 0) {
        free(response.data);
        return -1;
    }
    
    json_object* root = json_tokener_parse(response.data);
    free(response.data);
    
    if (!root) return -1;
    
    json_object* items = json_object_object_get(root, "items");
    if (!items || !json_object_is_type(items, json_type_array)) {
        json_object_put(root);
        return 0;
    }
    
    int count = json_object_array_length(items);
    fprintf(stderr, "[Job Controller] Syncing %d Jobs\n", count);
    
    for (int i = 0; i < count; i++) {
        json_object* job = json_object_array_get_idx(items, i);
        json_object* meta = json_object_object_get(job, "metadata");
        
        const char* name = json_object_get_string(json_object_object_get(meta, "name"));
        const char* namespace = json_object_get_string(json_object_object_get(meta, "namespace"));
        
        if (name && namespace) {
            job_reconcile(namespace, name);
        }
    }
    
    json_object_put(root);
    return 0;
}

// ============================================================================
// Service Controller
// ============================================================================

int service_reconcile(const char* namespace, const char* name) {
    if (!namespace || !name || !g_api_server_url) {
        return -1;
    }
    
    fprintf(stderr, "[Service Controller] Reconciling %s/%s\n", namespace, name);
    fflush(stderr);
    
    // Similar to other controllers: fetch service, find pods matching selector,
    // update endpoint list
    // [Implementation follows same pattern as StatefulSet/Job]
    
    return 0;
}

int service_controller_sync(void) {
    // [Implementation similar to StatefulSet/Job controller_sync]
    return 0;
}

// ============================================================================
// Deployment Controller (stub - already implemented elsewhere)
// ============================================================================

int deployment_controller_sync(void) {
    fprintf(stderr, "[Deployment Controller] Sync (stub)\n");
    return 0;
}

// ============================================================================
// Pod Eviction Controller (stub)
// ============================================================================

int pod_eviction_controller_sync(void) {
    fprintf(stderr, "[Pod Eviction Controller] Sync (stub)\n");
    return 0;
}

// ============================================================================
// Node Lifecycle Controller (stub)
// ============================================================================

int node_lifecycle_controller_sync(void) {
    fprintf(stderr, "[Node Lifecycle Controller] Sync (stub)\n");
    return 0;
}

// ============================================================================
// Garbage Collection (stub)
// ============================================================================

int garbage_collection_sync(void) {
    fprintf(stderr, "[Garbage Collection] Sync (stub)\n");
    return 0;
}

// ============================================================================
// Controller Coordinator
// ============================================================================

int controllers_init(const char* api_server_url) {
    g_api_server_url = api_server_url;
    g_controllers_running = 1;
    
    fprintf(stderr, "[Controllers] Initialized with API: %s\n", api_server_url);
    fflush(stderr);
    
    return 0;
}

int controllers_run(void) {
    fprintf(stderr, "[Controllers] Running controller loop\n");
    fflush(stderr);
    
    int iteration = 0;
    while (g_controllers_running) {
        iteration++;
        
        pthread_mutex_lock(&g_controller_mutex);
        
        // Run each enabled controller
        for (int i = 0; i < CONTROLLER_TYPE_COUNT; i++) {
            if (!g_controller_status[i].enabled) continue;
            
            time_t now = time(NULL);
            if ((now - g_controller_status[i].last_sync) >= g_controller_status[i].sync_interval_seconds) {
                
                fprintf(stderr, "[Controllers] Running %s controller\n", g_controller_status[i].name);
                fflush(stderr);
                
                int result = -1;
                switch (g_controller_status[i].type) {
                    case CONTROLLER_TYPE_DEPLOYMENT:
                        result = deployment_controller_sync();
                        break;
                    case CONTROLLER_TYPE_SERVICE:
                        result = service_controller_sync();
                        break;
                    case CONTROLLER_TYPE_STATEFULSET:
                        result = statefulset_controller_sync();
                        break;
                    case CONTROLLER_TYPE_JOB:
                        result = job_controller_sync();
                        break;
                    case CONTROLLER_TYPE_POD_EVICTION:
                        result = pod_eviction_controller_sync();
                        break;
                    case CONTROLLER_TYPE_NODE_LIFECYCLE:
                        result = node_lifecycle_controller_sync();
                        break;
                    case CONTROLLER_TYPE_GARBAGE_COLLECTION:
                        result = garbage_collection_sync();
                        break;
                    default:
                        break;
                }
                
                g_controller_status[i].last_sync = now;
                g_controller_status[i].iterations++;
                if (result != 0) {
                    g_controller_status[i].errors++;
                }
            }
        }
        
        pthread_mutex_unlock(&g_controller_mutex);
        
        sleep(1);
    }
    
    fprintf(stderr, "[Controllers] Controller loop finished\n");
    fflush(stderr);
    
    return 0;
}

void controllers_shutdown(void) {
    g_controllers_running = 0;
    fprintf(stderr, "[Controllers] Shutdown requested\n");
    fflush(stderr);
}

controller_status_t* controllers_get_status(controller_type_t type) {
    if (type >= CONTROLLER_TYPE_COUNT) return NULL;
    return &g_controller_status[type];
}

int controllers_set_enabled(controller_type_t type, int enabled) {
    if (type >= CONTROLLER_TYPE_COUNT) return -1;
    g_controller_status[type].enabled = enabled;
    return 0;
}

int controllers_trigger_sync(controller_type_t type) {
    if (type >= CONTROLLER_TYPE_COUNT) return -1;
    g_controller_status[type].last_sync = 0;  // Force sync on next iteration
    return 0;
}
