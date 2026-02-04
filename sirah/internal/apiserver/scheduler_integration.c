// internal/apiserver/scheduler_integration.c
// Scheduler integration - runs scheduling loop in API server

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include "scheduler_integration.h"

typedef struct {
    int initialized;
    int running;
    pthread_t scheduler_thread;
    int total_scheduled;
    int total_errors;
    time_t last_sync;
    int sync_interval;
} scheduler_context_t;

static scheduler_context_t g_scheduler = {0};
static pthread_mutex_t scheduler_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    char* data;
    size_t size;
} http_response_t;

static size_t curl_write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    http_response_t* buf = (http_response_t*)userp;
    
    char* ptr = realloc(buf->data, buf->size + realsize + 1);
    if (!ptr) return 0;
    
    buf->data = ptr;
    memcpy(&(buf->data[buf->size]), contents, realsize);
    buf->size += realsize;
    buf->data[buf->size] = 0;
    
    return realsize;
}

static json_object* scheduler_get_nodes(void) {
    CURL* curl = curl_easy_init();
    if (!curl) return NULL;
    
    http_response_t response = {0};
    response.data = malloc(1);
    response.size = 0;
    
    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:6443/api/v1/nodes");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "[SCHEDULER] Failed to fetch nodes: %s\n", curl_easy_strerror(res));
        fflush(stderr);
        free(response.data);
        return NULL;
    }
    
    json_object* root = json_tokener_parse(response.data);
    free(response.data);
    return root;
}

static json_object* scheduler_get_pending_pods(const char* namespace) {
    if (!namespace) namespace = "default";
    
    CURL* curl = curl_easy_init();
    if (!curl) return NULL;
    
    http_response_t response = {0};
    response.data = malloc(1);
    response.size = 0;
    
    char url[512];
    snprintf(url, sizeof(url), "http://localhost:6443/api/v1/namespaces/%s/pods", namespace);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "[SCHEDULER] Failed to fetch pods: %s\n", curl_easy_strerror(res));
        fflush(stderr);
        free(response.data);
        return NULL;
    }
    
    json_object* root = json_tokener_parse(response.data);
    free(response.data);
    return root;
}

static char* scheduler_select_node(json_object* nodes_obj) {
    if (!nodes_obj) return NULL;
    
    json_object* items = NULL;
    if (!json_object_object_get_ex(nodes_obj, "items", &items) ||
        !json_object_is_type(items, json_type_array)) {
        return NULL;
    }
    
    int num_nodes = json_object_array_length(items);
    if (num_nodes == 0) {
        fprintf(stderr, "[SCHEDULER] No nodes available\n");
        return NULL;
    }
    
    json_object* first_node = json_object_array_get_idx(items, 0);
    if (!first_node) return NULL;
    
    json_object* metadata = NULL;
    if (!json_object_object_get_ex(first_node, "metadata", &metadata)) {
        return NULL;
    }
    
    json_object* name_obj = NULL;
    if (!json_object_object_get_ex(metadata, "name", &name_obj)) {
        return NULL;
    }
    
    const char* node_name = json_object_get_string(name_obj);
    if (!node_name) return NULL;
    
    char* selected = malloc(strlen(node_name) + 1);
    strcpy(selected, node_name);
    return selected;
}

static int scheduler_bind_pod_to_node(const char* namespace, const char* pod_name,
                                      const char* node_name) {
    if (!namespace || !pod_name || !node_name) return -1;
    
    json_object* binding = json_object_new_object();
    json_object_object_add(binding, "nodeName", json_object_new_string(node_name));
    
    const char* binding_json = json_object_to_json_string(binding);
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        json_object_put(binding);
        return -1;
    }
    
    http_response_t response = {0};
    response.data = malloc(1);
    response.size = 0;
    
    char url[512];
    snprintf(url, sizeof(url), "http://localhost:6443/api/v1/namespaces/%s/pods/%s/bind",
             namespace, pod_name);
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, binding_json);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(response.data);
    json_object_put(binding);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "[SCHEDULER] Failed to bind pod: %s\n", curl_easy_strerror(res));
        fflush(stderr);
        return -1;
    }
    
    if (http_code != 200) {
        fprintf(stderr, "[SCHEDULER] Bind failed with HTTP %ld\n", http_code);
        fflush(stderr);
        return -1;
    }
    
    fprintf(stderr, "[SCHEDULER] Bound pod %s/%s to node %s\n", namespace, pod_name, node_name);
    fflush(stderr);
    
    return 0;
}

