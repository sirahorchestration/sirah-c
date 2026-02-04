#include "agent.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <curl/curl.h>
#include <json-c/json.h>

// Helper for HTTP requests
typedef struct {
    char* data;
    size_t size;
} response_buffer_t;

static size_t curl_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    response_buffer_t* mem = (response_buffer_t*)userp;
    
    char* ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) {
        fprintf(stderr, "Not enough memory for HTTP response\n");
        return 0;
    }
    
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    
    return realsize;
}

static int api_request(const char* method, const char* url, const char* body, 
                       char** response) {
    CURL* curl = curl_easy_init();
    if (!curl) return -1;
    
    response_buffer_t buf = {0};
    buf.data = malloc(1);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&buf);
    
    if (body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    }
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "API request failed: %s\n", curl_easy_strerror(res));
        free(buf.data);
        curl_easy_cleanup(curl);
        return -1;
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_easy_cleanup(curl);
    
    if (response) {
        *response = buf.data;
    } else {
        free(buf.data);
    }
    
    return http_code;
}

// Agent creation and lifecycle
agent_t* agent_new(const char* api_url, const char* node_name, 
                   const char* node_ip, const char* pod_cidr) {
    agent_t* agent = (agent_t*)malloc(sizeof(agent_t));
    if (!agent) return NULL;
    
    memset(agent, 0, sizeof(agent_t));
    
    agent->api_server_url = strdup(api_url);
    
    // Initialize node info
    agent->node_info = (node_info_t*)malloc(sizeof(node_info_t));
    memset(agent->node_info, 0, sizeof(node_info_t));
    
    agent->node_info->node_name = strdup(node_name);
    agent->node_info->node_ip = strdup(node_ip);
    agent->node_info->pod_cidr = strdup(pod_cidr);
    agent->node_info->status = strdup("NotReady");
    agent->node_info->capacity_pods = 110;
    agent->node_info->allocatable_cpu = 2000;
    agent->node_info->allocatable_memory = 4294967296;  // 4GB
    
    // Initialize pod array
    agent->pod_capacity = 20;
    agent->pods = (managed_pod_t*)malloc(sizeof(managed_pod_t) * agent->pod_capacity);
    agent->pod_count = 0;
    
    // Initialize instance array
    agent->instances = (vm_instance_t*)malloc(sizeof(vm_instance_t) * agent->pod_capacity);
    agent->instance_count = 0;
    
    // Default intervals
    agent->heartbeat_interval = 10;
    agent->pod_sync_interval = 5;
    agent->max_pod_restarts = 5;
    
    agent->running = 0;
    pthread_mutex_init(&agent->lock, NULL);
    
    return agent;
}

void agent_free(agent_t* agent) {
    if (!agent) return;
    
    free(agent->api_server_url);
    
    if (agent->node_info) {
        free(agent->node_info->node_name);
        free(agent->node_info->node_ip);
        free(agent->node_info->pod_cidr);
        free(agent->node_info->status);
        free(agent->node_info);
    }
    
    for (int i = 0; i < agent->pod_count; i++) {
        free(agent->pods[i].name);
        free(agent->pods[i].namespace);
        free(agent->pods[i].status_phase);
        free(agent->pods[i].pod_ip);
    }
    free(agent->pods);
    
    free(agent->instances);
    pthread_mutex_destroy(&agent->lock);
    free(agent);
}

int agent_init(agent_t* agent) {
    if (!agent) return -1;
    
    agent->running = 1;
    
    // Register node with control plane
    if (agent_register_node(agent) != 0) {
        fprintf(stderr, "Failed to register node\n");
        return -1;
    }
    
    return 0;
}

int agent_shutdown(agent_t* agent) {
    if (!agent) return -1;
    
    agent->running = 0;
    return 0;
}

