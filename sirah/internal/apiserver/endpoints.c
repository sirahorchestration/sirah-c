// internal/apiserver/endpoints.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <json-c/json.h>
#include "endpoints.h"
#include "../storage/store.h"
#include "../../pkg/types/service.h"

// Forward declarations for persistence
extern int store_save_pod_to_etcd(k8s_pod_t* pod);
extern int store_delete_pod_from_etcd(const char* namespace, const char* name);
extern etcd_client_t* g_etcd_client;

// Global node store
static struct {
    struct {
        char name[256];
        char ip[256];
        char status[64];
    } nodes[100];
    int count;
} node_store = {0};

void init_node_store(void) {
    // Add local node for MVP
    if (node_store.count == 0) {
        strcpy(node_store.nodes[0].name, "control-plane");
        strcpy(node_store.nodes[0].ip, "127.0.0.1");
        strcpy(node_store.nodes[0].status, "Ready");
        node_store.count = 1;
    }
}

// Helper to convert phase enum to string
static const char* phase_to_string(k8s_phase_t phase) {
    switch (phase) {
        case PHASE_PENDING: return "Pending";
        case PHASE_RUNNING: return "Running";
        case PHASE_SUCCEEDED: return "Succeeded";
        case PHASE_FAILED: return "Failed";
        case PHASE_TERMINATING: return "Terminating";
        default: return "Unknown";
    }
}

// List pods in namespace
int endpoint_list_pods(const char* namespace, char* response_buffer, int* response_code) {
    init_node_store();

    json_object* root = json_object_new_object();
    json_object* items = json_object_new_array();

    // Return pods from store
    for (int i = 0; i < pod_store.count; i++) {
        if (strlen(namespace) == 0 || strcmp(pod_store.pods[i]->metadata.namespace, namespace) == 0) {
            json_object* pod_obj = json_object_new_object();
            
            // Add apiVersion and kind
            json_object_object_add(pod_obj, "apiVersion", json_object_new_string("v1"));
            json_object_object_add(pod_obj, "kind", json_object_new_string("Pod"));
            
            // Add metadata (properly wrapped)
            json_object* metadata = json_object_new_object();
            json_object_object_add(metadata, "name",
                                   json_object_new_string(pod_store.pods[i]->metadata.name));
            json_object_object_add(metadata, "namespace",
                                   json_object_new_string(pod_store.pods[i]->metadata.namespace));
            if (pod_store.pods[i]->metadata.uid) {
                json_object_object_add(metadata, "uid",
                                       json_object_new_string(pod_store.pods[i]->metadata.uid));
            }
            json_object_object_add(pod_obj, "metadata", metadata);
            
            // Add spec with containers
            json_object* spec = json_object_new_object();
            json_object* containers = json_object_new_array();
            
            if (pod_store.pods[i]->spec.containers) {
                for (int c = 0; c < pod_store.pods[i]->spec.num_containers; c++) {
                    json_object* container = json_object_new_object();
                    if (pod_store.pods[i]->spec.containers[c].name) {
                        json_object_object_add(container, "name",
                                               json_object_new_string(pod_store.pods[i]->spec.containers[c].name));
                    }
                    if (pod_store.pods[i]->spec.containers[c].image) {
                        json_object_object_add(container, "image",
                                               json_object_new_string(pod_store.pods[i]->spec.containers[c].image));
                    }
                    if (pod_store.pods[i]->spec.containers[c].image_pull_policy) {
                        json_object_object_add(container, "imagePullPolicy",
                                               json_object_new_string(pod_store.pods[i]->spec.containers[c].image_pull_policy));
                    }
                    json_object_array_add(containers, container);
                }
            }
            
            json_object_object_add(spec, "containers", containers);
            
            if (pod_store.pods[i]->spec.node_name) {
                json_object_object_add(spec, "nodeName",
                                       json_object_new_string(pod_store.pods[i]->spec.node_name));
            }
            if (pod_store.pods[i]->spec.restart_policy) {
                json_object_object_add(spec, "restartPolicy",
                                       json_object_new_string(pod_store.pods[i]->spec.restart_policy));
            }
            
            json_object_object_add(pod_obj, "spec", spec);
            
            // Add status with containerStatuses
            json_object* status = json_object_new_object();
            json_object_object_add(status, "phase",
                                   json_object_new_string(phase_to_string(pod_store.pods[i]->status.phase)));
            
            if (pod_store.pods[i]->status.pod_ip) {
                json_object_object_add(status, "podIP",
                                       json_object_new_string(pod_store.pods[i]->status.pod_ip));
            }
            if (pod_store.pods[i]->status.host_ip) {
                json_object_object_add(status, "hostIP",
                                       json_object_new_string(pod_store.pods[i]->status.host_ip));
            }
            
            // Add containerStatuses
            json_object* container_statuses = json_object_new_array();
            for (int c = 0; c < pod_store.pods[i]->status.num_container_statuses; c++) {
                json_object* cs = json_object_new_object();
                
                // Safety check: only access container if it exists
                if (pod_store.pods[i]->spec.containers && c < pod_store.pods[i]->spec.num_containers &&
                    pod_store.pods[i]->spec.containers[c].name) {
                    json_object_object_add(cs, "name",
                                           json_object_new_string(pod_store.pods[i]->spec.containers[c].name));
                } else {
                    json_object_object_add(cs, "name", json_object_new_string("unknown"));
                }
                
                json_object_object_add(cs, "ready", json_object_new_boolean(
                    pod_store.pods[i]->status.container_statuses[c].state == PHASE_RUNNING ? 1 : 0));
                json_object_object_add(cs, "restartCount", json_object_new_int(0));
                
                json_object* state = json_object_new_object();
                if (pod_store.pods[i]->status.container_statuses[c].state == PHASE_RUNNING) {
                    json_object* running = json_object_new_object();
                    json_object_object_add(running, "startedAt",
                                           json_object_new_string("2026-01-30T00:00:00Z"));
                    json_object_object_add(state, "running", running);
                } else {
                    json_object* waiting = json_object_new_object();
                    json_object_object_add(waiting, "reason", json_object_new_string("ContainerCreating"));
                    json_object_object_add(state, "waiting", waiting);
                }
                json_object_object_add(cs, "state", state);
                
                if (pod_store.pods[i]->status.container_statuses[c].container_id &&
                    strlen(pod_store.pods[i]->status.container_statuses[c].container_id) > 0) {
                    json_object_object_add(cs, "containerID",
                                           json_object_new_string(pod_store.pods[i]->status.container_statuses[c].container_id));
                }
                
                json_object_array_add(container_statuses, cs);
            }
            json_object_object_add(status, "containerStatuses", container_statuses);
            
            json_object_object_add(pod_obj, "status", status);
            
            json_object_array_add(items, pod_obj);
        }
    }

    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("PodList"));
    json_object_object_add(root, "items", items);

    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;

    json_object_put(root);
    return 0;
}

