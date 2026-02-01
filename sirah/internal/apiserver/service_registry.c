#include "service_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

service_registry_t* service_registry_new() {
    service_registry_t* registry = (service_registry_t*)malloc(sizeof(service_registry_t));
    
    registry->services = (k8s_service_t**)malloc(sizeof(k8s_service_t*) * 1000);
    registry->service_count = 0;
    
    registry->load_balancers = (load_balancer_t**)malloc(sizeof(load_balancer_t*) * 1000);
    registry->lb_count = 0;
    
    registry->dns_resolver = dns_resolver_new();
    
    fprintf(stderr, "[service-registry] Initialized\n");
    return registry;
}

void service_registry_free(service_registry_t* registry) {
    if (!registry) return;
    
    // Free services
    for (int i = 0; i < registry->service_count; i++) {
        k8s_service_free(registry->services[i]);
    }
    free(registry->services);
    
    // Free load balancers
    for (int i = 0; i < registry->lb_count; i++) {
        load_balancer_free(registry->load_balancers[i]);
    }
    free(registry->load_balancers);
    
    // Free DNS resolver
    dns_resolver_free(registry->dns_resolver);
    
    free(registry);
}

int service_registry_add_service(service_registry_t* registry, k8s_service_t* service) {
    if (!registry || !service) return -1;
    if (registry->service_count >= 1000) return -1;
    
    // Check if service already exists
    for (int i = 0; i < registry->service_count; i++) {
        if (strcmp(registry->services[i]->metadata.name, service->metadata.name) == 0 &&
            strcmp(registry->services[i]->metadata.namespace, service->metadata.namespace) == 0) {
            // Update existing service
            k8s_service_free(registry->services[i]);
            registry->services[i] = service;
            
            // Update DNS
            dns_resolver_update_service(registry->dns_resolver, service);
            
            fprintf(stderr, "[service-registry] Updated service %s/%s\n",
                    service->metadata.namespace, service->metadata.name);
            return 0;
        }
    }
    
    // Add new service
    int idx = registry->service_count++;
    registry->services[idx] = service;
    
    // Register in DNS
    dns_resolver_update_service(registry->dns_resolver, service);
    
    // Create load balancer for this service
    load_balancer_t* lb = load_balancer_new(service->metadata.name,
                                            service->metadata.namespace,
                                            LB_ROUND_ROBIN);
    if (lb) {
        if (registry->lb_count < 1000) {
            registry->load_balancers[registry->lb_count++] = lb;
        } else {
            load_balancer_free(lb);
        }
    }
    
    fprintf(stderr, "[service-registry] Registered service %s/%s (IP: %s)\n",
            service->metadata.namespace, service->metadata.name,
            service->spec.cluster_ip);
    
    return 0;
}

int service_registry_remove_service(service_registry_t* registry, const char* name,
                                    const char* namespace) {
    if (!registry || !name || !namespace) return -1;
    
    // Remove service
    for (int i = 0; i < registry->service_count; i++) {
        if (strcmp(registry->services[i]->metadata.name, name) == 0 &&
            strcmp(registry->services[i]->metadata.namespace, namespace) == 0) {
            
            k8s_service_free(registry->services[i]);
            memmove(&registry->services[i], &registry->services[i + 1],
                   sizeof(k8s_service_t*) * (registry->service_count - i - 1));
            registry->service_count--;
            
            // Remove from DNS
            dns_resolver_remove_service(registry->dns_resolver, name, namespace);
            
            // Remove load balancer
            for (int j = 0; j < registry->lb_count; j++) {
                if (strcmp(registry->load_balancers[j]->service_name, name) == 0 &&
                    strcmp(registry->load_balancers[j]->namespace, namespace) == 0) {
                    load_balancer_free(registry->load_balancers[j]);
                    memmove(&registry->load_balancers[j], &registry->load_balancers[j + 1],
                           sizeof(load_balancer_t*) * (registry->lb_count - j - 1));
                    registry->lb_count--;
                    break;
                }
            }
            
            fprintf(stderr, "[service-registry] Removed service %s/%s\n", namespace, name);
            return 0;
        }
    }
    
    return -1;
}

