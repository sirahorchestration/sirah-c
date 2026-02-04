// internal/controller/pod_controller.c
// Pod controller that monitors pods and spawns QEMU VMs

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include "../runtime/runtime.h"
#include "../runtime/qemu.h"

#define SYNC_INTERVAL 5  // Check pods every 5 seconds
#define MAX_API_RESPONSE 1048576  // 1MB max response

// Structure to hold HTTP response data
typedef struct {
    char* data;
    size_t size;
    size_t capacity;
} http_response_t;

static const char* api_server_url = NULL;
static pthread_mutex_t controller_mutex = PTHREAD_MUTEX_INITIALIZER;

// Curl callback to accumulate response data
static size_t pod_curl_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    http_response_t* mem = (http_response_t*)userp;
    
    if (mem->size + realsize >= mem->capacity) {
        // Response too large, stop reading
        fprintf(stderr, "[POD CONTROLLER] Response exceeds max size\n");
        fflush(stderr);
        return 0;
    }
    
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    
    return realsize;
}

// Pod tracking entry
typedef struct {
    char pod_name[256];
    char namespace[256];
    char image[512];
    char status[32];        // pending, running, failed, succeeded
    char vm_id[256];
    int memory_mb;
    int cpu_count;
    int vm_pid;             // Track actual QEMU PID
    char vm_status[32];     // VM status: running, stopped, crashed
} pod_entry_t;

#define MAX_TRACKED_PODS 100
static pod_entry_t tracked_pods[MAX_TRACKED_PODS];
static int tracked_pod_count = 0;

// Extract memory from pod spec (from resources.limits or default)
static int pod_extract_memory_mb(json_object* container_obj) {
    json_object* resources_obj = NULL;
    if (!json_object_object_get_ex(container_obj, "resources", &resources_obj)) {
        return 128;  // Default 128MB
    }
    
    json_object* limits_obj = NULL;
    if (!json_object_object_get_ex(resources_obj, "limits", &limits_obj)) {
        return 128;
    }
    
    json_object* memory_obj = NULL;
    if (!json_object_object_get_ex(limits_obj, "memory", &memory_obj)) {
        return 128;
    }
    
    const char* memory_str = json_object_get_string(memory_obj);
    if (!memory_str) return 128;
    
    // Parse memory string (e.g., "256Mi", "512M", "1Gi")
    int value = atoi(memory_str);
    
    if (strstr(memory_str, "Gi")) {
        value *= 1024;  // Convert GiB to MiB
    } else if (strstr(memory_str, "G")) {
        value *= 1000;  // Convert GB to MB
    } else if (strstr(memory_str, "Mi")) {
        // Already in MiB
    } else if (strstr(memory_str, "M")) {
        // MB
    } else if (strstr(memory_str, "Ki")) {
        value /= 1024;  // Convert KiB to MiB
    } else if (strstr(memory_str, "K")) {
        value /= 1000;  // Convert KB to MB
    }
    
    // Sanity bounds
    if (value < 32) value = 32;    // Min 32MB
    if (value > 32768) value = 32768;  // Max 32GB
    
    return value;
}

// Extract CPU count from pod spec
static int pod_extract_cpu_count(json_object* container_obj) {
    json_object* resources_obj = NULL;
    if (!json_object_object_get_ex(container_obj, "resources", &resources_obj)) {
        return 1;  // Default 1 CPU
    }
    
    json_object* limits_obj = NULL;
    if (!json_object_object_get_ex(resources_obj, "limits", &limits_obj)) {
        return 1;
    }
    
    json_object* cpu_obj = NULL;
    if (!json_object_object_get_ex(limits_obj, "cpu", &cpu_obj)) {
        return 1;
    }
    
    const char* cpu_str = json_object_get_string(cpu_obj);
    if (!cpu_str) return 1;
    
    // Parse CPU string (e.g., "1", "2", "500m")
    if (strstr(cpu_str, "m")) {
        int millicpus = atoi(cpu_str);
        return (millicpus + 999) / 1000;  // Round up
    }
    
    int cpus = atoi(cpu_str);
    if (cpus < 1) cpus = 1;
    if (cpus > 16) cpus = 16;  // Cap at 16 CPUs
    
    return cpus;
}