// Get single pod
int endpoint_get_pod(const char* namespace, const char* pod_name,
                     char* response_buffer, int* response_code) {
    for (int i = 0; i < pod_store.count; i++) {
        if (strcmp(pod_store.pods[i]->metadata.name, pod_name) == 0 &&
            strcmp(pod_store.pods[i]->metadata.namespace, namespace) == 0) {
            json_object* pod_obj = json_object_new_object();
            
            // Add apiVersion and kind
            json_object_object_add(pod_obj, "apiVersion", json_object_new_string("v1"));
            json_object_object_add(pod_obj, "kind", json_object_new_string("Pod"));
            
            // Add metadata (properly wrapped)
            json_object* metadata = json_object_new_object();
            json_object_object_add(metadata, "name",
                                   json_object_new_string(pod_store.pods[i]->metadata.name));
            json_object_object_add(metadata, "namespace",
                                   json_object_new_string(pod_store.pods[i]->metadata.namespace));
            if (pod_store.pods[i]->metadata.uid) {
                json_object_object_add(metadata, "uid",
                                       json_object_new_string(pod_store.pods[i]->metadata.uid));
            }
            json_object_object_add(pod_obj, "metadata", metadata);
            
            // Add spec with containers (REQUIRED for kubectl logs)
            json_object* spec = json_object_new_object();
            json_object* containers = json_object_new_array();
            
            for (int c = 0; c < pod_store.pods[i]->spec.num_containers; c++) {
                json_object* container = json_object_new_object();
                if (pod_store.pods[i]->spec.containers[c].name) {
                    json_object_object_add(container, "name",
                                           json_object_new_string(pod_store.pods[i]->spec.containers[c].name));
                }
                if (pod_store.pods[i]->spec.containers[c].image) {
                    json_object_object_add(container, "image",
                                           json_object_new_string(pod_store.pods[i]->spec.containers[c].image));
                }
                if (pod_store.pods[i]->spec.containers[c].image_pull_policy) {
                    json_object_object_add(container, "imagePullPolicy",
                                           json_object_new_string(pod_store.pods[i]->spec.containers[c].image_pull_policy));
                }
                
                // Add ports if any
                if (pod_store.pods[i]->spec.containers[c].num_ports > 0) {
                    json_object* ports = json_object_new_array();
                    for (int p = 0; p < pod_store.pods[i]->spec.containers[c].num_ports; p++) {
                        json_object* port_obj = json_object_new_object();
                        json_object_object_add(port_obj, "containerPort",
                                               json_object_new_int(atoi(pod_store.pods[i]->spec.containers[c].ports[p])));
                        json_object_array_add(ports, port_obj);
                    }
                    json_object_object_add(container, "ports", ports);
                }
                
                json_object_array_add(containers, container);
            }
            
            json_object_object_add(spec, "containers", containers);
            
            if (pod_store.pods[i]->spec.node_name) {
                json_object_object_add(spec, "nodeName",
                                       json_object_new_string(pod_store.pods[i]->spec.node_name));
            }
            if (pod_store.pods[i]->spec.restart_policy) {
                json_object_object_add(spec, "restartPolicy",
                                       json_object_new_string(pod_store.pods[i]->spec.restart_policy));
            }
            
            json_object_object_add(pod_obj, "spec", spec);
            
            // Add status with containerStatuses (REQUIRED for kubectl logs)
            json_object* status = json_object_new_object();
            json_object_object_add(status, "phase",
                                   json_object_new_string(phase_to_string(pod_store.pods[i]->status.phase)));
            
            if (pod_store.pods[i]->status.pod_ip) {
                json_object_object_add(status, "podIP",
                                       json_object_new_string(pod_store.pods[i]->status.pod_ip));
            }
            if (pod_store.pods[i]->status.host_ip) {
                json_object_object_add(status, "hostIP",
                                       json_object_new_string(pod_store.pods[i]->status.host_ip));
            }
            
            // Add containerStatuses (required for logs)
            json_object* container_statuses = json_object_new_array();
            for (int c = 0; c < pod_store.pods[i]->status.num_container_statuses; c++) {
                json_object* cs = json_object_new_object();
                json_object_object_add(cs, "name",
                                       json_object_new_string(pod_store.pods[i]->spec.containers[c].name));
                json_object_object_add(cs, "ready", json_object_new_boolean(
                    pod_store.pods[i]->status.container_statuses[c].state == PHASE_RUNNING ? 1 : 0));
                json_object_object_add(cs, "restartCount", json_object_new_int(0));
                
                json_object* state = json_object_new_object();
                if (pod_store.pods[i]->status.container_statuses[c].state == PHASE_RUNNING) {
                    json_object* running = json_object_new_object();
                    json_object_object_add(running, "startedAt",
                                           json_object_new_string("2026-01-30T00:00:00Z"));
                    json_object_object_add(state, "running", running);
                } else {
                    json_object* waiting = json_object_new_object();
                    json_object_object_add(waiting, "reason", json_object_new_string("ContainerCreating"));
                    json_object_object_add(state, "waiting", waiting);
                }
                json_object_object_add(cs, "state", state);
                
                if (pod_store.pods[i]->status.container_statuses[c].container_id &&
                    strlen(pod_store.pods[i]->status.container_statuses[c].container_id) > 0) {
                    json_object_object_add(cs, "containerID",
                                           json_object_new_string(pod_store.pods[i]->status.container_statuses[c].container_id));
                }
                
                json_object_array_add(container_statuses, cs);
            }
            json_object_object_add(status, "containerStatuses", container_statuses);
            
            json_object_object_add(pod_obj, "status", status);

            const char* json_str = json_object_to_json_string(pod_obj);
            strcpy(response_buffer, json_str);
            *response_code = 200;
            json_object_put(pod_obj);
            return 0;
        }
    }

    const char* not_found = "{\"error\":\"pod not found\"}";
    strcpy(response_buffer, not_found);
    *response_code = 404;
    return 0;
}