k8s_service_t* service_registry_get(service_registry_t* registry, const char* name,
                                   const char* namespace) {
    if (!registry || !name || !namespace) return NULL;
    
    for (int i = 0; i < registry->service_count; i++) {
        if (strcmp(registry->services[i]->metadata.name, name) == 0 &&
            strcmp(registry->services[i]->metadata.namespace, namespace) == 0) {
            return registry->services[i];
        }
    }
    
    return NULL;
}

int service_registry_add_endpoint(service_registry_t* registry, const char* service_name,
                                  const char* namespace, const char* pod_ip,
                                  const char* pod_name) {
    if (!registry || !service_name || !namespace || !pod_ip || !pod_name) return -1;
    
    // Find load balancer for this service
    load_balancer_t* lb = NULL;
    for (int i = 0; i < registry->lb_count; i++) {
        if (strcmp(registry->load_balancers[i]->service_name, service_name) == 0 &&
            strcmp(registry->load_balancers[i]->namespace, namespace) == 0) {
            lb = registry->load_balancers[i];
            break;
        }
    }
    
    if (!lb) return -1;
    
    // Add endpoint to load balancer
    if (load_balancer_add_endpoint(lb, pod_ip, pod_name) == 0) {
        // Also add to service status
        k8s_service_t* svc = service_registry_get(registry, service_name, namespace);
        if (svc) {
            k8s_service_add_endpoint(svc, pod_ip, pod_name);
        }
        
        fprintf(stderr, "[service-registry] Added endpoint %s (%s) to %s/%s\n",
                pod_ip, pod_name, namespace, service_name);
        return 0;
    }
    
    return -1;
}

int service_registry_remove_endpoint(service_registry_t* registry, const char* service_name,
                                     const char* namespace, const char* pod_ip) {
    if (!registry || !service_name || !namespace || !pod_ip) return -1;
    
    // Find load balancer for this service
    load_balancer_t* lb = NULL;
    for (int i = 0; i < registry->lb_count; i++) {
        if (strcmp(registry->load_balancers[i]->service_name, service_name) == 0 &&
            strcmp(registry->load_balancers[i]->namespace, namespace) == 0) {
            lb = registry->load_balancers[i];
            break;
        }
    }
    
    if (!lb) return -1;
    
    // Remove from load balancer
    return load_balancer_remove_endpoint(lb, pod_ip);
}

k8s_endpoint_t* service_registry_select_endpoint(service_registry_t* registry,
                                                const char* service_name,
                                                const char* namespace,
                                                const char* client_ip) {
    if (!registry || !service_name || !namespace) return NULL;
    
    // Find load balancer
    load_balancer_t* lb = NULL;
    for (int i = 0; i < registry->lb_count; i++) {
        if (strcmp(registry->load_balancers[i]->service_name, service_name) == 0 &&
            strcmp(registry->load_balancers[i]->namespace, namespace) == 0) {
            lb = registry->load_balancers[i];
            break;
        }
    }
    
    if (!lb) return NULL;
    
    // Select endpoint
    return load_balancer_select_endpoint(lb, client_ip);
}

char* service_registry_discover_service(service_registry_t* registry, const char* service_name,
                                       const char* namespace) {
    if (!registry || !service_name) return NULL;
    
    const char* ns = namespace ? namespace : "default";
    
    // First try DNS resolution
    char* ip = dns_resolver_lookup(registry->dns_resolver, service_name, ns);
    if (ip) {
        return ip;
    }
    
    // Fallback to direct lookup
    k8s_service_t* svc = service_registry_get(registry, service_name, ns);
    if (svc && strlen(svc->spec.cluster_ip) > 0) {
        return strdup(svc->spec.cluster_ip);
    }
    
    return NULL;
}

k8s_service_t** service_registry_list_services(service_registry_t* registry,
                                              const char* namespace, int* count) {
    if (!registry || !count) return NULL;
    
    k8s_service_t** result = (k8s_service_t**)malloc(sizeof(k8s_service_t*) * 1000);
    *count = 0;
    
    const char* ns = namespace ? namespace : "";
    
    for (int i = 0; i < registry->service_count; i++) {
        if (strlen(ns) == 0 || strcmp(registry->services[i]->metadata.namespace, ns) == 0) {
            result[(*count)++] = registry->services[i];
        }
    }
    
    return result;
}