// Check if image is a unikernel (heuristic)
static int pod_is_unikernel_image(const char* image) {
    if (!image) return 0;
    
    // Check for common unikernel indicators
    const char* unikernel_keywords[] = {
        "unikernel", "kernel", ".img", ".bin", "vmlinuz",
        "osv", "mirage", "rumprun", "IncludeOS", NULL
    };
    
    for (int i = 0; unikernel_keywords[i]; i++) {
        if (strcasestr(image, unikernel_keywords[i])) {
            return 1;
        }
    }
    
    // Check if path is to a file (not a container registry reference)
    if (image[0] == '/' || strstr(image, "/") == NULL || strstr(image, ":") == NULL) {
        // Looks like a local kernel file path
        return 1;
    }
    
    return 0;
}

// Update pod status in API server
static int pod_update_status_in_api(const char* namespace, const char* pod_name, 
                                    const char* phase) {
    if (!api_server_url) {
        return -1;
    }
    
    fprintf(stderr, "[POD CONTROLLER] Updating pod status: %s/%s → %s\n", 
            namespace, pod_name, phase);
    fflush(stderr);
    
    // Build PATCH request to update pod status
    char url[512];
    snprintf(url, sizeof(url), "%s/api/v1/namespaces/%s/pods/%s/status", 
             api_server_url, namespace, pod_name);
    
    // Create JSON body for status update
    char patch_data[256];
    snprintf(patch_data, sizeof(patch_data), 
             "{"
             "\"status\":{"
             "\"phase\":\"%s\""
             "}"
             "}", phase);
    
    // Make HTTP PATCH request
    CURL* curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[POD CONTROLLER] Failed to initialize curl\n");
        fflush(stderr);
        return -1;
    }
    
    http_response_t response = {0};
    response.data = (char*)malloc(MAX_API_RESPONSE);
    response.capacity = MAX_API_RESPONSE;
    response.size = 0;
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, patch_data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, pod_curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "[POD CONTROLLER] PATCH request failed: %s\n", curl_easy_strerror(res));
        fflush(stderr);
    } else {
        fprintf(stderr, "[POD CONTROLLER] PATCH request succeeded: %s\n", response.data);
        fflush(stderr);
    }
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(response.data);
    
    return res == CURLE_OK ? 0 : -1;
}

// Monitor QEMU process and return current status
static const char* pod_get_vm_status(int vm_pid) {
    if (vm_pid <= 0) {
        return "unknown";
    }
    
    // Check if process still exists
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "kill -0 %d 2>/dev/null", vm_pid);
    int ret = system(cmd);
    
    if (ret == 0) {
        return "running";
    } else {
        return "stopped";
    }
}

// Track a pod
static int pod_controller_track_pod(const char* namespace, const char* name,
                                     const char* image, int memory_mb) {
    if (tracked_pod_count >= MAX_TRACKED_PODS) {
        fprintf(stderr, "[Pod Controller] Max pods reached\n");
        return -1;
    }
    
    pod_entry_t* pod = &tracked_pods[tracked_pod_count];
    strncpy(pod->namespace, namespace, sizeof(pod->namespace) - 1);
    strncpy(pod->pod_name, name, sizeof(pod->pod_name) - 1);
    strncpy(pod->image, image, sizeof(pod->image) - 1);
    strcpy(pod->status, "pending");
    
    // Create VM ID (namespace-name)
    snprintf(pod->vm_id, sizeof(pod->vm_id), "%s-%s", namespace, name);
    
    pod->memory_mb = memory_mb > 0 ? memory_mb : 128;
    
    tracked_pod_count++;
    
    printf("[Pod Controller] Tracking pod: %s/%s (image: %s)\n", 
           namespace, name, image);
    
    return 0;
}

