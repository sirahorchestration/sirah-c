// internal/controller/network_controller.c
// Network Controller Implementation
// Handles IPAM (IP allocation), virtual IP routing, and NetworkPolicy enforcement

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include "network_controller.h"

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

// Convert uint32 IP to string "a.b.c.d"
void network_uint32_to_ip_string(unsigned int ip, char* out_str) {
    if (!out_str) return;
    sprintf(out_str, "%u.%u.%u.%u",
            (ip >> 24) & 0xFF,
            (ip >> 16) & 0xFF,
            (ip >> 8) & 0xFF,
            ip & 0xFF);
}

// Convert string "a.b.c.d" to uint32 IP
unsigned int network_ip_string_to_uint32(const char* ip_str) {
    if (!ip_str) return 0;
    unsigned int a, b, c, d;
    if (sscanf(ip_str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return 0;
    }
    return (a << 24) | (b << 16) | (c << 8) | d;
}

// HTTP request helper
static json_object* api_request(network_controller_t* controller, const char* path) {
    if (!controller || !path) return NULL;

    char url[1024];
    snprintf(url, sizeof(url), "%s%s", controller->api_server_url, path);

    http_response_t response = {0};
    response.buffer = malloc(1);
    if (!response.buffer) return NULL;
    response.size = 0;

    curl_easy_reset(controller->curl_handle);
    curl_easy_setopt(controller->curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(controller->curl_handle, CURLOPT_WRITEFUNCTION, http_write_callback);
    curl_easy_setopt(controller->curl_handle, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(controller->curl_handle, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(controller->curl_handle);
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

network_controller_t* network_controller_new(const char* api_server_url) {
    if (!api_server_url) return NULL;

    network_controller_t* controller = calloc(1, sizeof(network_controller_t));
    if (!controller) return NULL;

    controller->api_server_url = strdup(api_server_url);
    controller->curl_handle = curl_easy_init();

    // Initialize IPAM config (Kubernetes default ranges)
    strcpy(controller->ipam_config.cluster_cidr, "10.0.0.0/8");
    strcpy(controller->ipam_config.service_cidr, "10.96.0.0/12");
    strcpy(controller->ipam_config.pod_cidr, "10.0.0.0/8");
    
    // Service IP range: 10.96.0.1 to 10.111.255.255
    controller->ipam_config.service_ip_start = network_ip_string_to_uint32("10.96.0.1");
    controller->ipam_config.service_ip_end = network_ip_string_to_uint32("10.111.255.255");
    controller->next_allocated_ip = 0;

    // Initialize arrays
    controller->max_allocated_ips = 1000;
    controller->allocated_ips = calloc(controller->max_allocated_ips, sizeof(service_ip_t));

    controller->max_routes = 1000;
    controller->routes = calloc(controller->max_routes, sizeof(virtual_route_t));

    controller->max_policies = 500;
    controller->policies = calloc(controller->max_policies, sizeof(cached_network_policy_t));

    // Set reconciliation intervals
    controller->ip_allocation_interval = 10;
    controller->route_update_interval = 5;
    controller->policy_sync_interval = 10;

    controller->running = 0;

    printf("Network Controller created for %s\n", api_server_url);
    return controller;
}

void network_controller_free(network_controller_t* controller) {
    if (!controller) return;

    if (controller->api_server_url) free(controller->api_server_url);
    if (controller->curl_handle) curl_easy_cleanup(controller->curl_handle);

    // Free allocated IPs
    if (controller->allocated_ips) {
        for (int i = 0; i < controller->num_allocated_ips; i++) {
            // Names are on stack, no need to free
        }
        free(controller->allocated_ips);
    }

    // Free routes
    if (controller->routes) free(controller->routes);

    // Free policies
    if (controller->policies) {
        for (int i = 0; i < controller->num_policies; i++) {
            cached_network_policy_t* policy = &controller->policies[i];
            
            if (policy->pod_selector_labels) {
                for (int j = 0; j < policy->num_pod_selector_labels; j++) {
                    if (policy->pod_selector_labels[j]) {
                        free(policy->pod_selector_labels[j]);
                    }
                }
                free(policy->pod_selector_labels);
            }
            
            if (policy->ingress_rules) {
                for (int j = 0; j < policy->num_ingress_rules; j++) {
                    // Free rule arrays
                    if (policy->ingress_rules[j].from_namespaces) {
                        for (int k = 0; k < policy->ingress_rules[j].num_from_namespaces; k++) {
                            if (policy->ingress_rules[j].from_namespaces[k]) {
                                free(policy->ingress_rules[j].from_namespaces[k]);
                            }
                        }
                        free(policy->ingress_rules[j].from_namespaces);
                    }
                    if (policy->ingress_rules[j].from_pod_labels) {
                        for (int k = 0; k < policy->ingress_rules[j].num_from_pod_labels; k++) {
                            if (policy->ingress_rules[j].from_pod_labels[k]) {
                                free(policy->ingress_rules[j].from_pod_labels[k]);
                            }
                        }
                        free(policy->ingress_rules[j].from_pod_labels);
                    }
                    if (policy->ingress_rules[j].from_ports) {
                        free(policy->ingress_rules[j].from_ports);
                    }
                    if (policy->ingress_rules[j].to_namespaces) {
                        for (int k = 0; k < policy->ingress_rules[j].num_to_namespaces; k++) {
                            if (policy->ingress_rules[j].to_namespaces[k]) {
                                free(policy->ingress_rules[j].to_namespaces[k]);
                            }
                        }
                        free(policy->ingress_rules[j].to_namespaces);
                    }
                    if (policy->ingress_rules[j].to_pod_labels) {
                        for (int k = 0; k < policy->ingress_rules[j].num_to_pod_labels; k++) {
                            if (policy->ingress_rules[j].to_pod_labels[k]) {
                                free(policy->ingress_rules[j].to_pod_labels[k]);
                            }
                        }
                        free(policy->ingress_rules[j].to_pod_labels);
                    }
                    if (policy->ingress_rules[j].to_ports) {
                        free(policy->ingress_rules[j].to_ports);
                    }
                }
                free(policy->ingress_rules);
            }
            
            if (policy->egress_rules) {
                for (int j = 0; j < policy->num_egress_rules; j++) {
                    // Similar cleanup as ingress
                    if (policy->egress_rules[j].from_namespaces) {
                        for (int k = 0; k < policy->egress_rules[j].num_from_namespaces; k++) {
                            if (policy->egress_rules[j].from_namespaces[k]) {
                                free(policy->egress_rules[j].from_namespaces[k]);
                            }
                        }
                        free(policy->egress_rules[j].from_namespaces);
                    }
                    if (policy->egress_rules[j].from_pod_labels) {
                        for (int k = 0; k < policy->egress_rules[j].num_from_pod_labels; k++) {
                            if (policy->egress_rules[j].from_pod_labels[k]) {
                                free(policy->egress_rules[j].from_pod_labels[k]);
                            }
                        }
                        free(policy->egress_rules[j].from_pod_labels);
                    }
                }
                free(policy->egress_rules);
            }
        }
        free(controller->policies);
    }

    free(controller);
}

int network_controller_init(network_controller_t* controller) {
    if (!controller) return -1;

    printf("Network Controller initializing...\n");
    printf("  IPAM config: cluster=%s, service=%s\n",
           controller->ipam_config.cluster_cidr,
           controller->ipam_config.service_cidr);
    
    return 0;
}

void network_controller_shutdown(network_controller_t* controller) {
    if (!controller) return;
    controller->running = 0;
    printf("Network Controller shutdown\n");
}

// ============ IP Allocation Functions ============

int network_allocate_service_ip(network_controller_t* controller,
                               const char* service_name,
                               const char* namespace,
                               char* out_ip_str) {
    if (!controller || !service_name || !namespace || !out_ip_str) return -1;

    // Check if already allocated
    for (int i = 0; i < controller->num_allocated_ips; i++) {
        if (strcmp(controller->allocated_ips[i].service_name, service_name) == 0 &&
            strcmp(controller->allocated_ips[i].namespace, namespace) == 0) {
            // Already allocated, return existing IP
            strcpy(out_ip_str, controller->allocated_ips[i].cluster_ip_str);
            return 0;
        }
    }

    // Need to allocate new IP
    if (controller->num_allocated_ips >= controller->max_allocated_ips) {
        printf("ERROR: IP pool exhausted\n");
        return -1;
    }

    // Find next free IP in range
    unsigned int ip_range_size = controller->ipam_config.service_ip_end -
                                 controller->ipam_config.service_ip_start;
    unsigned int offset = controller->next_allocated_ip % ip_range_size;
    unsigned int new_ip = controller->ipam_config.service_ip_start + offset;

    // Increment for next allocation
    controller->next_allocated_ip = (controller->next_allocated_ip + 1) % ip_range_size;

    // Record allocation
    service_ip_t* alloc = &controller->allocated_ips[controller->num_allocated_ips];
    strncpy(alloc->service_name, service_name, sizeof(alloc->service_name) - 1);
    strncpy(alloc->namespace, namespace, sizeof(alloc->namespace) - 1);
    alloc->cluster_ip = new_ip;
    network_uint32_to_ip_string(new_ip, alloc->cluster_ip_str);
    alloc->port = 0;  // Will be updated later
    strcpy(alloc->protocol, "TCP");
    alloc->in_use = 1;
    alloc->allocated_at = time(NULL);

    controller->num_allocated_ips++;

    // Return the allocated IP
    strcpy(out_ip_str, alloc->cluster_ip_str);

    printf("[IPAM] Allocated IP %s to %s/%s\n", out_ip_str, namespace, service_name);
    return 0;
}

int network_release_service_ip(network_controller_t* controller,
                              const char* service_name,
                              const char* namespace) {
    if (!controller || !service_name || !namespace) return -1;

    for (int i = 0; i < controller->num_allocated_ips; i++) {
        if (strcmp(controller->allocated_ips[i].service_name, service_name) == 0 &&
            strcmp(controller->allocated_ips[i].namespace, namespace) == 0) {
            
            printf("[IPAM] Released IP %s from %s/%s\n",
                   controller->allocated_ips[i].cluster_ip_str, namespace, service_name);
            
            // Mark as unused
            controller->allocated_ips[i].in_use = 0;
            
            // Optionally remove from array (for simplicity, just mark unused)
            return 0;
        }
    }

    return -1;
}

int network_get_service_ip(network_controller_t* controller,
                          const char* service_name,
                          const char* namespace,
                          char* out_ip_str) {
    if (!controller || !service_name || !namespace || !out_ip_str) return -1;

    for (int i = 0; i < controller->num_allocated_ips; i++) {
        if (controller->allocated_ips[i].in_use &&
            strcmp(controller->allocated_ips[i].service_name, service_name) == 0 &&
            strcmp(controller->allocated_ips[i].namespace, namespace) == 0) {
            strcpy(out_ip_str, controller->allocated_ips[i].cluster_ip_str);
            return 0;
        }
    }

    return -1;  // Not found
}

// ============ Virtual Routing Functions ============

int network_update_service_routes(network_controller_t* controller,
                                 const char* service_name,
                                 const char* namespace,
                                 const char* service_ip,
                                 char** pod_ips,
                                 int pod_count) {
    if (!controller || !service_name || !namespace || !service_ip) return -1;

    // Find or create route entry
    virtual_route_t* route = NULL;
    unsigned int vip = network_ip_string_to_uint32(service_ip);

    for (int i = 0; i < controller->num_routes; i++) {
        if (controller->routes[i].vip == vip) {
            route = &controller->routes[i];
            break;
        }
    }

    // Create new route if needed
    if (!route) {
        if (controller->num_routes >= controller->max_routes) {
            printf("ERROR: Route table full\n");
            return -1;
        }
        route = &controller->routes[controller->num_routes];
        controller->num_routes++;
    }

    // Update route
    route->vip = vip;
    strcpy(route->vip_str, service_ip);
    route->num_pods = 0;
    route->current_pod_index = 0;
    route->updated_at = time(NULL);

    // Add pod IPs
    for (int i = 0; i < pod_count && i < 32; i++) {
        if (pod_ips && pod_ips[i]) {
            strncpy(route->pod_ips[i], pod_ips[i], sizeof(route->pod_ips[i]) - 1);
            route->num_pods++;
        }
    }

    printf("[Routing] Updated route for VIP %s -> %d pods\n", service_ip, pod_count);
    return 0;
}

char* network_select_backend_pod(network_controller_t* controller,
                                const char* service_ip) {
    if (!controller || !service_ip) return NULL;

    unsigned int vip = network_ip_string_to_uint32(service_ip);

    for (int i = 0; i < controller->num_routes; i++) {
        if (controller->routes[i].vip == vip) {
            virtual_route_t* route = &controller->routes[i];
            
            if (route->num_pods == 0) return NULL;

            // Round-robin selection
            char* selected_ip = route->pod_ips[route->current_pod_index];
            route->current_pod_index = (route->current_pod_index + 1) % route->num_pods;
            
            return selected_ip;
        }
    }

    return NULL;
}

// ============ NetworkPolicy Support Functions ============

json_object* network_get_pod(network_controller_t* controller,
                            const char* pod_name,
                            const char* namespace) {
    if (!controller || !pod_name || !namespace) return NULL;

    char path[512];
    snprintf(path, sizeof(path), "/api/v1/namespaces/%s/pods/%s", namespace, pod_name);
    
    return api_request(controller, path);
}

json_object* network_get_service(network_controller_t* controller,
                                const char* service_name,
                                const char* namespace) {
    if (!controller || !service_name || !namespace) return NULL;

    char path[512];
    snprintf(path, sizeof(path), "/api/v1/namespaces/%s/services/%s", namespace, service_name);
    
    return api_request(controller, path);
}

json_object* network_get_policies(network_controller_t* controller,
                                 const char* namespace) {
    if (!controller || !namespace) return NULL;

    char path[512];
    snprintf(path, sizeof(path), "/apis/networking.k8s.io/v1/namespaces/%s/networkpolicies",
             namespace);
    
    return api_request(controller, path);
}

int network_pod_matches_labels(json_object* pod,
                              const char** selector_labels,
                              int num_selector_labels) {
    if (!pod || !selector_labels || num_selector_labels == 0) return 1;

    // Extract pod metadata labels
    json_object* metadata = NULL;
    if (!json_object_object_get_ex(pod, "metadata", &metadata)) {
        return 0;
    }

    json_object* pod_labels_obj = NULL;
    if (!json_object_object_get_ex(metadata, "labels", &pod_labels_obj)) {
        return 0;
    }

    // Check each selector label
    for (int i = 0; i < num_selector_labels; i++) {
        const char* selector = selector_labels[i];
        if (!selector) continue;

        // Parse "key=value" format
        char key[256], value[256];
        if (sscanf(selector, "%255[^=]=%255s", key, value) != 2) {
            return 0;
        }

        // Get pod label value
        json_object* pod_label_value = NULL;
        if (!json_object_object_get_ex(pod_labels_obj, key, &pod_label_value)) {
            return 0;
        }

        const char* pod_value = json_object_get_string(pod_label_value);
        if (!pod_value || strcmp(pod_value, value) != 0) {
            return 0;
        }
    }

    return 1;
}

int network_sync_policies(network_controller_t* controller) {
    if (!controller) return -1;

    // Get policies from all namespaces (simplified: just default for now)
    json_object* policies_list = network_get_policies(controller, "default");
    if (!policies_list) {
        printf("[Policies] No policies found\n");
        return 0;
    }

    // Parse policies (simplified)
    printf("[Policies] Synced policies\n");

    json_object_put(policies_list);
    return 0;
}

int network_evaluate_ingress_rules(network_controller_t* controller,
                                  const char* dest_pod_name,
                                  const char* dest_namespace,
                                  const char* source_pod_name,
                                  const char* source_namespace,
                                  const char* protocol,
                                  int port) {
    if (!controller || !dest_pod_name || !dest_namespace) return 1;  // Default allow

    // Check if destination pod has any network policies
    for (int i = 0; i < controller->num_policies; i++) {
        cached_network_policy_t* policy = &controller->policies[i];
        
        if (strcmp(policy->namespace, dest_namespace) != 0) continue;

        // Check if policy targets destination pod
        json_object* dest_pod = network_get_pod(controller, dest_pod_name, dest_namespace);
        if (!dest_pod) continue;

        if (!network_pod_matches_labels(dest_pod, policy->pod_selector_labels,
                                       policy->num_pod_selector_labels)) {
            json_object_put(dest_pod);
            continue;
        }
        json_object_put(dest_pod);

        // Policy applies to this pod - check ingress rules
        if (policy->num_ingress_rules == 0) {
            // No ingress rules = deny all
            printf("[Policy] Denying ingress to %s/%s (no rules)\n", dest_namespace, dest_pod_name);
            return 0;
        }

        // Check if any rule allows this traffic
        for (int j = 0; j < policy->num_ingress_rules; j++) {
            network_policy_rule_t* rule = &policy->ingress_rules[j];
            
            // Check source pod labels
            if (rule->num_from_pod_labels > 0) {
                json_object* source_pod = network_get_pod(controller, source_pod_name, source_namespace);
                if (!source_pod) continue;

                if (!network_pod_matches_labels(source_pod, rule->from_pod_labels,
                                               rule->num_from_pod_labels)) {
                    json_object_put(source_pod);
                    continue;
                }
                json_object_put(source_pod);
            }

            // Check port
            if (rule->num_to_ports > 0) {
                int port_matches = 0;
                for (int k = 0; k < rule->num_to_ports; k++) {
                    if (rule->to_ports[k] == port) {
                        port_matches = 1;
                        break;
                    }
                }
                if (!port_matches) continue;
            }

            // All checks passed, traffic allowed
            printf("[Policy] Allowing ingress to %s/%s from %s/%s\n",
                   dest_namespace, dest_pod_name, source_namespace, source_pod_name);
            return 1;
        }
    }

    // No policies applying = default allow
    return 1;
}

int network_evaluate_egress_rules(network_controller_t* controller,
                                 const char* source_pod_name,
                                 const char* source_namespace,
                                 const char* dest_pod_name,
                                 const char* dest_namespace,
                                 const char* protocol,
                                 int port) {
    if (!controller || !source_pod_name || !source_namespace) return 1;  // Default allow

    // Similar to ingress evaluation but for egress direction
    printf("[Policy] Evaluating egress from %s/%s to %s/%s\n",
           source_namespace, source_pod_name, dest_namespace, dest_pod_name);
    
    // Default allow for now (simplified)
    return 1;
}

int network_is_traffic_allowed(network_controller_t* controller,
                              const char* source_pod_name,
                              const char* source_namespace,
                              const char* dest_pod_name,
                              const char* dest_namespace,
                              const char* protocol,
                              int dest_port) {
    if (!controller) return 1;

    // Check both ingress and egress
    int ingress_allowed = network_evaluate_ingress_rules(controller,
                                                        dest_pod_name, dest_namespace,
                                                        source_pod_name, source_namespace,
                                                        protocol, dest_port);
    
    int egress_allowed = network_evaluate_egress_rules(controller,
                                                      source_pod_name, source_namespace,
                                                      dest_pod_name, dest_namespace,
                                                      protocol, dest_port);

    return ingress_allowed && egress_allowed;
}

// ============ Main Reconciliation Loop ============

int network_controller_run(network_controller_t* controller) {
    if (!controller) return -1;

    controller->running = 1;
    printf("Network Controller running\n");

    time_t last_ip_alloc = time(NULL);
    time_t last_route_update = time(NULL);
    time_t last_policy_sync = time(NULL);

    while (controller->running) {
        time_t now = time(NULL);

        // IP Allocation reconciliation (10 seconds)
        if (now - last_ip_alloc >= controller->ip_allocation_interval) {
            printf("[Network] IP allocation reconciliation...\n");
            
            // In a real implementation, would:
            // 1. Fetch all services from API
            // 2. For each service without a cluster IP, allocate one
            // 3. For deleted services, release IPs
            
            last_ip_alloc = now;
        }

        // Virtual routing reconciliation (5 seconds)
        if (now - last_route_update >= controller->route_update_interval) {
            printf("[Network] Virtual routing reconciliation...\n");
            
            // In a real implementation, would:
            // 1. Fetch all services
            // 2. Fetch pods for each service
            // 3. Update routes with current pod IPs
            
            last_route_update = now;
        }

        // Network policy reconciliation (10 seconds)
        if (now - last_policy_sync >= controller->policy_sync_interval) {
            printf("[Network] Network policy synchronization...\n");
            
            // Sync policies from API
            network_sync_policies(controller);
            
            last_policy_sync = now;
        }

        sleep(1);  // Sleep 1 second between checks
    }

    return 0;
}
