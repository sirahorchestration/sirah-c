// internal/kubelet/kubelet_lifecycle.c
// Kubelet pod lifecycle management implementation

#include "kubelet_lifecycle.h"
#include "qemu_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include <json-c/json.h>

// ============================================================================
// Pod State Creation/Destruction
// ============================================================================

pod_container_state_t* kubelet_pod_state_create(const char* pod_name, const char* namespace,
                                                 const k8s_pod_t* pod) {
    if (!pod_name || !namespace) {
        return NULL;
    }
    
    pod_container_state_t* state = (pod_container_state_t*)malloc(sizeof(pod_container_state_t));
    if (!state) return NULL;
    
    memset(state, 0, sizeof(pod_container_state_t));
    
    state->pod_name = strdup(pod_name);
    state->namespace = strdup(namespace);
    state->state = POD_STATE_INIT;
    state->is_ready = 0;
    state->created_time = time(NULL);
    state->vm_pid = -1;
    
    // Create container health states
    if (pod) {
        state->num_containers = pod->spec.num_containers;
        if (state->num_containers > 0) {
            state->containers = (container_health_t**)malloc(
                sizeof(container_health_t*) * state->num_containers);
            
            for (int i = 0; i < state->num_containers; i++) {
                const char* cname = pod->spec.containers[i].name;
                state->containers[i] = health_container_create(cname);
                
                // Enable probes based on container spec
                // TODO: Parse probe configs from container spec
                
                fprintf(stderr, "[KUBELET] Created container health state: %s\n", cname);
            }
        }
    }
    
    fprintf(stderr, "[KUBELET] Pod state created: %s/%s with %d containers\n",
           namespace, pod_name, state->num_containers);
    
    return state;
}

void kubelet_pod_state_free(pod_container_state_t* state) {
    if (!state) return;
    
    if (state->pod_name) free(state->pod_name);
    if (state->namespace) free(state->namespace);
    
    for (int i = 0; i < state->num_containers; i++) {
        if (state->containers[i]) {
            health_container_free(state->containers[i]);
        }
    }
    if (state->containers) free(state->containers);
    
    if (state->vm_id) free(state->vm_id);
    
    for (int i = 0; i < state->num_mounted_volumes; i++) {
        if (state->mounted_volumes[i]) {
            free(state->mounted_volumes[i]);
        }
    }
    if (state->mounted_volumes) free(state->mounted_volumes);
    
    free(state);
}

// ============================================================================
// Pod Initialization
// ============================================================================

int kubelet_pod_init(pod_container_state_t* state, const char* api_server_url) {
    if (!state) return -1;
    
    fprintf(stderr, "[KUBELET] Initializing pod: %s/%s\n", state->namespace, state->pod_name);
    
    state->state = POD_STATE_WAITING;
    
    // Volume mounting would happen here
    // For MVP: skip volume operations
    
    return 0;
}

// ============================================================================
// Pod Start
// ============================================================================

int kubelet_pod_start(pod_container_state_t* state, const char* vm_image_path) {
    if (!state || !vm_image_path) return -1;
    
    fprintf(stderr, "[KUBELET] Starting pod: %s/%s\n", state->namespace, state->pod_name);
    
    // For MVP: start all containers in single VM
    // In production: might use one VM per container or shared pod namespace
    
    // Generate VM ID
    char vm_id[256];
    snprintf(vm_id, sizeof(vm_id), "%s-%s-%ld", 
             state->namespace, state->pod_name, time(NULL));
    
    state->vm_id = strdup(vm_id);
    
    // Create VM
    char* created_vm_id = qemu_create_vm(state->pod_name, state->namespace,
                                          vm_image_path, state->memory_mb ? state->memory_mb : 256,
                                          state->cpu_millicores ? state->cpu_millicores / 1000 : 2);
    
    if (!created_vm_id) {
        fprintf(stderr, "[KUBELET] Failed to create VM for pod %s/%s\n",
               state->namespace, state->pod_name);
        state->state = POD_STATE_FAILED;
        return -1;
    }
    
    // Start VM
    if (qemu_start_vm(created_vm_id) != 0) {
        fprintf(stderr, "[KUBELET] Failed to start VM for pod %s/%s\n",
               state->namespace, state->pod_name);
        state->state = POD_STATE_FAILED;
        return -1;
    }
    
    // Get VM info
    qemu_vm_t* vm = qemu_get_vm(created_vm_id);
    if (vm) {
        state->vm_pid = vm->qemu_pid;
    }
    
    state->state = POD_STATE_RUNNING;
    state->started_time = time(NULL);
    
    // Update container states
    for (int i = 0; i < state->num_containers; i++) {
        state->containers[i]->state = CONTAINER_STATE_RUNNING;
    }
    
    fprintf(stderr, "[KUBELET] Pod started: %s/%s (VM: %s, PID: %d)\n",
           state->namespace, state->pod_name, state->vm_id, state->vm_pid);
    
    return 0;
}