static int scheduler_run_cycle(void) {
    fprintf(stderr, "[SCHEDULER] Starting scheduling cycle\n");
    fflush(stderr);
    
    json_object* nodes = scheduler_get_nodes();
    if (!nodes) {
        fprintf(stderr, "[SCHEDULER] Failed to get nodes\n");
        fflush(stderr);
        return -1;
    }
    
    json_object* pods = scheduler_get_pending_pods("default");
    if (!pods) {
        fprintf(stderr, "[SCHEDULER] Failed to get pods\n");
        fflush(stderr);
        json_object_put(nodes);
        return -1;
    }
    
    json_object* items = NULL;
    if (!json_object_object_get_ex(pods, "items", &items) ||
        !json_object_is_type(items, json_type_array)) {
        fprintf(stderr, "[SCHEDULER] Invalid pods response\n");
        fflush(stderr);
        json_object_put(nodes);
        json_object_put(pods);
        return -1;
    }
    
    int num_pods = json_object_array_length(items);
    fprintf(stderr, "[SCHEDULER] Found %d pods in default namespace\n", num_pods);
    fflush(stderr);
    
    int scheduled = 0;
    for (int i = 0; i < num_pods; i++) {
        json_object* pod = json_object_array_get_idx(items, i);
        if (!pod) continue;
        
        json_object* metadata = NULL;
        if (!json_object_object_get_ex(pod, "metadata", &metadata)) {
            continue;
        }
        
        json_object* name_obj = NULL;
        if (!json_object_object_get_ex(metadata, "name", &name_obj)) {
            continue;
        }
        const char* pod_name = json_object_get_string(name_obj);
        
        json_object* ns_obj = NULL;
        if (!json_object_object_get_ex(metadata, "namespace", &ns_obj)) {
            continue;
        }
        const char* namespace = json_object_get_string(ns_obj);
        
        json_object* status = NULL;
        const char* phase = "Unknown";
        if (json_object_object_get_ex(pod, "status", &status)) {
            json_object* phase_obj = NULL;
            if (json_object_object_get_ex(status, "phase", &phase_obj)) {
                phase = json_object_get_string(phase_obj);
            }
        }
        
        json_object* spec = NULL;
        const char* bound_node = NULL;
        if (json_object_object_get_ex(pod, "spec", &spec)) {
            json_object* node_obj = NULL;
            if (json_object_object_get_ex(spec, "nodeName", &node_obj)) {
                bound_node = json_object_get_string(node_obj);
            }
        }
        
        fprintf(stderr, "[SCHEDULER] Pod %s/%s: phase=%s bound_node=%s\n",
                namespace, pod_name, phase, bound_node ? bound_node : "none");
        fflush(stderr);
        
        if (strcmp(phase, "Pending") == 0 && !bound_node) {
            fprintf(stderr, "[SCHEDULER] Scheduling pending pod: %s/%s\n", namespace, pod_name);
            fflush(stderr);
            
            char* selected_node = scheduler_select_node(nodes);
            if (selected_node) {
                if (scheduler_bind_pod_to_node(namespace, pod_name, selected_node) == 0) {
                    scheduled++;
                    g_scheduler.total_scheduled++;
                } else {
                    g_scheduler.total_errors++;
                }
                free(selected_node);
            } else {
                fprintf(stderr, "[SCHEDULER] Failed to select node for pod %s/%s\n",
                        namespace, pod_name);
                fflush(stderr);
                g_scheduler.total_errors++;
            }
        }
    }
    
    fprintf(stderr, "[SCHEDULER] Scheduling cycle complete: scheduled %d pods\n", scheduled);
    fflush(stderr);
    
    if (nodes) json_object_put(nodes);
    if (pods) json_object_put(pods);
    
    return 0;
}