// Create pod
int endpoint_create_pod(const char* namespace, const char* body,
                        char* response_buffer, int* response_code) {
    
    fprintf(stderr, "[DEBUG] endpoint_create_pod called\n");
    fflush(stderr);
    
    if (!body || strlen(body) == 0) {
        fprintf(stderr, "[ERROR] Empty body\n");
        snprintf(response_buffer, 16384, "{\"error\":\"empty body\"}");
        *response_code = 400;
        return 0;
    }

    fprintf(stderr, "[DEBUG] Body length: %lu, Body: %.200s\n", strlen(body), body);
    fflush(stderr);
    
    // Parse JSON body to extract pod name and spec
    json_object* root = json_tokener_parse(body);
    fprintf(stderr, "[DEBUG] JSON parse result: %p\n", (void*)root);
    fflush(stderr);
    
    // Extract pod name
    char pod_name[256] = "pod-unknown";
    json_object* metadata_obj = NULL;
    fprintf(stderr, "[DEBUG] Extracting pod name from metadata\n");
    fflush(stderr);
    if (json_object_object_get_ex(root, "metadata", &metadata_obj) && metadata_obj) {
        json_object* name_obj = NULL;
        if (json_object_object_get_ex(metadata_obj, "name", &name_obj) && name_obj) {
            const char* name = json_object_get_string(name_obj);
            if (name && strlen(name) > 0) {
                strncpy(pod_name, name, 255);
                pod_name[255] = '\0';
                fprintf(stderr, "[DEBUG] Pod name extracted: %s\n", pod_name);
                fflush(stderr);
            }
        }
    }

    // Create pod object and add to store
    k8s_pod_t* pod = (k8s_pod_t*)malloc(sizeof(k8s_pod_t));
    fprintf(stderr, "[DEBUG] After pod malloc, pod=%p\n", (void*)pod);
    fflush(stderr);
    if (!pod) {
        snprintf(response_buffer, 16384, "{\"error\":\"memory allocation failed\"}");
        *response_code = 500;
        json_object_put(root);
        return -1;
    }
    
    memset(pod, 0, sizeof(k8s_pod_t));
    fprintf(stderr, "[DEBUG] After memset\n");
    fflush(stderr);
    
    // Set metadata
    pod->metadata.name = (char*)malloc(strlen(pod_name) + 1);
    fprintf(stderr, "[DEBUG] After name malloc: %p\n", (void*)pod->metadata.name);
    fflush(stderr);
    if (!pod->metadata.name) {
        free(pod);
        snprintf(response_buffer, 16384, "{\"error\":\"memory allocation failed\"}");
        *response_code = 500;
        json_object_put(root);
        return -1;
    }
    strcpy(pod->metadata.name, pod_name);
    fprintf(stderr, "[DEBUG] After strcpy(name), About to set namespace\n");
    fflush(stderr);
    
    const char* ns = namespace ? namespace : "default";
    pod->metadata.namespace = (char*)malloc(strlen(ns) + 1);
    fprintf(stderr, "[DEBUG] After namespace malloc: %p\n", (void*)pod->metadata.namespace);
    fflush(stderr);
    if (!pod->metadata.namespace) {
        free(pod->metadata.name);
        free(pod);
        snprintf(response_buffer, 16384, "{\"error\":\"memory allocation failed\"}");
        *response_code = 500;
        json_object_put(root);
        return -1;
    }
    strcpy(pod->metadata.namespace, ns);
    
    // Set status
    pod->status.phase = PHASE_PENDING;
    fprintf(stderr, "[DEBUG] Metadata and status set, about to parse containers\n");
    fflush(stderr);
    
    // Extract spec.containers from request body
    json_object* spec_obj = NULL;
    fprintf(stderr, "[DEBUG] Extracting spec.containers\n");
    fflush(stderr);
    if (json_object_object_get_ex(root, "spec", &spec_obj) && spec_obj) {
        fprintf(stderr, "[DEBUG] Got spec_obj\n");
        fflush(stderr);
        json_object* containers_obj = NULL;
        if (json_object_object_get_ex(spec_obj, "containers", &containers_obj) && containers_obj) {
            fprintf(stderr, "[DEBUG] Got containers_obj\n");
            fflush(stderr);
            // Parse containers array
            int num_containers = json_object_array_length(containers_obj);
            fprintf(stderr, "[DEBUG] num_containers=%d\n", num_containers);
            fflush(stderr);
            if (num_containers > 16) num_containers = 16;  // Max 16 containers per pod
            
            // ALLOCATE the containers array!
            fprintf(stderr, "[DEBUG] Allocating %d containers\n", num_containers);
            fflush(stderr);
            pod->spec.containers = (k8s_container_t*)malloc(sizeof(k8s_container_t) * num_containers);
            if (!pod->spec.containers) {
                fprintf(stderr, "[ERROR] Failed to allocate containers array\n");
                fflush(stderr);
                free(pod->metadata.namespace);
                free(pod->metadata.name);
                free(pod);
                snprintf(response_buffer, 16384, "{\"error\":\"memory allocation failed\"}");
                *response_code = 500;
                json_object_put(root);
                return -1;
            }
            memset(pod->spec.containers, 0, sizeof(k8s_container_t) * num_containers);
            
            for (int i = 0; i < num_containers; i++) {
                fprintf(stderr, "[DEBUG] Processing container %d\n", i);
                fflush(stderr);
                json_object* container_obj = json_object_array_get_idx(containers_obj, i);
                fprintf(stderr, "[DEBUG] container_obj=%p\n", (void*)container_obj);
                fflush(stderr);
                if (!container_obj) continue;
                
                k8s_container_t* container = &pod->spec.containers[i];
                fprintf(stderr, "[DEBUG] container pointer=%p\n", (void*)container);
                fflush(stderr);
                
                // Extract container name
                json_object* c_name_obj = NULL;
                if (json_object_object_get_ex(container_obj, "name", &c_name_obj) && c_name_obj) {
                    const char* c_name = json_object_get_string(c_name_obj);
                    if (c_name && strlen(c_name) > 0) {
                        container->name = (char*)malloc(strlen(c_name) + 1);
                        if (container->name) {
                            strcpy(container->name, c_name);
                        }
                    }
                }
                
                // Extract container image
                json_object* c_image_obj = NULL;
                if (json_object_object_get_ex(container_obj, "image", &c_image_obj) && c_image_obj) {
                    const char* c_image = json_object_get_string(c_image_obj);
                    if (c_image && strlen(c_image) > 0) {
                        container->image = (char*)malloc(strlen(c_image) + 1);
                        if (container->image) {
                            strcpy(container->image, c_image);
                        }
                    }
                }
                
                // Set default imagePullPolicy
                container->image_pull_policy = (char*)malloc(strlen("IfNotPresent") + 1);
                if (container->image_pull_policy) {
                    strcpy(container->image_pull_policy, "IfNotPresent");
                }
                
                pod->spec.num_containers++;
            }
        }
        
        // Extract spec.restartPolicy
        json_object* restart_policy_obj = NULL;
        if (json_object_object_get_ex(spec_obj, "restartPolicy", &restart_policy_obj) && restart_policy_obj) {
            const char* restart_policy = json_object_get_string(restart_policy_obj);
            if (restart_policy && strlen(restart_policy) > 0) {
                pod->spec.restart_policy = (char*)malloc(strlen(restart_policy) + 1);
                if (pod->spec.restart_policy) {
                    strcpy(pod->spec.restart_policy, restart_policy);
                }
            }
        }
        if (!pod->spec.restart_policy) {
            pod->spec.restart_policy = (char*)malloc(strlen("Always") + 1);
            if (pod->spec.restart_policy) {
                strcpy(pod->spec.restart_policy, "Always");
            }
        }
    }
    
    // Initialize container statuses to match containers
    pod->status.num_container_statuses = pod->spec.num_containers;
    for (int i = 0; i < pod->spec.num_containers; i++) {
        pod->status.container_statuses[i].state = PHASE_PENDING;
        pod->status.container_statuses[i].reason = (char*)malloc(strlen("ContainerCreating") + 1);
        if (pod->status.container_statuses[i].reason) {
            strcpy(pod->status.container_statuses[i].reason, "ContainerCreating");
        }
    }
    
    // Add to store
    fprintf(stderr, "[DEBUG] Adding pod to store. pod_store.count=%d\n", pod_store.count);
    fflush(stderr);
    if (pod_store.count < 1000) {
        pod_store.pods[pod_store.count++] = pod;
        
        // Persist to etcd if available
        if (g_etcd_client) {
            if (store_save_pod_to_etcd(pod) == 0) {
                fprintf(stderr, "[DEBUG] Pod persisted to etcd: %s/%s\n", ns, pod_name);
            } else {
                fprintf(stderr, "[WARNING] Failed to persist pod to etcd\n");
            }
        }
    } else {
        snprintf(response_buffer, 16384, "{\"error\":\"pod store full\"}");
        *response_code = 500;
        json_object_put(root);
        return -1;
    }

    // Build response with spec and status
    json_object* resp = json_object_new_object();
    json_object_object_add(resp, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(resp, "kind", json_object_new_string("Pod"));
    
    json_object* meta = json_object_new_object();
    json_object_object_add(meta, "name", json_object_new_string(pod_name));
    json_object_object_add(meta, "namespace", json_object_new_string(ns));
    json_object_object_add(resp, "metadata", meta);
    
    // Add spec with containers to response
    json_object* spec = json_object_new_object();
    json_object* containers = json_object_new_array();
    
    for (int c = 0; c < pod->spec.num_containers; c++) {
        json_object* container = json_object_new_object();
        if (pod->spec.containers[c].name) {
            json_object_object_add(container, "name",
                                   json_object_new_string(pod->spec.containers[c].name));
        }
        if (pod->spec.containers[c].image) {
            json_object_object_add(container, "image",
                                   json_object_new_string(pod->spec.containers[c].image));
        }
        if (pod->spec.containers[c].image_pull_policy) {
            json_object_object_add(container, "imagePullPolicy",
                                   json_object_new_string(pod->spec.containers[c].image_pull_policy));
        }
        json_object_array_add(containers, container);
    }
    
    json_object_object_add(spec, "containers", containers);
    
    if (pod->spec.restart_policy) {
        json_object_object_add(spec, "restartPolicy",
                               json_object_new_string(pod->spec.restart_policy));
    }
    json_object_object_add(resp, "spec", spec);
    
    // Add status with containerStatuses
    json_object* status = json_object_new_object();
    json_object_object_add(status, "phase", json_object_new_string("Pending"));
    
    json_object* container_statuses = json_object_new_array();
    for (int c = 0; c < pod->spec.num_containers; c++) {
        json_object* cs = json_object_new_object();
        if (pod->spec.containers[c].name) {
            json_object_object_add(cs, "name",
                                   json_object_new_string(pod->spec.containers[c].name));
        }
        json_object_object_add(cs, "ready", json_object_new_boolean(0));
        json_object_object_add(cs, "restartCount", json_object_new_int(0));
        
        json_object* state = json_object_new_object();
        json_object* waiting = json_object_new_object();
        json_object_object_add(waiting, "reason", json_object_new_string("ContainerCreating"));
        json_object_object_add(state, "waiting", waiting);
        json_object_object_add(cs, "state", state);
        
        json_object_array_add(container_statuses, cs);
    }
    json_object_object_add(status, "containerStatuses", container_statuses);
    json_object_object_add(resp, "status", status);

    const char* json_str = json_object_to_json_string(resp);
    strncpy(response_buffer, json_str, 16383);
    response_buffer[16383] = '\0';
    
    *response_code = 201;
    
    // Note: Pod controller monitors API server directly and will fetch this pod
    // via pod_controller_fetch_pods() - no direct function calls between processes
    
    json_object_put(resp);
    json_object_put(root);
    fprintf(stderr, "[DEBUG] endpoint_create_pod completed successfully\n");
    fflush(stderr);
    return 0;
}

// Delete pod
int endpoint_delete_pod(const char* namespace, const char* pod_name,
                        char* response_buffer, int* response_code) {
    for (int i = 0; i < pod_store.count; i++) {
        if (strcmp(pod_store.pods[i]->metadata.name, pod_name) == 0 &&
            strcmp(pod_store.pods[i]->metadata.namespace, namespace) == 0) {
            
            // Check if finalizer already exists (pod already marked for deletion)
            int has_finalizer = 0;
            for (int f = 0; f < pod_store.pods[i]->metadata.num_finalizers; f++) {
                if (strcmp(pod_store.pods[i]->metadata.finalizers[f], "sirah.io/cleanup") == 0) {
                    has_finalizer = 1;
                    break;
                }
            }
            
            if (!has_finalizer) {
                // Add finalizer to mark pod for graceful cleanup
                pod_store.pods[i]->metadata.finalizers = realloc(pod_store.pods[i]->metadata.finalizers,
                                                                  (pod_store.pods[i]->metadata.num_finalizers + 1) * sizeof(char*));
                pod_store.pods[i]->metadata.finalizers[pod_store.pods[i]->metadata.num_finalizers] = 
                    malloc(20);
                strcpy(pod_store.pods[i]->metadata.finalizers[pod_store.pods[i]->metadata.num_finalizers], 
                       "sirah.io/cleanup");
                pod_store.pods[i]->metadata.num_finalizers++;
                
                // Set deletion timestamp
                pod_store.pods[i]->metadata.deletion_timestamp = time(NULL);
                
                // Update phase to Terminating
                pod_store.pods[i]->status.phase = PHASE_TERMINATING;
                
                fprintf(stderr, "[ENDPOINTS] Pod marked for deletion: %s/%s (finalizer added)\n", 
                        namespace, pod_name);
                
                // Persist deletion state to etcd
                if (g_etcd_client) {
                    if (store_save_pod_to_etcd(pod_store.pods[i]) == 0) {
                        fprintf(stderr, "[DEBUG] Pod deletion state persisted to etcd: %s/%s\n", 
                                namespace, pod_name);
                    } else {
                        fprintf(stderr, "[WARNING] Failed to persist pod deletion state to etcd\n");
                    }
                }
            }

            strcpy(response_buffer, "{\"status\":\"terminating\"}");
            *response_code = 200;
            return 0;
        }
    }

    strcpy(response_buffer, "{\"error\":\"pod not found\"}");
    *response_code = 404;
    return 0;
}

// List nodes
int endpoint_list_nodes(char* response_buffer, int* response_code) {
    init_node_store();

    json_object* root = json_object_new_object();
    json_object* items = json_object_new_array();

    for (int i = 0; i < node_store.count; i++) {
        json_object* node_obj = json_object_new_object();
        
        // Add apiVersion and kind
        json_object_object_add(node_obj, "apiVersion", json_object_new_string("v1"));
        json_object_object_add(node_obj, "kind", json_object_new_string("Node"));
        
        // Add metadata
        json_object* metadata = json_object_new_object();
        json_object_object_add(metadata, "name",
                               json_object_new_string(node_store.nodes[i].name));
        json_object_object_add(node_obj, "metadata", metadata);
        
        // Add status
        json_object* status = json_object_new_object();
        json_object_object_add(status, "status",
                               json_object_new_string(node_store.nodes[i].status));
        json_object_object_add(node_obj, "status", status);
        
        json_object_array_add(items, node_obj);
    }

    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("NodeList"));
    json_object_object_add(root, "items", items);

    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;

    json_object_put(root);
    return 0;
}

