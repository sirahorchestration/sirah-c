#ifndef SIRAH_SERVICE_REGISTRY_H
#define SIRAH_SERVICE_REGISTRY_H

#include "../../pkg/types/service.h"
#include "../../pkg/networking/dns_resolver.h"
#include "../../pkg/networking/load_balancer.h"

// Service registry for managing services, discovery, and load balancing
typedef struct {
    k8s_service_t** services;
    int service_count;
    
    load_balancer_t** load_balancers;
    int lb_count;
    
    dns_resolver_t* dns_resolver;
} service_registry_t;

// Operations
service_registry_t* service_registry_new();
void service_registry_free(service_registry_t* registry);

// Register/unregister services
int service_registry_add_service(service_registry_t* registry, k8s_service_t* service);
int service_registry_remove_service(service_registry_t* registry, const char* name,
                                    const char* namespace);

// Get service by name and namespace
k8s_service_t* service_registry_get(service_registry_t* registry, const char* name,
                                   const char* namespace);

// Add endpoint to service load balancer
int service_registry_add_endpoint(service_registry_t* registry, const char* service_name,
                                  const char* namespace, const char* pod_ip,
                                  const char* pod_name);

// Remove endpoint from service load balancer
int service_registry_remove_endpoint(service_registry_t* registry, const char* service_name,
                                     const char* namespace, const char* pod_ip);

// Select endpoint for load balancing
k8s_endpoint_t* service_registry_select_endpoint(service_registry_t* registry,
                                                const char* service_name,
                                                const char* namespace,
                                                const char* client_ip);

// Service discovery by name
char* service_registry_discover_service(service_registry_t* registry, const char* service_name,
                                       const char* namespace);

// List all services in namespace
k8s_service_t** service_registry_list_services(service_registry_t* registry,
                                              const char* namespace, int* count);

#endif
