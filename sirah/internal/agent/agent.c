// internal/agent/agent.c
// Sirah Worker Agent Implementation
// Node-local agent managing pod lifecycle and communicating with control plane

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include "agent.h"

// ============ HTTP Response Buffering ============

typedef struct {
    char* buffer;
    size_t size;
    size_t allocated;
} http_response_t;

static size_t http_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    http_response_t* mem = (http_response_t*)userp;

    char* ptr = realloc(mem->buffer, mem->size + realsize + 1);
    if (!ptr) {
        printf("ERROR: Not enough memory for HTTP response\n");
        return 0;
    }
    mem->buffer = ptr;
    memcpy(&(mem->buffer[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->buffer[mem->size] = 0;

    return realsize;
}

// ============ Helper Functions ============

void agent_uint32_to_ip_string(unsigned int ip, char* out_str) {
    if (!out_str) return;
    sprintf(out_str, "%u.%u.%u.%u",
            (ip >> 24) & 0xFF,
            (ip >> 16) & 0xFF,
            (ip >> 8) & 0xFF,
            ip & 0xFF);
}

unsigned int agent_ip_string_to_uint32(const char* ip_str) {
    if (!ip_str) return 0;
    unsigned int a, b, c, d;
    if (sscanf(ip_str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return 0;
    }
    return (a << 24) | (b << 16) | (c << 8) | d;
}

// HTTP request helper
static json_object* agent_api_request(agent_t* agent, const char* method, const char* path,
                                     json_object* body) {
    if (!agent || !path) return NULL;

    char url[1024];
    snprintf(url, sizeof(url), "%s%s", agent->api_server_url, path);

    http_response_t response = {0};
    response.buffer = malloc(1);
    if (!response.buffer) return NULL;
    response.size = 0;

    curl_easy_reset(agent->curl_handle);
    curl_easy_setopt(agent->curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(agent->curl_handle, CURLOPT_WRITEFUNCTION, http_write_callback);
    curl_easy_setopt(agent->curl_handle, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(agent->curl_handle, CURLOPT_TIMEOUT, 5L);

    if (strcmp(method, "GET") == 0) {
        curl_easy_setopt(agent->curl_handle, CURLOPT_HTTPGET, 1L);
    } else if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(agent->curl_handle, CURLOPT_POST, 1L);
        if (body) {
            const char* json_str = json_object_to_json_string(body);
            curl_easy_setopt(agent->curl_handle, CURLOPT_POSTFIELDS, json_str);
        }
    } else if (strcmp(method, "PATCH") == 0) {
        curl_easy_setopt(agent->curl_handle, CURLOPT_CUSTOMREQUEST, "PATCH");
        if (body) {
            const char* json_str = json_object_to_json_string(body);
            curl_easy_setopt(agent->curl_handle, CURLOPT_POSTFIELDS, json_str);
        }
    }

    CURLcode res = curl_easy_perform(agent->curl_handle);
    if (res != CURLE_OK) {
        printf("ERROR: curl_easy_perform failed: %s\n", curl_easy_strerror(res));
        free(response.buffer);
        return NULL;
    }

    json_object* json = json_tokener_parse(response.buffer);
    free(response.buffer);
    return json;
}

// ============ Lifecycle Functions ============

agent_t* agent_new(const char* api_server_url,
                   const char* node_name,
                   const char* node_ip,
                   const char* pod_cidr) {
    if (!api_server_url || !node_name || !node_ip) return NULL;

    agent_t* agent = calloc(1, sizeof(agent_t));
    if (!agent) return NULL;

    agent->api_server_url = strdup(api_server_url);
    agent->curl_handle = curl_easy_init();

    // Initialize node info
    strncpy(agent->node_info.node_name, node_name, sizeof(agent->node_info.node_name) - 1);
    strncpy(agent->node_info.node_ip, node_ip, sizeof(agent->node_info.node_ip) - 1);
    if (pod_cidr) {
        strncpy(agent->node_info.pod_cidr, pod_cidr, sizeof(agent->node_info.pod_cidr) - 1);
        agent->node_info.pod_cidr_start = agent_ip_string_to_uint32("10.0.1.1");
        agent->node_info.pod_cidr_end = agent_ip_string_to_uint32("10.0.1.254");
    }

    agent->node_info.max_pods = 110;
    agent->node_info.memory_bytes = 4L * 1024 * 1024 * 1024;  // 4GB
    agent->node_info.cpu_millicores = 2000;                   // 2 CPUs
    agent->node_info.ready = 0;
    strcpy(agent->node_info.status, "NotReady");

    // Initialize pod storage
    agent->max_pods_capacity = 200;
    agent->pods = calloc(agent->max_pods_capacity, sizeof(managed_pod_t));

    // Initialize VM storage
    agent->max_vms_capacity = 200;
    agent->vms = calloc(agent->max_vms_capacity, sizeof(vm_instance_t));

    // Set intervals
    agent->heartbeat_interval = 10;
    agent->pod_sync_interval = 5;
    agent->max_pod_restarts = 5;
    agent->pod_restart_backoff = 5;

    agent->running = 0;
    agent->ready = 0;

    printf("Agent created for node %s (%s, CIDR: %s)\n", node_name, node_ip,
           pod_cidr ? pod_cidr : "none");
    return agent;
}

void agent_free(agent_t* agent) {
    if (!agent) return;

    if (agent->api_server_url) free(agent->api_server_url);
    if (agent->curl_handle) curl_easy_cleanup(agent->curl_handle);
    if (agent->pods) free(agent->pods);
    if (agent->vms) free(agent->vms);

    free(agent);
}

int agent_init(agent_t* agent) {
    if (!agent) return -1;

    printf("Agent initializing...\n");
    printf("  Node: %s\n", agent->node_info.node_name);
    printf("  IP: %s\n", agent->node_info.node_ip);
    printf("  Pod CIDR: %s\n", agent->node_info.pod_cidr);

    // Register with control plane
    if (agent_register_node(agent) != 0) {
        printf("WARNING: Node registration failed\n");
        // Don't fail init if registration fails
    }

    agent->running = 1;
    agent->started_at = time(NULL);
    
    return 0;
}

void agent_shutdown(agent_t* agent) {
    if (!agent) return;
    agent->running = 0;
    printf("Agent shutting down...\n");
}

// ============ Control Plane Communication ============

int agent_register_node(agent_t* agent) {
    if (!agent) return -1;

    // Create node object
    json_object* node = json_object_new_object();
    json_object* metadata = json_object_new_object();
    json_object* status = json_object_new_object();
    json_object* spec = json_object_new_object();

    json_object_object_add(metadata, "name", json_object_new_string(agent->node_info.node_name));
    json_object_object_add(spec, "podCIDR", json_object_new_string(agent->node_info.pod_cidr));
    
    json_object_object_add(status, "nodeIP", json_object_new_string(agent->node_info.node_ip));
    json_object_object_add(status, "phase", json_object_new_string("Ready"));
    json_object_object_add(status, "capacity",
                          json_object_new_int(agent->node_info.max_pods));

    json_object_object_add(node, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(node, "kind", json_object_new_string("Node"));
    json_object_object_add(node, "metadata", metadata);
    json_object_object_add(node, "spec", spec);
    json_object_object_add(node, "status", status);

    // POST to API
    char path[512];
    snprintf(path, sizeof(path), "/api/v1/nodes");
    
    json_object* response = agent_api_request(agent, "POST", path, node);
    json_object_put(node);

    if (response) {
        printf("Node registered: %s\n", agent->node_info.node_name);
        json_object_put(response);
        agent->node_info.ready = 1;
        strcpy(agent->node_info.status, "Ready");
        return 0;
    }

    return -1;
}

int agent_send_heartbeat(agent_t* agent) {
    if (!agent) return -1;

    // Create heartbeat
    json_object* heartbeat = json_object_new_object();
    json_object_object_add(heartbeat, "nodeName", json_object_new_string(agent->node_info.node_name));
    json_object_object_add(heartbeat, "status", json_object_new_string(agent->node_info.status));
    json_object_object_add(heartbeat, "podCount", json_object_new_int(agent->num_pods));
    json_object_object_add(heartbeat, "timestamp", json_object_new_int((int)time(NULL)));

    // POST to API
    char path[512];
    snprintf(path, sizeof(path), "/api/v1/nodes/%s/heartbeat", agent->node_info.node_name);
    
    json_object* response = agent_api_request(agent, "POST", path, heartbeat);
    json_object_put(heartbeat);

    if (response) {
        printf("[Heartbeat] %s (pods: %d)\n", agent->node_info.node_name, agent->num_pods);
        agent->node_info.last_heartbeat = time(NULL);
        json_object_put(response);
        return 0;
    }

    return -1;
}

int agent_sync_pods(agent_t* agent) {
    if (!agent) return -1;

    // Fetch pods assigned to this node
    char path[512];
    snprintf(path, sizeof(path), "/api/v1/pods?fieldSelector=spec.nodeName=%s",
             agent->node_info.node_name);

    json_object* response = agent_api_request(agent, "GET", path, NULL);
    if (!response) {
        printf("WARNING: Failed to sync pods\n");
        return -1;
    }

    // Parse pod list (simplified)
    json_object* items = NULL;
    if (json_object_object_get_ex(response, "items", &items)) {
        int num_items = json_object_array_length(items);
        printf("[Pod Sync] Found %d pods for node %s\n", num_items, agent->node_info.node_name);
        
        for (int i = 0; i < num_items && i < agent->max_pods_capacity; i++) {
            json_object* pod_obj = json_object_array_get_idx(items, i);
            if (!pod_obj) continue;

            // Extract pod name and namespace
            json_object* metadata = NULL;
            if (!json_object_object_get_ex(pod_obj, "metadata", &metadata)) continue;

            const char* pod_name = json_object_get_string(
                json_object_object_get(metadata, "name"));
            const char* namespace = json_object_get_string(
                json_object_object_get(metadata, "namespace"));

            if (pod_name && namespace) {
                // Check if we already have this pod
                managed_pod_t* pod = agent_get_pod(agent, pod_name, namespace);
                if (!pod && agent->num_pods < agent->max_pods_capacity) {
                    // New pod
                    pod = &agent->pods[agent->num_pods];
                    strncpy(pod->pod_name, pod_name, sizeof(pod->pod_name) - 1);
                    strncpy(pod->namespace, namespace, sizeof(pod->namespace) - 1);
                    strcpy(pod->phase, "Pending");
                    pod->created_at = time(NULL);
                    agent->num_pods++;
                    
                    printf("  Synced pod: %s/%s\n", namespace, pod_name);
                }
            }
        }
    }

    json_object_put(response);
    return 0;
}

int agent_update_pod_status(agent_t* agent,
                           const char* pod_name,
                           const char* namespace,
                           const char* phase,
                           const char* pod_ip) {
    if (!agent || !pod_name || !namespace) return -1;

    // Create status patch
    json_object* patch = json_object_new_object();
    json_object* status = json_object_new_object();

    json_object_object_add(status, "phase", json_object_new_string(phase));
    if (pod_ip) {
        json_object_object_add(status, "podIP", json_object_new_string(pod_ip));
    }

    json_object_object_add(patch, "status", status);

    // PATCH to API
    char path[512];
    snprintf(path, sizeof(path), "/api/v1/namespaces/%s/pods/%s", namespace, pod_name);

    json_object* response = agent_api_request(agent, "PATCH", path, patch);
    json_object_put(patch);

    if (response) {
        printf("[Status] Updated %s/%s -> %s\n", namespace, pod_name, phase);
        json_object_put(response);
        return 0;
    }

    return -1;
}

// ============ Pod Lifecycle Management ============

int agent_create_pod(agent_t* agent, json_object* pod_spec) {
    if (!agent || !pod_spec) return -1;

    printf("[Pod] Creating pod from spec...\n");
    
    // In a real implementation, would:
    // 1. Parse pod spec
    // 2. Allocate pod IP
    // 3. Spawn VM/container via runtime
    // 4. Update pod status

    return 0;
}

int agent_start_pod(agent_t* agent,
                   const char* pod_name,
                   const char* namespace) {
    if (!agent || !pod_name || !namespace) return -1;

    managed_pod_t* pod = agent_get_pod(agent, pod_name, namespace);
    if (!pod) return -1;

    printf("[Pod] Starting %s/%s\n", namespace, pod_name);
    strcpy(pod->phase, "Running");
    pod->last_updated = time(NULL);

    return agent_update_pod_status(agent, pod_name, namespace, "Running", pod->pod_ip);
}

int agent_stop_pod(agent_t* agent,
                  const char* pod_name,
                  const char* namespace) {
    if (!agent || !pod_name || !namespace) return -1;

    managed_pod_t* pod = agent_get_pod(agent, pod_name, namespace);
    if (!pod) return -1;

    printf("[Pod] Stopping %s/%s\n", namespace, pod_name);
    strcpy(pod->phase, "Stopped");
    pod->last_updated = time(NULL);

    return 0;
}

int agent_restart_pod(agent_t* agent,
                     const char* pod_name,
                     const char* namespace) {
    if (!agent || !pod_name || !namespace) return -1;

    managed_pod_t* pod = agent_get_pod(agent, pod_name, namespace);
    if (!pod) return -1;

    if (pod->restart_count >= agent->max_pod_restarts) {
        printf("[Pod] Max restarts reached for %s/%s\n", namespace, pod_name);
        return -1;
    }

    printf("[Pod] Restarting %s/%s (attempt %d)\n", namespace, pod_name,
           pod->restart_count + 1);
    
    pod->restart_count++;
    agent->total_restarts++;

    return agent_start_pod(agent, pod_name, namespace);
}

int agent_check_pod_health(agent_t* agent,
                          const char* pod_name,
                          const char* namespace) {
    if (!agent || !pod_name || !namespace) return -1;

    // In a real implementation, would run liveness/readiness probes
    printf("[Health] Checking %s/%s\n", namespace, pod_name);
    return 1;  // Healthy
}

// ============ Node Management ============

int agent_mark_node_ready(agent_t* agent) {
    if (!agent) return -1;
    agent->node_info.ready = 1;
    strcpy(agent->node_info.status, "Ready");
    return agent_update_node_status(agent);
}

int agent_mark_node_not_ready(agent_t* agent) {
    if (!agent) return -1;
    agent->node_info.ready = 0;
    strcpy(agent->node_info.status, "NotReady");
    return agent_update_node_status(agent);
}

int agent_drain_node(agent_t* agent) {
    if (!agent) return -1;
    printf("[Node] Draining node %s\n", agent->node_info.node_name);
    strcpy(agent->node_info.status, "Draining");
    return agent_update_node_status(agent);
}

int agent_update_node_status(agent_t* agent) {
    if (!agent) return -1;

    // Create node status
    json_object* patch = json_object_new_object();
    json_object* status = json_object_new_object();

    json_object_object_add(status, "phase", json_object_new_string(agent->node_info.status));
    json_object_object_add(status, "nodeIP", json_object_new_string(agent->node_info.node_ip));
    json_object_object_add(patch, "status", status);

    // PATCH to API
    char path[512];
    snprintf(path, sizeof(path), "/api/v1/nodes/%s", agent->node_info.node_name);

    json_object* response = agent_api_request(agent, "PATCH", path, patch);
    json_object_put(patch);

    if (response) {
        json_object_put(response);
        return 0;
    }

    return -1;
}

node_info_t* agent_get_node_info(agent_t* agent) {
    if (!agent) return NULL;
    return &agent->node_info;
}

// ============ Pod Query Functions ============

managed_pod_t* agent_get_pod(agent_t* agent,
                            const char* pod_name,
                            const char* namespace) {
    if (!agent || !pod_name || !namespace) return NULL;

    for (int i = 0; i < agent->num_pods; i++) {
        if (strcmp(agent->pods[i].pod_name, pod_name) == 0 &&
            strcmp(agent->pods[i].namespace, namespace) == 0) {
            return &agent->pods[i];
        }
    }

    return NULL;
}

managed_pod_t** agent_list_pods(agent_t* agent, int* out_count) {
    if (!agent || !out_count) return NULL;
    *out_count = agent->num_pods;
    return agent->pods;
}

int agent_get_pod_count(agent_t* agent) {
    if (!agent) return 0;
    return agent->num_pods;
}

// ============ VM Management (Stubs) ============

int agent_spawn_vm_instance(agent_t* agent,
                           const char* pod_name,
                           const char* namespace,
                           json_object* pod_spec) {
    if (!agent || !pod_name || !namespace) return -1;
    printf("[VM] Spawning VM for %s/%s\n", namespace, pod_name);
    return 0;
}

int agent_stop_vm_instance(agent_t* agent, const char* container_id) {
    if (!agent || !container_id) return -1;
    printf("[VM] Stopping VM %s\n", container_id);
    return 0;
}

int agent_monitor_vm_process(agent_t* agent,
                            const char* container_id,
                            int* out_exit_code) {
    if (!agent || !container_id || !out_exit_code) return -1;
    *out_exit_code = 0;
    return 0;
}

// ============ IP Management ============

int agent_allocate_pod_ip(agent_t* agent, char* out_pod_ip) {
    if (!agent || !out_pod_ip) return -1;

    // Simple round-robin allocation
    static int next_ip = 1;
    unsigned int base = agent->node_info.pod_cidr_start;
    unsigned int ip = base + next_ip;
    
    if (ip > agent->node_info.pod_cidr_end) {
        printf("ERROR: Pod IP pool exhausted\n");
        return -1;
    }

    agent_uint32_to_ip_string(ip, out_pod_ip);
    next_ip++;

    return 0;
}

int agent_release_pod_ip(agent_t* agent, const char* pod_ip) {
    if (!agent || !pod_ip) return -1;
    // Simplified: just mark as available
    return 0;
}

// ============ Main Agent Loop ============

int agent_run(agent_t* agent) {
    if (!agent) return -1;

    agent->running = 1;
    printf("Agent running on node %s\n", agent->node_info.node_name);

    time_t last_heartbeat = time(NULL);
    time_t last_pod_sync = time(NULL);

    while (agent->running) {
        time_t now = time(NULL);

        // Heartbeat (10 seconds)
        if (now - last_heartbeat >= agent->heartbeat_interval) {
            agent_send_heartbeat(agent);
            last_heartbeat = now;
        }

        // Pod sync (5 seconds)
        if (now - last_pod_sync >= agent->pod_sync_interval) {
            agent_sync_pods(agent);
            last_pod_sync = now;
        }

        sleep(1);  // Sleep 1 second between checks
    }

    return 0;
}