// Get node
int endpoint_get_node(const char* node_name, char* response_buffer, int* response_code) {
    init_node_store();

    for (int i = 0; i < node_store.count; i++) {
        if (strcmp(node_store.nodes[i].name, node_name) == 0) {
            json_object* node_obj = json_object_new_object();
            json_object_object_add(node_obj, "name",
                                   json_object_new_string(node_store.nodes[i].name));
            json_object_object_add(node_obj, "ip",
                                   json_object_new_string(node_store.nodes[i].ip));
            json_object_object_add(node_obj, "status",
                                   json_object_new_string(node_store.nodes[i].status));

            const char* json_str = json_object_to_json_string(node_obj);
            strcpy(response_buffer, json_str);
            *response_code = 200;
            json_object_put(node_obj);
            return 0;
        }
    }

    strcpy(response_buffer, "{\"error\":\"node not found\"}");
    *response_code = 404;
    return 0;
}

// Register new node
int endpoint_register_node(const char* body, char* response_buffer, int* response_code) {
    if (!body || strlen(body) == 0) {
        strcpy(response_buffer, "{\"error\":\"missing node data\"}");
        *response_code = 400;
        return 0;
    }

    // Parse node registration request
    json_object* req = json_tokener_parse(body);
    if (!req) {
        strcpy(response_buffer, "{\"error\":\"invalid json\"}");
        *response_code = 400;
        return 0;
    }

    json_object* resp = json_object_new_object();
    json_object_object_add(resp, "status", json_object_new_string("registered"));
    json_object_object_add(resp, "message", json_object_new_string("Node registered successfully"));

    const char* json_str = json_object_to_json_string(resp);
    strcpy(response_buffer, json_str);
    *response_code = 200;

    json_object_put(req);
    json_object_put(resp);
    return 0;
}