// Node registration
int agent_register_node(agent_t* agent) {
    if (!agent) return -1;
    
    char url[512];
    snprintf(url, sizeof(url), "%s/api/v1/nodes", agent->api_server_url);
    
    // Create node JSON
    json_object* node = json_object_new_object();
    json_object_object_add(node, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(node, "kind", json_object_new_string("Node"));
    
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(agent->node_info->node_name));
    json_object_object_add(node, "metadata", metadata);
    
    json_object* status = json_object_new_object();
    json_object_object_add(status, "phase", json_object_new_string("Ready"));
    json_object_object_add(status, "nodeIP", json_object_new_string(agent->node_info->node_ip));
    json_object_object_add(status, "podCIDR", json_object_new_string(agent->node_info->pod_cidr));
    json_object_object_add(node, "status", status);
    
    const char* body = json_object_to_json_string(node);
    char* response = NULL;
    
    int http_code = api_request("POST", url, body, &response);
    
    json_object_put(node);
    free(response);
    
    if (http_code == 201 || http_code == 200) {
        agent->node_info->status = strdup("Ready");
        printf("Node registered successfully\n");
        return 0;
    } else {
        printf("Failed to register node (HTTP %ld)\n", http_code);
        return -1;
    }
}

// Heartbeat mechanism
int agent_send_heartbeat(agent_t* agent) {
    if (!agent || !agent->running) return -1;
    
    char url[512];
    snprintf(url, sizeof(url), "%s/api/v1/nodes/%s/heartbeat", 
             agent->api_server_url, agent->node_info->node_name);
    
    // Create heartbeat JSON
    json_object* hb = json_object_new_object();
    json_object_object_add(hb, "status", json_object_new_string("Ready"));
    json_object_object_add(hb, "podsRunning", json_object_new_int(agent->pod_count));
    json_object_object_add(hb, "timestamp", json_object_new_int64(time(NULL)));
    
    const char* body = json_object_to_json_string(hb);
    char* response = NULL;
    
    int http_code = api_request("PATCH", url, body, &response);
    
    json_object_put(hb);
    free(response);
    
    agent->node_info->last_heartbeat = time(NULL);
    
    return (http_code >= 200 && http_code < 300) ? 0 : -1;
}

// Pod synchronization
int agent_sync_pods(agent_t* agent) {
    if (!agent || !agent->running) return -1;
    
    char url[512];
    snprintf(url, sizeof(url), 
             "%s/api/v1/pods?fieldSelector=spec.nodeName=%s",
             agent->api_server_url, agent->node_info->node_name);
    
    char* response = NULL;
    int http_code = api_request("GET", url, NULL, &response);
    
    if (http_code != 200) {
        free(response);
        return -1;
    }
    
    // Parse pod list from JSON
    json_object* pods_obj = json_tokener_parse(response);
    if (!pods_obj) {
        free(response);
        return -1;
    }
    
    // For now, just log that we synced
    printf("Pods synced from control plane\n");
    
    json_object_put(pods_obj);
    free(response);
    
    return 0;
}

// Pod lifecycle management
int agent_create_pod(agent_t* agent, const char* pod_json) {
    if (!agent || !pod_json) return -1;
    
    pthread_mutex_lock(&agent->lock);
    
    // Allocate IP
    char* pod_ip = agent_allocate_pod_ip(agent);
    if (!pod_ip) {
        pthread_mutex_unlock(&agent->lock);
        return -1;
    }
    
    // Expand pod array if needed
    if (agent->pod_count >= agent->pod_capacity) {
        agent->pod_capacity *= 2;
        agent->pods = realloc(agent->pods, 
                             sizeof(managed_pod_t) * agent->pod_capacity);
    }
    
    // Add pod to tracking
    managed_pod_t* pod = &agent->pods[agent->pod_count];
    memset(pod, 0, sizeof(managed_pod_t));
    
    pod->name = strdup("pod-name");  // Parse from JSON
    pod->namespace = strdup("default");
    pod->status_phase = strdup("Pending");
    pod->pod_ip = strdup(pod_ip);
    pod->restart_count = 0;
    pod->created_at = time(NULL);
    
    agent->pod_count++;
    
    // Spawn VM instance
    agent->instances[agent->instance_count].pid = -1;  // Would be QEMU PID
    agent->instances[agent->instance_count].status = strdup("Creating");
    agent->instance_count++;
    
    pthread_mutex_unlock(&agent->lock);
    
    return 0;
}