static void* scheduler_loop_thread(void* arg) {
    (void)arg;
    
    fprintf(stderr, "[SCHEDULER THREAD] Started\n");
    fflush(stderr);
    
    while (g_scheduler.running) {
        scheduler_run_cycle();
        g_scheduler.last_sync = time(NULL);
        sleep(g_scheduler.sync_interval);
    }
    
    fprintf(stderr, "[SCHEDULER THREAD] Stopped\n");
    fflush(stderr);
    
    return NULL;
}

int scheduler_integration_init(void) {
    pthread_mutex_lock(&scheduler_mutex);
    
    if (g_scheduler.initialized) {
        pthread_mutex_unlock(&scheduler_mutex);
        return 0;
    }
    
    g_scheduler.initialized = 1;
    g_scheduler.running = 0;
    g_scheduler.total_scheduled = 0;
    g_scheduler.total_errors = 0;
    g_scheduler.last_sync = time(NULL);
    g_scheduler.sync_interval = 5;
    
    fprintf(stderr, "[SCHEDULER] Integration initialized\n");
    fflush(stderr);
    
    pthread_mutex_unlock(&scheduler_mutex);
    
    return 0;
}

int scheduler_integration_start(void) {
    pthread_mutex_lock(&scheduler_mutex);
    
    if (!g_scheduler.initialized) {
        pthread_mutex_unlock(&scheduler_mutex);
        return -1;
    }
    
    if (g_scheduler.running) {
        pthread_mutex_unlock(&scheduler_mutex);
        return 0;
    }
    
    g_scheduler.running = 1;
    int ret = pthread_create(&g_scheduler.scheduler_thread, NULL, scheduler_loop_thread, NULL);
    
    if (ret != 0) {
        fprintf(stderr, "[SCHEDULER] Failed to create thread: %d\n", ret);
        fflush(stderr);
        g_scheduler.running = 0;
        pthread_mutex_unlock(&scheduler_mutex);
        return -1;
    }
    
    fprintf(stderr, "[SCHEDULER] Control loop started\n");
    fflush(stderr);
    
    pthread_mutex_unlock(&scheduler_mutex);
    
    return 0;
}

void scheduler_integration_stop(void) {
    pthread_mutex_lock(&scheduler_mutex);
    
    if (!g_scheduler.running) {
        pthread_mutex_unlock(&scheduler_mutex);
        return;
    }
    
    g_scheduler.running = 0;
    
    pthread_mutex_unlock(&scheduler_mutex);
    
    pthread_join(g_scheduler.scheduler_thread, NULL);
    
    fprintf(stderr, "[SCHEDULER] Control loop stopped\n");
    fflush(stderr);
}

int scheduler_schedule_pod(const char* namespace, const char* pod_name) {
    if (!namespace || !pod_name) return -1;
    
    CURL* curl = curl_easy_init();
    if (!curl) return -1;
    
    http_response_t response = {0};
    response.data = malloc(1);
    response.size = 0;
    
    char url[512];
    snprintf(url, sizeof(url), "http://localhost:6443/api/v1/namespaces/%s/pods/%s",
             namespace, pod_name);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        free(response.data);
        return -1;
    }
    
    json_object* pod = json_tokener_parse(response.data);
    free(response.data);
    
    if (!pod) return -1;
    
    json_object* nodes = scheduler_get_nodes();
    if (!nodes) {
        json_object_put(pod);
        return -1;
    }
    
    char* node = scheduler_select_node(nodes);
    int ret = -1;
    
    if (node) {
        ret = scheduler_bind_pod_to_node(namespace, pod_name, node);
        free(node);
    }
    
    json_object_put(nodes);
    json_object_put(pod);
    
    return ret;
}

scheduler_stats_t scheduler_get_stats(void) {
    scheduler_stats_t stats = {0};
    
    pthread_mutex_lock(&scheduler_mutex);
    stats.total_pods_scheduled = g_scheduler.total_scheduled;
    stats.total_scheduling_errors = g_scheduler.total_errors;
    stats.last_sync_time = g_scheduler.last_sync;
    pthread_mutex_unlock(&scheduler_mutex);
    
    return stats;
}