// Node heartbeat endpoint
int endpoint_node_heartbeat(const char* node_name, const char* body,
                           char* response_buffer, int* response_code) {
    if (!node_name || strlen(node_name) == 0) {
        strcpy(response_buffer, "{\"error\":\"missing node name\"}");
        *response_code = 400;
        return 0;
    }

    json_object* resp = json_object_new_object();
    json_object_object_add(resp, "status", json_object_new_string("heartbeat received"));
    json_object_object_add(resp, "timestamp", json_object_new_int((int)time(NULL)));

    const char* json_str = json_object_to_json_string(resp);
    strcpy(response_buffer, json_str);
    *response_code = 200;

    json_object_put(resp);
    return 0;
}

// Bind pod to node (scheduler binding)
int endpoint_bind_pod(const char* namespace, const char* pod_name, const char* body,
                     char* response_buffer, int* response_code) {
    if (!pod_name || strlen(pod_name) == 0) {
        strcpy(response_buffer, "{\"error\":\"missing pod name\"}");
        *response_code = 400;
        return 0;
    }

    // Parse binding request
    json_object* req = json_tokener_parse(body);
    if (!req) {
        strcpy(response_buffer, "{\"error\":\"invalid json\"}");
        *response_code = 400;
        return 0;
    }

    json_object* node_name_obj = NULL;
    const char* node_name = NULL;
    
    if (json_object_object_get_ex(req, "nodeName", &node_name_obj)) {
        node_name = json_object_get_string(node_name_obj);
    }

    if (!node_name) {
        strcpy(response_buffer, "{\"error\":\"missing nodeName\"}");
        *response_code = 400;
        json_object_put(req);
        return 0;
    }

    // Update pod in store - mark as bound
    for (int i = 0; i < pod_store.count; i++) {
        if (strcmp(pod_store.pods[i]->metadata.namespace, namespace) == 0 &&
            strcmp(pod_store.pods[i]->metadata.name, pod_name) == 0) {
            
            // Update pod status
            pod_store.pods[i]->spec.node_name = malloc(strlen(node_name) + 1);
            strcpy(pod_store.pods[i]->spec.node_name, node_name);
            pod_store.pods[i]->status.phase = PHASE_RUNNING;

            json_object* resp = json_object_new_object();
            json_object_object_add(resp, "status", json_object_new_string("bound"));
            json_object_object_add(resp, "nodeName", json_object_new_string(node_name));

            const char* json_str = json_object_to_json_string(resp);
            strcpy(response_buffer, json_str);
            *response_code = 200;

            json_object_put(resp);
            json_object_put(req);
            return 0;
        }
    }

    strcpy(response_buffer, "{\"error\":\"pod not found\"}");
    *response_code = 404;
    json_object_put(req);
    return 0;
}

// Pod status update endpoint
int endpoint_pod_status(const char* namespace, const char* pod_name, const char* body,
                       char* response_buffer, int* response_code) {
    if (!namespace || !pod_name || !body) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid parameters\"}");
        return -1;
    }
    
    // Parse status update body
    json_object* update = json_tokener_parse(body);
    if (!update) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    json_object* phase_obj = json_object_object_get(update, "phase");
    const char* phase = phase_obj ? json_object_get_string(phase_obj) : "Running";
    
    // Get pod from storage
    for (int i = 0; i < pod_store.count; i++) {
        if (pod_store.pods[i] && 
            strcmp(pod_store.pods[i]->metadata.namespace, namespace) == 0 &&
            strcmp(pod_store.pods[i]->metadata.name, pod_name) == 0) {
            
            // Update pod phase
            if (strcmp(phase, "Pending") == 0) {
                pod_store.pods[i]->status.phase = PHASE_PENDING;
            } else if (strcmp(phase, "Running") == 0) {
                pod_store.pods[i]->status.phase = PHASE_RUNNING;
            } else if (strcmp(phase, "Succeeded") == 0) {
                pod_store.pods[i]->status.phase = PHASE_SUCCEEDED;
            } else if (strcmp(phase, "Failed") == 0) {
                pod_store.pods[i]->status.phase = PHASE_FAILED;
            }
            
            if (!pod_store.pods[i]->status.start_time) {
                pod_store.pods[i]->status.start_time = time(NULL);
            }
            
            // Persist status update to etcd
            if (g_etcd_client) {
                if (store_save_pod_to_etcd(pod_store.pods[i]) == 0) {
                    fprintf(stderr, "[DEBUG] Pod status persisted to etcd: %s/%s -> %s\n", 
                            namespace, pod_name, phase);
                } else {
                    fprintf(stderr, "[WARNING] Failed to persist pod status to etcd\n");
                }
            }
            
            // Build response
            json_object* resp = json_object_new_object();
            json_object_object_add(resp, "name", json_object_new_string(pod_name));
            json_object_object_add(resp, "phase", json_object_new_string(phase));
            json_object_object_add(resp, "timestamp", json_object_new_int64(time(NULL)));
            
            const char* json_str = json_object_to_json_string(resp);
            strcpy(response_buffer, json_str);
            
            *response_code = 200;
            json_object_put(resp);
            json_object_put(update);
            return 0;
        }
    }
    
    *response_code = 404;
    strcpy(response_buffer, "{\"error\":\"pod not found\"}");
    json_object_put(update);
    return 0;
}



// Global service store (in-memory for MVP)
static struct {
    k8s_service_t* services[1000];
    int count;
} service_store = {0};