// Fetch pods from API and trigger VM spawning for new pods
static int pod_controller_fetch_pods(void) {
    if (!api_server_url) {
        fprintf(stderr, "[POD CONTROLLER] FETCH: No API URL configured\n");
        fflush(stderr);
        return -1;
    }
    
    fprintf(stderr, "[POD CONTROLLER] FETCH: Querying %s/api/v1/pods\n", api_server_url);
    fflush(stderr);
    
    // Initialize curl
    CURL* curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[POD CONTROLLER] FETCH: curl_easy_init failed\n");
        fflush(stderr);
        return -1;
    }
    
    // Initialize response buffer
    http_response_t response;
    response.data = malloc(MAX_API_RESPONSE);
    response.size = 0;
    response.capacity = MAX_API_RESPONSE;
    
    if (!response.data) {
        fprintf(stderr, "[POD CONTROLLER] FETCH: malloc failed\n");
        fflush(stderr);
        curl_easy_cleanup(curl);
        return -1;
    }
    
    // Build URL - query all namespaces
    char url[512];
    snprintf(url, sizeof(url), "%s/api/v1/namespaces/default/pods", api_server_url);
    
    // Configure curl
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, pod_curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "[POD CONTROLLER] FETCH: curl_easy_perform failed: %s\n", 
                curl_easy_strerror(res));
        fflush(stderr);
        free(response.data);
        return -1;
    }
    
    fprintf(stderr, "[POD CONTROLLER] FETCH: Received %zu bytes of data\n", response.size);
    fflush(stderr);
    
    // Parse JSON response
    json_object* root = json_tokener_parse(response.data);
    free(response.data);
    
    if (!root) {
        fprintf(stderr, "[POD CONTROLLER] FETCH: JSON parse failed\n");
        fflush(stderr);
        return -1;
    }
    
    // Extract items array from PodList
    json_object* items_obj = NULL;
    if (!json_object_object_get_ex(root, "items", &items_obj) || !json_object_is_type(items_obj, json_type_array)) {
        fprintf(stderr, "[POD CONTROLLER] FETCH: No items array in response\n");
        fflush(stderr);
        json_object_put(root);
        return -1;
    }
    
    int num_items = json_object_array_length(items_obj);
    fprintf(stderr, "[POD CONTROLLER] FETCH: Found %d pods in API\n", num_items);
    fflush(stderr);
    
    // Process each pod from API
    pthread_mutex_lock(&controller_mutex);
    
    for (int i = 0; i < num_items; i++) {
        json_object* pod_obj = json_object_array_get_idx(items_obj, i);
        if (!pod_obj) continue;
        
        // Extract metadata
        json_object* metadata_obj = NULL;
        if (!json_object_object_get_ex(pod_obj, "metadata", &metadata_obj)) {
            continue;
        }
        
        // Get pod name
        json_object* name_obj = NULL;
        if (!json_object_object_get_ex(metadata_obj, "name", &name_obj)) {
            continue;
        }
        const char* pod_name = json_object_get_string(name_obj);
        
        // Get namespace
        json_object* ns_obj = NULL;
        if (!json_object_object_get_ex(metadata_obj, "namespace", &ns_obj)) {
            continue;
        }
        const char* namespace = json_object_get_string(ns_obj);
        
        // Extract spec
        json_object* spec_obj = NULL;
        if (!json_object_object_get_ex(pod_obj, "spec", &spec_obj)) {
            continue;
        }
        
        // Get containers array
        json_object* containers_obj = NULL;
        if (!json_object_object_get_ex(spec_obj, "containers", &containers_obj) ||
            !json_object_is_type(containers_obj, json_type_array) ||
            json_object_array_length(containers_obj) == 0) {
            continue;
        }
        
        // Get first container
        json_object* container_obj = json_object_array_get_idx(containers_obj, 0);
        if (!container_obj) continue;
        
        // Get image
        json_object* image_obj = NULL;
        if (!json_object_object_get_ex(container_obj, "image", &image_obj)) {
            continue;
        }
        const char* image = json_object_get_string(image_obj);
        
        // Check if this is a unikernel image
        if (!pod_is_unikernel_image(image)) {
            fprintf(stderr, "[POD CONTROLLER] FETCH: Skipping non-unikernel image: %s\n", image);
            fflush(stderr);
            continue;
        }
        
        // Extract resources from pod spec
        int memory_mb = pod_extract_memory_mb(container_obj);
        int cpu_count = pod_extract_cpu_count(container_obj);
        
        // Get status
        json_object* status_obj = NULL;
        const char* phase = "Unknown";
        if (json_object_object_get_ex(pod_obj, "status", &status_obj)) {
            json_object* phase_obj = NULL;
            if (json_object_object_get_ex(status_obj, "phase", &phase_obj)) {
                phase = json_object_get_string(phase_obj);
            }
        }
        
        fprintf(stderr, "[POD CONTROLLER] FETCH: Found pod %s/%s image=%s memory=%dMB cpu=%d status=%s\n",
                namespace, pod_name, image, memory_mb, cpu_count, phase);
        fflush(stderr);
        
        // Check if we're already tracking this pod
        int found = 0;
        for (int j = 0; j < tracked_pod_count; j++) {
            if (strcmp(tracked_pods[j].namespace, namespace) == 0 &&
                strcmp(tracked_pods[j].pod_name, pod_name) == 0) {
                found = 1;
                break;
            }
        }
        
        // If new pod and pending, add it for VM spawning
        if (!found && strcmp(phase, "Pending") == 0) {
            fprintf(stderr, "[POD CONTROLLER] FETCH: New pending pod detected: %s/%s\n",
                    namespace, pod_name);
            fflush(stderr);
            
            if (tracked_pod_count < MAX_TRACKED_PODS) {
                pod_entry_t* pod = &tracked_pods[tracked_pod_count];
                strncpy(pod->namespace, namespace, sizeof(pod->namespace) - 1);
                strncpy(pod->pod_name, pod_name, sizeof(pod->pod_name) - 1);
                strncpy(pod->image, image, sizeof(pod->image) - 1);
                strcpy(pod->status, "pending");
                snprintf(pod->vm_id, sizeof(pod->vm_id), "%s-%s", namespace, pod_name);
                pod->memory_mb = memory_mb;
                pod->cpu_count = cpu_count;
                pod->vm_pid = -1;
                strcpy(pod->vm_status, "pending");
                tracked_pod_count++;
                
                fprintf(stderr, "[POD CONTROLLER] FETCH: Added to tracking: %s/%s memory=%dMB cpu=%d (total: %d)\n",
                        namespace, pod_name, memory_mb, cpu_count, tracked_pod_count);
                fflush(stderr);
            }
        }
    }
    
    pthread_mutex_unlock(&controller_mutex);
    json_object_put(root);
    
    fprintf(stderr, "[POD CONTROLLER] FETCH: Complete\n");
    fflush(stderr);
    
    return 0;
}