// ============================================================================
// Health Checking
// ============================================================================

int kubelet_pod_check_health(pod_container_state_t* state, metrics_collector_t* metrics) {
    if (!state) return -1;
    
    time_t now = time(NULL);
    int all_ready = 1;
    
    // Check each container
    for (int i = 0; i < state->num_containers; i++) {
        container_health_t* health = state->containers[i];
        
        if (!health) continue;
        
        // Check probes if enabled
        if (health->startup_probe.enabled && health->startup_probe.is_successful == 0) {
            all_ready = 0;  // Still waiting for startup
        }
        
        if (health->readiness_probe.enabled && !health->is_ready) {
            all_ready = 0;  // Not ready
        }
        
        // Update container health status
        health_container_update(health, now);
        
        if (metrics) {
            // Report probe metrics
            if (health->readiness_probe.last_result == PROBE_SUCCESS) {
                metrics_record_probe_execution(metrics, 1);
            } else if (health->readiness_probe.last_result == PROBE_FAILURE) {
                metrics_record_probe_execution(metrics, 0);
            }
        }
    }
    
    state->is_ready = all_ready;
    state->last_health_check = now;
    
    return 0;
}

// ============================================================================
// Status Updates
// ============================================================================

typedef struct {
    char* data;
    size_t size;
} http_response_t;

static size_t curl_write_cb(void* data, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    http_response_t* buf = (http_response_t*)userp;
    
    char* ptr = realloc(buf->data, buf->size + realsize + 1);
    if (!ptr) return 0;
    
    buf->data = ptr;
    memcpy(&(buf->data[buf->size]), data, realsize);
    buf->size += realsize;
    buf->data[buf->size] = 0;
    
    return realsize;
}

int kubelet_pod_update_status(pod_container_state_t* state, const char* api_server_url) {
    if (!state || !api_server_url) return -1;
    
    // Determine phase based on container states
    const char* phase = "Unknown";
    switch (state->state) {
        case POD_STATE_WAITING: phase = "Pending"; break;
        case POD_STATE_RUNNING: phase = "Running"; break;
        case POD_STATE_SUCCEEDED: phase = "Succeeded"; break;
        case POD_STATE_FAILED: phase = "Failed"; break;
        case POD_STATE_TERMINATING: phase = "Terminating"; break;
        default: phase = "Unknown"; break;
    }
    
    // Build status patch
    json_object* patch = json_object_new_object();
    json_object* status = json_object_new_object();
    
    json_object_object_add(status, "phase", json_object_new_string(phase));
    
    // Add condition: Ready
    json_object* conditions = json_object_new_array();
    json_object* condition = json_object_new_object();
    json_object_object_add(condition, "type", json_object_new_string("Ready"));
    json_object_object_add(condition, "status", json_object_new_string(
        state->is_ready ? "True" : "False"));
    json_object_object_add(condition, "lastProbeTime", json_object_new_int64(state->last_health_check));
    json_object_array_add(conditions, condition);
    json_object_object_add(status, "conditions", conditions);
    
    json_object_object_add(patch, "status", status);
    
    const char* json_str = json_object_to_json_string(patch);
    
    // Send PATCH to API server
    char url[512];
    snprintf(url, sizeof(url), "%s/api/v1/namespaces/%s/pods/%s/status",
            api_server_url, state->namespace, state->pod_name);
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        json_object_put(patch);
        return -1;
    }
    
    http_response_t response = {0};
    response.data = (char*)malloc(4096);
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "[KUBELET] Status update failed: %s\n", curl_easy_strerror(res));
    } else {
        fprintf(stderr, "[KUBELET] Status updated: %s/%s → %s\n",
               state->namespace, state->pod_name, phase);
    }
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(response.data);
    json_object_put(patch);
    
    return (res == CURLE_OK) ? 0 : -1;
}

// ============================================================================
// Pod Termination
// ============================================================================