// List services in namespace
int endpoint_list_services(const char* namespace, char* response_buffer, int* response_code) {
    fprintf(stderr, "[DEBUG] endpoint_list_services called with namespace=%s, count=%d\n", namespace, service_store.count);
    json_object* root = json_object_new_object();
    json_object* items = json_object_new_array();
    
    for (int i = 0; i < service_store.count; i++) {
        if (strlen(namespace) == 0 || strcmp(service_store.services[i]->metadata.namespace, namespace) == 0) {
            json_object* svc_obj = json_object_new_object();
            json_object_object_add(svc_obj, "apiVersion", json_object_new_string("v1"));
            json_object_object_add(svc_obj, "kind", json_object_new_string("Service"));
            
            json_object* metadata = json_object_new_object();
            json_object_object_add(metadata, "name", json_object_new_string(service_store.services[i]->metadata.name));
            json_object_object_add(metadata, "namespace", json_object_new_string(service_store.services[i]->metadata.namespace));
            json_object_object_add(svc_obj, "metadata", metadata);
            
            json_object* status = json_object_new_object();
            json_object_object_add(status, "loadBalancer", json_object_new_object());
            json_object_object_add(svc_obj, "status", status);
            
            json_object_array_add(items, svc_obj);
        }
    }
    
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("ServiceList"));
    json_object_object_add(root, "items", items);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    
    json_object_put(root);
    return 0;
}

// Get single service
int endpoint_get_service(const char* namespace, const char* name,
                        char* response_buffer, int* response_code) {
    for (int i = 0; i < service_store.count; i++) {
        if (strcmp(service_store.services[i]->metadata.name, name) == 0 &&
            strcmp(service_store.services[i]->metadata.namespace, namespace) == 0) {
            char* json_str = k8s_service_to_json(service_store.services[i]);
            strcpy(response_buffer, json_str);
            *response_code = 200;
            free(json_str);
            return 0;
        }
    }
    
    strcpy(response_buffer, "{\"error\":\"service not found\"}");
    *response_code = 404;
    return -1;
}

// Create service
int endpoint_create_service(const char* namespace, const char* body,
                           char* response_buffer, int* response_code) {
    if (!namespace || !body) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid parameters\"}");
        return -1;
    }
    
    json_object* req = json_tokener_parse(body);
    if (!req) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    json_object* meta = json_object_object_get(req, "metadata");
    const char* svc_name = json_object_get_string(json_object_object_get(meta, "name"));
    
    if (!svc_name) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"service name required\"}");
        json_object_put(req);
        return -1;
    }
    
    if (service_store.count >= 1000) {
        *response_code = 507;
        strcpy(response_buffer, "{\"error\":\"service store full\"}");
        json_object_put(req);
        return -1;
    }
    
    k8s_service_t* svc = k8s_service_new(svc_name, namespace);
    service_store.services[service_store.count++] = svc;
    
    char* json_str = k8s_service_to_json(svc);
    strcpy(response_buffer, json_str);
    *response_code = 201;
    
    free(json_str);
    json_object_put(req);
    return 0;
}

// Delete service
int endpoint_delete_service(const char* namespace, const char* name,
                           char* response_buffer, int* response_code) {
    for (int i = 0; i < service_store.count; i++) {
        if (strcmp(service_store.services[i]->metadata.name, name) == 0 &&
            strcmp(service_store.services[i]->metadata.namespace, namespace) == 0) {
            k8s_service_free(service_store.services[i]);
            memmove(&service_store.services[i], &service_store.services[i + 1],
                   sizeof(k8s_service_t*) * (service_store.count - i - 1));
            service_store.count--;
            
            strcpy(response_buffer, "{\"status\":\"deleted\"}");
            *response_code = 200;
            return 0;
        }
    }
    
    strcpy(response_buffer, "{\"error\":\"service not found\"}");
    *response_code = 404;
    return -1;
}

// Event store for pod events
static struct {
    struct {
        char pod_name[256];
        char pod_namespace[256];
        char reason[256];
        char message[512];
        time_t timestamp;
    } events[1000];
    int count;
} event_store = {0};

// Get pod events
int endpoint_pod_events(const char* namespace, const char* pod_name,
                       char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object* items = json_object_new_array();
    
    // Find events for this pod
    for (int i = 0; i < event_store.count; i++) {
        if (strcmp(event_store.events[i].pod_namespace, namespace) == 0 &&
            strcmp(event_store.events[i].pod_name, pod_name) == 0) {
            json_object* event_obj = json_object_new_object();
            json_object_object_add(event_obj, "reason", 
                                   json_object_new_string(event_store.events[i].reason));
            json_object_object_add(event_obj, "message",
                                   json_object_new_string(event_store.events[i].message));
            json_object_object_add(event_obj, "timestamp",
                                   json_object_new_int64(event_store.events[i].timestamp));
            json_object_array_add(items, event_obj);
        }
    }
    
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("EventList"));
    json_object_object_add(root, "items", items);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    
    json_object_put(root);
    return 0;
}

// Internal function to record event (called by controllers)
int record_pod_event(const char* pod_name, const char* pod_namespace, 
                    const char* reason, const char* message) {
    if (event_store.count >= 1000) {
        // Shift events
        memmove(&event_store.events[0], &event_store.events[1], 
               sizeof(event_store.events[0]) * (event_store.count - 1));
        event_store.count--;
    }
    
    strcpy(event_store.events[event_store.count].pod_name, pod_name);
    strcpy(event_store.events[event_store.count].pod_namespace, pod_namespace);
    strcpy(event_store.events[event_store.count].reason, reason);
    strcpy(event_store.events[event_store.count].message, message);
    event_store.events[event_store.count].timestamp = time(NULL);
    
    event_store.count++;
    return 0;
}
// ==================== PATCH ENDPOINTS ====================

// Patch pod
int endpoint_patch_pod(const char* namespace, const char* name, const char* body,
                      const char* content_type, char* response_buffer, int* response_code) {
    // Find the pod
    for (int i = 0; i < pod_store.count; i++) {
        if (strcmp(pod_store.pods[i]->metadata.name, name) == 0 &&
            strcmp(pod_store.pods[i]->metadata.namespace, namespace) == 0) {
            
            // Parse current pod as JSON
            json_object* current_pod = json_object_new_object();
            json_object* metadata = json_object_new_object();
            json_object_object_add(metadata, "name", json_object_new_string(pod_store.pods[i]->metadata.name));
            json_object_object_add(metadata, "namespace", json_object_new_string(pod_store.pods[i]->metadata.namespace));
            json_object_object_add(current_pod, "metadata", metadata);
            
            // Parse patch
            json_object* patch = json_tokener_parse(body);
            if (!patch) {
                *response_code = 400;
                strcpy(response_buffer, "{\"error\":\"invalid JSON patch\"}");
                return -1;
            }
            
            // Apply patch
            char error_buffer[256] = {0};
            int result = 0;
            
            if (strstr(content_type ? content_type : "", "json-patch")) {
                // JSON Patch (RFC 6902)
                // result = apply_json_patch(current_pod, patch, error_buffer, sizeof(error_buffer));
            } else {
                // Strategic Merge Patch (default)
                // result = apply_strategic_merge_patch(current_pod, patch, error_buffer, sizeof(error_buffer));
            }
            
            if (result != 0) {
                *response_code = 400;
                snprintf(response_buffer, 4096, "{\"error\":\"%s\"}", error_buffer);
                json_object_put(patch);
                json_object_put(current_pod);
                return -1;
            }
            
            // Return patched pod
            const char* json_str = json_object_to_json_string(current_pod);
            strcpy(response_buffer, json_str);
            *response_code = 200;
            json_object_put(patch);
            json_object_put(current_pod);
            return 0;
        }
    }
    
    *response_code = 404;
    strcpy(response_buffer, "{\"error\":\"pod not found\"}");
    return -1;
}