int agent_start_pod(agent_t* agent, const char* pod_name) {
    if (!agent || !pod_name) return -1;
    
    pthread_mutex_lock(&agent->lock);
    
    for (int i = 0; i < agent->pod_count; i++) {
        if (strcmp(agent->pods[i].name, pod_name) == 0) {
            agent->pods[i].status_phase = strdup("Running");
            agent->pods[i].started_at = time(NULL);
            pthread_mutex_unlock(&agent->lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&agent->lock);
    return -1;
}

int agent_stop_pod(agent_t* agent, const char* pod_name) {
    if (!agent || !pod_name) return -1;
    
    pthread_mutex_lock(&agent->lock);
    
    for (int i = 0; i < agent->pod_count; i++) {
        if (strcmp(agent->pods[i].name, pod_name) == 0) {
            agent->pods[i].status_phase = strdup("Failed");
            agent_release_pod_ip(agent, agent->pods[i].pod_ip);
            pthread_mutex_unlock(&agent->lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&agent->lock);
    return -1;
}

int agent_restart_pod(agent_t* agent, const char* pod_name) {
    if (!agent || !pod_name) return -1;
    
    pthread_mutex_lock(&agent->lock);
    
    for (int i = 0; i < agent->pod_count; i++) {
        if (strcmp(agent->pods[i].name, pod_name) == 0) {
            if (agent->pods[i].restart_count >= agent->max_pod_restarts) {
                pthread_mutex_unlock(&agent->lock);
                return -1;
            }
            agent->pods[i].restart_count++;
            agent->pods[i].status_phase = strdup("Running");
            pthread_mutex_unlock(&agent->lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&agent->lock);
    return -1;
}

int agent_update_pod_status(agent_t* agent, const char* pod_name) {
    if (!agent || !pod_name) return -1;
    
    // Find pod locally
    pthread_mutex_lock(&agent->lock);
    managed_pod_t* pod = NULL;
    for (int i = 0; i < agent->pod_count; i++) {
        if (strcmp(agent->pods[i].name, pod_name) == 0) {
            pod = &agent->pods[i];
            break;
        }
    }
    pthread_mutex_unlock(&agent->lock);
    
    if (!pod) return -1;
    
    // Send PATCH to control plane
    char url[512];
    snprintf(url, sizeof(url), 
             "%s/api/v1/namespaces/default/pods/%s",
             agent->api_server_url, pod_name);
    
    json_object* status = json_object_new_object();
    json_object_object_add(status, "phase", json_object_new_string(pod->status_phase));
    json_object_object_add(status, "podIP", json_object_new_string(pod->pod_ip));
    
    const char* body = json_object_to_json_string(status);
    char* response = NULL;
    
    int http_code = api_request("PATCH", url, body, &response);
    
    json_object_put(status);
    free(response);
    
    return (http_code >= 200 && http_code < 300) ? 0 : -1;
}

// IP allocation from node CIDR
char* agent_allocate_pod_ip(agent_t* agent) {
    if (!agent) return NULL;
    
    // Simple round-robin IP allocation
    // In real implementation: parse CIDR, track allocated IPs
    static int ip_counter = 1;
    static char ip[32];
    
    // Example: 10.0.1.0/24 -> 10.0.1.2, 10.0.1.3, ...
    snprintf(ip, sizeof(ip), "10.0.1.%d", ip_counter++);
    
    return ip;
}

void agent_release_pod_ip(agent_t* agent, const char* ip) {
    // Track IP as available for reallocation
    if (!agent || !ip) return;
    
    // In real implementation: add IP back to free pool
}

// Component functions for supervision
int agent_pod_sync_component(void* context) {
    agent_t* agent = (agent_t*)context;
    if (!agent) return 1;
    
    return agent_sync_pods(agent);
}

int agent_heartbeat_component(void* context) {
    agent_t* agent = (agent_t*)context;
    if (!agent) return 1;
    
    return agent_send_heartbeat(agent);
}

int agent_health_check_component(void* context) {
    agent_t* agent = (agent_t*)context;
    if (!agent) return 1;
    
    // Monitor node and pod health
    // Return 0 if healthy, non-zero if unhealthy
    
    return 0;
}

// Main agent loop
int agent_run(agent_t* agent) {
    if (!agent) return -1;
    
    agent->running = 1;
    time_t last_pod_sync = 0;
    time_t last_heartbeat = 0;
    
    while (agent->running) {
        time_t now = time(NULL);
        
        // Pod sync every 5 seconds
        if (now - last_pod_sync >= agent->pod_sync_interval) {
            agent_sync_pods(agent);
            last_pod_sync = now;
        }
        
        // Heartbeat every 10 seconds
        if (now - last_heartbeat >= agent->heartbeat_interval) {
            agent_send_heartbeat(agent);
            last_heartbeat = now;
        }
        
        // Sleep 1 second between checks
        sleep(1);
    }
    
    return 0;
}