int kubelet_pod_terminate(pod_container_state_t* state, const char* api_server_url) {
    if (!state) return -1;
    
    fprintf(stderr, "[KUBELET] Terminating pod: %s/%s\n", state->namespace, state->pod_name);
    
    state->state = POD_STATE_TERMINATING;
    
    // Stop VM
    if (state->vm_id) {
        qemu_stop_vm(state->vm_id, 30);
    }
    
    // Unmount volumes
    for (int i = 0; i < state->num_mounted_volumes; i++) {
        // Volume unmounting would happen here
    }
    
    state->state = POD_STATE_SUCCEEDED;
    
    // Update final status
    if (api_server_url) {
        kubelet_pod_update_status(state, api_server_url);
    }
    
    fprintf(stderr, "[KUBELET] Pod terminated: %s/%s\n", state->namespace, state->pod_name);
    
    return 0;
}

// ============================================================================
// Restart Management
// ============================================================================

int kubelet_pod_check_restart_needed(pod_container_state_t* state, metrics_collector_t* metrics) {
    if (!state) return 0;
    
    for (int i = 0; i < state->num_containers; i++) {
        if (state->containers[i] && health_container_should_restart(state->containers[i])) {
            fprintf(stderr, "[KUBELET] Container %s needs restart (liveness failed)\n",
                   state->containers[i]->container_name);
            return 1;
        }
    }
    
    return 0;
}

// ============================================================================
// Container Operations
// ============================================================================

int kubelet_container_start(pod_container_state_t* state, int container_idx,
                            const char* vm_image_path) {
    if (!state || container_idx < 0 || container_idx >= state->num_containers) {
        return -1;
    }
    
    // Containers are started as part of pod start (single VM)
    // Individual container start is a no-op for MVP
    
    return 0;
}

int kubelet_container_restart(pod_container_state_t* state, int container_idx,
                              const char* vm_image_path, const char* api_server_url) {
    if (!state || container_idx < 0 || container_idx >= state->num_containers) {
        return -1;
    }
    
    container_health_t* health = state->containers[container_idx];
    
    fprintf(stderr, "[KUBELET] Restarting container: %s (restart count: %d)\n",
           health->container_name, health->restart_count);
    
    health_container_handle_restart(health);
    
    // Would restart container/VM here
    // For MVP: increment counter and reset state
    
    return 0;
}

int kubelet_container_stop(pod_container_state_t* state, int container_idx) {
    if (!state || container_idx < 0 || container_idx >= state->num_containers) {
        return -1;
    }
    
    if (state->containers[container_idx]) {
        state->containers[container_idx]->state = CONTAINER_STATE_TERMINATED;
    }
    
    return 0;
}

int kubelet_container_check_health(pod_container_state_t* state, int container_idx,
                                   metrics_collector_t* metrics) {
    if (!state || container_idx < 0 || container_idx >= state->num_containers) {
        return -1;
    }
    
    container_health_t* health = state->containers[container_idx];
    if (!health) return -1;
    
    time_t now = time(NULL);
    
    // Execute probes and update health
    health_container_update(health, now);
    
    if (metrics) {
        if (health->startup_probe.last_result == PROBE_SUCCESS) {
            metrics_record_probe_execution(metrics, 1);
        } else if (health->startup_probe.last_result == PROBE_FAILURE) {
            metrics_record_probe_execution(metrics, 0);
        }
    }
    
    return 0;
}

// ============================================================================
// Diagnostics
// ============================================================================

char* kubelet_pod_state_summary(pod_container_state_t* state, char* buf, int buf_len) {
    if (!state || !buf) return NULL;
    
    const char* state_str = "Unknown";
    switch (state->state) {
        case POD_STATE_INIT: state_str = "Init"; break;
        case POD_STATE_WAITING: state_str = "Waiting"; break;
        case POD_STATE_RUNNING: state_str = "Running"; break;
        case POD_STATE_SUCCEEDED: state_str = "Succeeded"; break;
        case POD_STATE_FAILED: state_str = "Failed"; break;
        case POD_STATE_TERMINATING: state_str = "Terminating"; break;
    }
    
    snprintf(buf, buf_len,
            "Pod: %s/%s | State: %s | Ready: %d | Containers: %d",
            state->namespace, state->pod_name, state_str,
            state->is_ready, state->num_containers);
    
    return buf;
}

char* kubelet_container_state_summary(pod_container_state_t* state, int container_idx,
                                      char* buf, int buf_len) {
    if (!state || container_idx < 0 || container_idx >= state->num_containers || !buf) {
        return NULL;
    }
    
    container_health_t* health = state->containers[container_idx];
    return health_container_summary(health, buf, buf_len);
}

int kubelet_pod_is_ready(pod_container_state_t* state) {
    if (!state) return 0;
    return state->is_ready;
}