// Patch service
int endpoint_patch_service(const char* namespace, const char* name, const char* body,
                          const char* content_type, char* response_buffer, int* response_code) {
    // Simplified: return success
    json_object* result = json_object_new_object();
    json_object_object_add(result, "kind", json_object_new_string("Service"));
    json_object_object_add(result, "apiVersion", json_object_new_string("v1"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(result, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(result);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(result);
    return 0;
}

// Patch configmap
int endpoint_patch_configmap(const char* namespace, const char* name, const char* body,
                            const char* content_type, char* response_buffer, int* response_code) {
    json_object* result = json_object_new_object();
    json_object_object_add(result, "kind", json_object_new_string("ConfigMap"));
    json_object_object_add(result, "apiVersion", json_object_new_string("v1"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(result, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(result);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(result);
    return 0;
}

// Patch secret
int endpoint_patch_secret(const char* namespace, const char* name, const char* body,
                         const char* content_type, char* response_buffer, int* response_code) {
    json_object* result = json_object_new_object();
    json_object_object_add(result, "kind", json_object_new_string("Secret"));
    json_object_object_add(result, "apiVersion", json_object_new_string("v1"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(result, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(result);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(result);
    return 0;
}

// ==================== WATCH ENDPOINTS ====================

// Watch pods
int endpoint_watch_pods(const char* namespace, const char* query_string,
                       char* response_buffer, int* response_code) {
    // Start watch session
    int watch_id = watch_start("Pod", namespace ? namespace : "default", "", "");
    if (watch_id < 0) {
        *response_code = 500;
        strcpy(response_buffer, "{\"error\":\"failed to start watch\"}");
        return -1;
    }
    
    // Build watch response header
    const char* watch_header = "{"
        "\"type\":\"ADDED\","
        "\"object\":{"
            "\"apiVersion\":\"v1\","
            "\"kind\":\"Pod\","
            "\"metadata\":{"
                "\"name\":\"watch-stream\","
                "\"namespace\":\"default\""
            "}"
        "}"
    "}";
    
    strcpy(response_buffer, watch_header);
    *response_code = 200;
    return 0;
}

// Watch services
int endpoint_watch_services(const char* namespace, const char* query_string,
                           char* response_buffer, int* response_code) {
    int watch_id = watch_start("Service", namespace ? namespace : "default", "", "");
    if (watch_id < 0) {
        *response_code = 500;
        strcpy(response_buffer, "{\"error\":\"failed to start watch\"}");
        return -1;
    }
    
    const char* watch_header = "{"
        "\"type\":\"ADDED\","
        "\"object\":{"
            "\"apiVersion\":\"v1\","
            "\"kind\":\"Service\","
            "\"metadata\":{"
                "\"name\":\"watch-stream\","
                "\"namespace\":\"default\""
            "}"
        "}"
    "}";
    
    strcpy(response_buffer, watch_header);
    *response_code = 200;
    return 0;
}

// Watch deployments
int endpoint_watch_deployments(const char* namespace, const char* query_string,
                              char* response_buffer, int* response_code) {
    int watch_id = watch_start("Deployment", namespace ? namespace : "default", "", "");
    if (watch_id < 0) {
        *response_code = 500;
        strcpy(response_buffer, "{\"error\":\"failed to start watch\"}");
        return -1;
    }
    
    const char* watch_header = "{"
        "\"type\":\"ADDED\","
        "\"object\":{"
            "\"apiVersion\":\"apps/v1\","
            "\"kind\":\"Deployment\","
            "\"metadata\":{"
                "\"name\":\"watch-stream\","
                "\"namespace\":\"default\""
            "}"
        "}"
    "}";
    
    strcpy(response_buffer, watch_header);
    *response_code = 200;
    return 0;
}

// ==================== DEPLOYMENT ENDPOINTS ====================

int endpoint_list_deployments(const char* namespace, char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("apps/v1"));
    json_object_object_add(root, "kind", json_object_new_string("DeploymentList"));
    json_object_object_add(root, "items", json_object_new_array());
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_get_deployment(const char* namespace, const char* name,
                           char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("apps/v1"));
    json_object_object_add(root, "kind", json_object_new_string("Deployment"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(root, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_create_deployment(const char* namespace, const char* body,
                              char* response_buffer, int* response_code) {
    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    const char* json_str = json_object_to_json_string(obj);
    strcpy(response_buffer, json_str);
    *response_code = 201;
    json_object_put(obj);
    return 0;
}

int endpoint_update_deployment(const char* namespace, const char* name, const char* body,
                              char* response_buffer, int* response_code) {
    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    const char* json_str = json_object_to_json_string(obj);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(obj);
    return 0;
}

int endpoint_patch_deployment(const char* namespace, const char* name, const char* body,
                             const char* content_type, char* response_buffer, int* response_code) {
    json_object* patch = json_tokener_parse(body);
    if (!patch) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("apps/v1"));
    json_object_object_add(root, "kind", json_object_new_string("Deployment"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(root, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(patch);
    json_object_put(root);
    return 0;
}

int endpoint_delete_deployment(const char* namespace, const char* name,
                              char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "kind", json_object_new_string("Status"));
    json_object_object_add(root, "status", json_object_new_string("Success"));
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

// ==================== DAEMONSET ENDPOINTS ====================

int endpoint_list_daemonsets(const char* namespace, char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("apps/v1"));
    json_object_object_add(root, "kind", json_object_new_string("DaemonSetList"));
    json_object_object_add(root, "items", json_object_new_array());
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_get_daemonset(const char* namespace, const char* name,
                          char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("apps/v1"));
    json_object_object_add(root, "kind", json_object_new_string("DaemonSet"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(root, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_create_daemonset(const char* namespace, const char* body,
                             char* response_buffer, int* response_code) {
    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    const char* json_str = json_object_to_json_string(obj);
    strcpy(response_buffer, json_str);
    *response_code = 201;
    json_object_put(obj);
    return 0;
}

int endpoint_update_daemonset(const char* namespace, const char* name, const char* body,
                             char* response_buffer, int* response_code) {
    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    const char* json_str = json_object_to_json_string(obj);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(obj);
    return 0;
}

int endpoint_patch_daemonset(const char* namespace, const char* name, const char* body,
                            const char* content_type, char* response_buffer, int* response_code) {
    json_object* patch = json_tokener_parse(body);
    if (!patch) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("apps/v1"));
    json_object_object_add(root, "kind", json_object_new_string("DaemonSet"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(root, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(patch);
    json_object_put(root);
    return 0;
}

int endpoint_delete_daemonset(const char* namespace, const char* name,
                             char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "kind", json_object_new_string("Status"));
    json_object_object_add(root, "status", json_object_new_string("Success"));
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

// ==================== JOB ENDPOINTS ====================

int endpoint_list_jobs(const char* namespace, char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("batch/v1"));
    json_object_object_add(root, "kind", json_object_new_string("JobList"));
    json_object_object_add(root, "items", json_object_new_array());
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_get_job(const char* namespace, const char* name,
                    char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("batch/v1"));
    json_object_object_add(root, "kind", json_object_new_string("Job"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(root, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_create_job(const char* namespace, const char* body,
                       char* response_buffer, int* response_code) {
    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    const char* json_str = json_object_to_json_string(obj);
    strcpy(response_buffer, json_str);
    *response_code = 201;
    json_object_put(obj);
    return 0;
}

int endpoint_update_job(const char* namespace, const char* name, const char* body,
                       char* response_buffer, int* response_code) {
    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    const char* json_str = json_object_to_json_string(obj);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(obj);
    return 0;
}

int endpoint_patch_job(const char* namespace, const char* name, const char* body,
                      const char* content_type, char* response_buffer, int* response_code) {
    json_object* patch = json_tokener_parse(body);
    if (!patch) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("batch/v1"));
    json_object_object_add(root, "kind", json_object_new_string("Job"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(root, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(patch);
    json_object_put(root);
    return 0;
}

int endpoint_delete_job(const char* namespace, const char* name,
                       char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "kind", json_object_new_string("Status"));
    json_object_object_add(root, "status", json_object_new_string("Success"));
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

// ==================== CRONJOB ENDPOINTS ====================

int endpoint_list_cronjobs(const char* namespace, char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("batch/v1"));
    json_object_object_add(root, "kind", json_object_new_string("CronJobList"));
    json_object_object_add(root, "items", json_object_new_array());
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_get_cronjob(const char* namespace, const char* name,
                        char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("batch/v1"));
    json_object_object_add(root, "kind", json_object_new_string("CronJob"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(root, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_create_cronjob(const char* namespace, const char* body,
                           char* response_buffer, int* response_code) {
    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    const char* json_str = json_object_to_json_string(obj);
    strcpy(response_buffer, json_str);
    *response_code = 201;
    json_object_put(obj);
    return 0;
}

int endpoint_update_cronjob(const char* namespace, const char* name, const char* body,
                           char* response_buffer, int* response_code) {
    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    const char* json_str = json_object_to_json_string(obj);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(obj);
    return 0;
}

int endpoint_patch_cronjob(const char* namespace, const char* name, const char* body,
                          const char* content_type, char* response_buffer, int* response_code) {
    json_object* patch = json_tokener_parse(body);
    if (!patch) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("batch/v1"));
    json_object_object_add(root, "kind", json_object_new_string("CronJob"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(root, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(patch);
    json_object_put(root);
    return 0;
}

int endpoint_delete_cronjob(const char* namespace, const char* name,
                           char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "kind", json_object_new_string("Status"));
    json_object_object_add(root, "status", json_object_new_string("Success"));
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

// ==================== NAMESPACE ENDPOINTS ====================

int endpoint_list_namespaces(char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("NamespaceList"));
    
    json_object* items = json_object_new_array();
    
    // Add default namespace
    json_object* default_ns = json_object_new_object();
    json_object_object_add(default_ns, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(default_ns, "kind", json_object_new_string("Namespace"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string("default"));
    json_object_object_add(default_ns, "metadata", metadata);
    json_object_array_add(items, default_ns);
    
    json_object_object_add(root, "items", items);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_get_namespace(const char* name, char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("Namespace"));
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(root, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_create_namespace(const char* body, char* response_buffer, int* response_code) {
    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    const char* json_str = json_object_to_json_string(obj);
    strcpy(response_buffer, json_str);
    *response_code = 201;
    json_object_put(obj);
    return 0;
}

int endpoint_delete_namespace(const char* name, char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "kind", json_object_new_string("Status"));
    json_object_object_add(root, "status", json_object_new_string("Success"));
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

// ==================== EVENT ENDPOINTS ====================

int endpoint_list_events(const char* namespace, char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("EventList"));
    json_object_object_add(root, "items", json_object_new_array());
    
    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_create_event(const char* namespace, const char* body,
                         char* response_buffer, int* response_code) {
    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
        return -1;
    }
    
    const char* json_str = json_object_to_json_string(obj);
    strcpy(response_buffer, json_str);
    *response_code = 201;
    json_object_put(obj);
    return 0;
}

int endpoint_get_event(const char* namespace, const char* name,
                       char* response_buffer, int* response_code) {
    // Return a mock event for now
    json_object* event = json_object_new_object();
    json_object_object_add(event, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(event, "kind", json_object_new_string("Event"));
    
    json_object* metadata = json_object_new_object();
    json_object_object_add(metadata, "name", json_object_new_string(name));
    json_object_object_add(metadata, "namespace", json_object_new_string(namespace));
    json_object_object_add(event, "metadata", metadata);
    
    const char* json_str = json_object_to_json_string(event);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(event);
    return 0;
}

int endpoint_delete_event(const char* namespace, const char* name,
                          char* response_buffer, int* response_code) {
    // Return 204 No Content on delete
    strcpy(response_buffer, "");
    *response_code = 204;
    return 0;
}

// Remove finalizer from pod (called by controller after cleanup)
// If no finalizers remain, the pod is automatically deleted
int endpoint_remove_finalizer(const char* namespace, const char* pod_name,
                             const char* finalizer_name) {
    for (int i = 0; i < pod_store.count; i++) {
        if (strcmp(pod_store.pods[i]->metadata.name, pod_name) == 0 &&
            strcmp(pod_store.pods[i]->metadata.namespace, namespace) == 0) {
            
            // Find and remove the finalizer
            int found = 0;
            for (int f = 0; f < pod_store.pods[i]->metadata.num_finalizers; f++) {
                if (strcmp(pod_store.pods[i]->metadata.finalizers[f], finalizer_name) == 0) {
                    // Remove this finalizer
                    free(pod_store.pods[i]->metadata.finalizers[f]);
                    
                    // Shift remaining finalizers
                    for (int j = f; j < pod_store.pods[i]->metadata.num_finalizers - 1; j++) {
                        pod_store.pods[i]->metadata.finalizers[j] = pod_store.pods[i]->metadata.finalizers[j + 1];
                    }
                    pod_store.pods[i]->metadata.num_finalizers--;
                    found = 1;
                    break;
                }
            }
            
            if (!found) {
                fprintf(stderr, "[ENDPOINTS] Finalizer not found: %s/%s ← %s\n", 
                        namespace, pod_name, finalizer_name);
                return -1;
            }
            
            // If no finalizers remain, delete the pod
            if (pod_store.pods[i]->metadata.num_finalizers == 0) {
                fprintf(stderr, "[ENDPOINTS] All finalizers removed, deleting pod: %s/%s\n", 
                        namespace, pod_name);
                
                // Free pod memory and remove from store
                free(pod_store.pods[i]->metadata.name);
                free(pod_store.pods[i]->metadata.namespace);
                free(pod_store.pods[i]);
                
                for (int j = i; j < pod_store.count - 1; j++) {
                    pod_store.pods[j] = pod_store.pods[j + 1];
                }
                pod_store.count--;
            } else {
                fprintf(stderr, "[ENDPOINTS] Finalizer removed: %s/%s ← %s (%d remaining)\n", 
                        namespace, pod_name, finalizer_name, pod_store.pods[i]->metadata.num_finalizers);
            }
            
            return 0;
        }
    }
    
    fprintf(stderr, "[ENDPOINTS] Pod not found for finalizer removal: %s/%s\n", namespace, pod_name);
    return -1;
}