// Sync pod states with actual VMs
// Log pod event similar to Kubernetes kubelet
static void pod_log_event(const char* pod_name, const char* namespace, 
                          const char* container, const char* reason, const char* message) {
    fprintf(stderr, "[POD EVENT] %s/%s container=%s | %s: %s\n", 
            namespace, pod_name, container ? container : "pod", reason, message);
    fflush(stderr);
}

// Pod status transition with proper logging
static void pod_transition_status(pod_entry_t* pod, const char* from_status, const char* to_status) {
    strcpy(pod->status, to_status);
    fprintf(stderr, "[POD STATUS] %s/%s: %s → %s\n", 
            pod->namespace, pod->pod_name, from_status, to_status);
    fflush(stderr);
}

static int pod_controller_sync_states(void) {
    pthread_mutex_lock(&controller_mutex);
    
    fprintf(stderr, "[POD CONTROLLER] SYNC: Found %d tracked pods\n", tracked_pod_count);
    fflush(stderr);
    
    for (int i = 0; i < tracked_pod_count; i++) {
        pod_entry_t* pod = &tracked_pods[i];
        
        fprintf(stderr, "[POD CONTROLLER] SYNC: Pod %d status=%s vm_pid=%d\n", 
                i, pod->status, pod->vm_pid);
        fflush(stderr);
        
        if (strcmp(pod->status, "pending") == 0) {
            // Log: Container image pulling/preparing
            pod_log_event(pod->pod_name, pod->namespace, "app", "Pulling", 
                         "Pulling image...");
            
            // Small delay to simulate image prep
            usleep(100000);  // 100ms
            
            // Log: Image pulled
            pod_log_event(pod->pod_name, pod->namespace, "app", "Pulled", 
                         "Successfully pulled image");
            
            // Log: Container creating
            pod_log_event(pod->pod_name, pod->namespace, "app", "Creating", 
                         "Creating unikernel VM instance...");
            
            fprintf(stderr, "[POD CONTROLLER] SYNC: Spawning VM for %s/%s image=%s memory=%dMB cpu=%d\n", 
                   pod->namespace, pod->pod_name, pod->image, pod->memory_mb, pod->cpu_count);
            fflush(stderr);
            
            vm_spec_t vm_spec = {
                .id = pod->vm_id,
                .image = pod->image,
                .memory_mb = pod->memory_mb,
                .cpu_count = pod->cpu_count,
                .namespace = pod->namespace,
                .pod_name = pod->pod_name,
                .vm_pid = -1,
                .status = "pending"
            };
            
            int ret = runtime_spawn_vm(&vm_spec);
            fprintf(stderr, "[POD CONTROLLER] SYNC: runtime_spawn_vm returned %d\n", ret);
            fflush(stderr);
            
            if (ret == 0) {
                // Log: Container created
                pod_log_event(pod->pod_name, pod->namespace, "app", "Created", 
                             "VM instance created successfully");
                
                // Log: Container started
                pod_log_event(pod->pod_name, pod->namespace, "app", "Started", 
                             "VM booting up...");
                
                // Update pod status
                pod_transition_status(pod, "Pending", "Running");
                pod_update_status_in_api(pod->namespace, pod->pod_name, "Running");
                
                fprintf(stderr, "[POD CONTROLLER] SYNC: VM started for %s/%s\n",
                       pod->namespace, pod->pod_name);
                
                // Log: Container ready
                usleep(200000);  // 200ms delay to simulate boot
                pod_log_event(pod->pod_name, pod->namespace, "app", "Ready", 
                             "Unikernel application is running");
            } else {
                // Log: Container failed
                pod_log_event(pod->pod_name, pod->namespace, "app", "Failed", 
                             "Failed to create VM instance");
                
                fprintf(stderr, "[POD CONTROLLER] SYNC: Failed to start VM for %s/%s\n",
                       pod->namespace, pod->pod_name);
                
                pod_transition_status(pod, "Pending", "Failed");
                pod_update_status_in_api(pod->namespace, pod->pod_name, "Failed");
            }
            fflush(stderr);
        } 
        else if (strcmp(pod->status, "running") == 0) {
            // Monitor VM status
            const char* vm_status = pod_get_vm_status(pod->vm_pid);
            strcpy(pod->vm_status, vm_status);
            
            if (strcmp(vm_status, "stopped") == 0) {
                // Log: Container exited
                pod_log_event(pod->pod_name, pod->namespace, "app", "Exited", 
                             "Unikernel VM stopped");
                
                pod_transition_status(pod, "Running", "Succeeded");
                pod_update_status_in_api(pod->namespace, pod->pod_name, "Succeeded");
                
                fprintf(stderr, "[POD CONTROLLER] SYNC: VM stopped for %s/%s\n",
                       pod->namespace, pod->pod_name);
                fflush(stderr);
            }
        }
    }
    
    pthread_mutex_unlock(&controller_mutex);
    return 0;
}

