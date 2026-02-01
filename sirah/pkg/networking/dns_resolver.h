#ifndef SIRAH_DNS_RESOLVER_H
#define SIRAH_DNS_RESOLVER_H

#include "../types/service.h"

// DNS cache entry
typedef struct {
    char service_name[256];    // e.g., "redis"
    char namespace[64];        // e.g., "default"
    char cluster_ip[64];       // e.g., "10.96.0.1"
    time_t expires;            // Cache expiration time
    int ttl;                   // Time to live in seconds
} dns_cache_entry_t;

// DNS resolver
typedef struct {
    dns_cache_entry_t cache[1000];  // Service DNS cache
    int cache_count;
    
    char cluster_domain[256];   // e.g., "cluster.local"
    int default_ttl;            // Default cache TTL (300s)
} dns_resolver_t;

// Operations
dns_resolver_t* dns_resolver_new();
void dns_resolver_free(dns_resolver_t* resolver);

// Service name to cluster IP resolution
// e.g., "redis.default.svc.cluster.local" -> "10.96.0.1"
char* dns_resolver_lookup(dns_resolver_t* resolver, const char* service_name, 
                          const char* namespace);

// Reverse lookup (IP to service name)
char* dns_resolver_reverse_lookup(dns_resolver_t* resolver, const char* cluster_ip);

// Update DNS cache with new service
int dns_resolver_update_service(dns_resolver_t* resolver, k8s_service_t* service);

// Remove service from DNS cache
int dns_resolver_remove_service(dns_resolver_t* resolver, const char* service_name,
                                const char* namespace);

// Get all services in namespace for DNS
k8s_service_t** dns_resolver_list_services(dns_resolver_t* resolver, const char* namespace,
                                          int* count);

// Flush cache
void dns_resolver_flush_cache(dns_resolver_t* resolver);

#endif