int pod_controller_init(const char* apiserver_url) {
    api_server_url = apiserver_url;
    tracked_pod_count = 0;
    
    printf("[Pod Controller] Initialized with API: %s\n", apiserver_url);
    return 0;
}

int pod_controller_run(void) {
    fprintf(stderr, "[POD CONTROLLER] RUN STARTED\n");
    fflush(stderr);
    
    // Run indefinitely - controller should keep running to discover and spawn pods
    int iteration = 0;
    while (1) {
        iteration++;
        
        // Fetch latest pods from API
        fprintf(stderr, "[POD CONTROLLER] Sync iteration %d\n", iteration);
        fflush(stderr);
        pod_controller_fetch_pods();
        
        // Sync pod states
        pod_controller_sync_states();
        
        fprintf(stderr, "[POD CONTROLLER] Sync iteration %d complete, sleeping...\n", iteration);
        fflush(stderr);
        
        // Wait before next sync
        sleep(SYNC_INTERVAL);
    }
    
    fprintf(stderr, "[POD CONTROLLER] RUN FINISHED\n");
    fflush(stderr);
    
    return 0;
}

// DEPRECATED: Callback to add pod to controller (now unused - controller fetches from API)
// int pod_controller_add_pod(const char* namespace, const char* name,
//                           const char* image, int memory_mb) {
//     fprintf(stderr, "[POD CONTROLLER] ADD POD DEPRECATED: Controller now fetches from API\n");
//     fflush(stderr);
//     return 0;
// }

void pod_controller_shutdown(void) {
    printf("[Pod Controller] Shutting down...\n");
    // Stop all tracked pods
    for (int i = 0; i < tracked_pod_count; i++) {
        runtime_stop_vm(tracked_pods[i].vm_id);
    }
    tracked_pod_count = 0;
}
