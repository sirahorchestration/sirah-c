#include "dns_resolver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

dns_resolver_t* dns_resolver_new() {
    dns_resolver_t* resolver = (dns_resolver_t*)malloc(sizeof(dns_resolver_t));
    resolver->cache_count = 0;
    resolver->default_ttl = 300;  // 5 minute default TTL
    strcpy(resolver->cluster_domain, "cluster.local");
    
    return resolver;
}

void dns_resolver_free(dns_resolver_t* resolver) {
    if (!resolver) return;
    free(resolver);
}

// Parse service name like "redis.default.svc.cluster.local"
// Returns: service_name="redis", namespace="default"
static int parse_dns_name(const char* dns_name, char* service_name, char* namespace) {
    if (!dns_name || !service_name || !namespace) return -1;
    
    // Handle short names like "redis" (use default namespace)
    if (!strchr(dns_name, '.')) {
        strcpy(service_name, dns_name);
        strcpy(namespace, "default");
        return 0;
    }
    
    // Handle FQDN like "redis.default.svc.cluster.local"
    char tmp[256];
    strcpy(tmp, dns_name);
    
    // Remove ".svc.cluster.local" suffix if present
    char* svc_ptr = strstr(tmp, ".svc.cluster.local");
    if (svc_ptr) {
        *svc_ptr = '\0';
    }
    
    // Now we have "redis.default", split on first dot
    char* dot = strchr(tmp, '.');
    if (dot) {
        int name_len = dot - tmp;
        strncpy(service_name, tmp, name_len);
        service_name[name_len] = '\0';
        strcpy(namespace, dot + 1);
    } else {
        strcpy(service_name, tmp);
        strcpy(namespace, "default");
    }
    
    return 0;
}

// Build FQDN from service name and namespace
// e.g., "redis" + "default" -> "redis.default.svc.cluster.local"
static void build_fqdn(const char* service_name, const char* namespace, 
                       char* fqdn, const char* cluster_domain) {
    snprintf(fqdn, 256, "%s.%s.svc.%s", service_name, namespace, cluster_domain);
}

// Check if cache entry is expired
static int is_expired(dns_cache_entry_t* entry) {
    if (entry->expires == 0) return 1;  // Never cached
    return time(NULL) > entry->expires;
}

char* dns_resolver_lookup(dns_resolver_t* resolver, const char* service_name, 
                          const char* namespace) {
    if (!resolver || !service_name) return NULL;
    
    const char* ns = namespace ? namespace : "default";
    
    // Search cache
    for (int i = 0; i < resolver->cache_count; i++) {
        dns_cache_entry_t* entry = &resolver->cache[i];
        
        if (strcmp(entry->service_name, service_name) == 0 &&
            strcmp(entry->namespace, ns) == 0) {
            
            // Check if expired
            if (!is_expired(entry)) {
                // Cache hit
                return strdup(entry->cluster_ip);
            } else {
                // Expired entry - remove it
                memmove(&resolver->cache[i], &resolver->cache[i + 1],
                       sizeof(dns_cache_entry_t) * (resolver->cache_count - i - 1));
                resolver->cache_count--;
                i--;
            }
        }
    }
    
    // Cache miss
    return NULL;
}

char* dns_resolver_reverse_lookup(dns_resolver_t* resolver, const char* cluster_ip) {
    if (!resolver || !cluster_ip) return NULL;
    
    for (int i = 0; i < resolver->cache_count; i++) {
        if (strcmp(resolver->cache[i].cluster_ip, cluster_ip) == 0) {
            if (!is_expired(&resolver->cache[i])) {
                char fqdn[256];
                build_fqdn(resolver->cache[i].service_name, 
                          resolver->cache[i].namespace,
                          fqdn, resolver->cluster_domain);
                return strdup(fqdn);
            }
        }
    }
    
    return NULL;
}

int dns_resolver_update_service(dns_resolver_t* resolver, k8s_service_t* service) {
    if (!resolver || !service) return -1;
    
    // Find existing entry or create new one
    int found = -1;
    for (int i = 0; i < resolver->cache_count; i++) {
        if (strcmp(resolver->cache[i].service_name, service->metadata.name) == 0 &&
            strcmp(resolver->cache[i].namespace, service->metadata.namespace) == 0) {
            found = i;
            break;
        }
    }
    
    // Add or update cache entry
    int idx = found >= 0 ? found : resolver->cache_count;
    if (idx >= 1000) return -1;  // Cache full
    
    strcpy(resolver->cache[idx].service_name, service->metadata.name);
    strcpy(resolver->cache[idx].namespace, service->metadata.namespace);
    strcpy(resolver->cache[idx].cluster_ip, service->spec.cluster_ip);
    resolver->cache[idx].ttl = resolver->default_ttl;
    resolver->cache[idx].expires = time(NULL) + resolver->default_ttl;
    
    if (found < 0) {
        resolver->cache_count++;
    }
    
    fprintf(stderr, "[dns-resolver] Registered %s.%s -> %s\n",
            service->metadata.name, service->metadata.namespace, service->spec.cluster_ip);
    
    return 0;
}

int dns_resolver_remove_service(dns_resolver_t* resolver, const char* service_name,
                                const char* namespace) {
    if (!resolver || !service_name) return -1;
    
    for (int i = 0; i < resolver->cache_count; i++) {
        if (strcmp(resolver->cache[i].service_name, service_name) == 0 &&
            strcmp(resolver->cache[i].namespace, namespace) == 0) {
            
            // Remove by shifting
            memmove(&resolver->cache[i], &resolver->cache[i + 1],
                   sizeof(dns_cache_entry_t) * (resolver->cache_count - i - 1));
            resolver->cache_count--;
            
            fprintf(stderr, "[dns-resolver] Removed %s.%s\n", service_name, namespace);
            return 0;
        }
    }
    
    return -1;
}

k8s_service_t** dns_resolver_list_services(dns_resolver_t* resolver, const char* namespace,
                                          int* count) {
    if (!resolver || !count) return NULL;
    
    const char* ns = namespace ? namespace : "default";
    k8s_service_t** services = (k8s_service_t**)malloc(sizeof(k8s_service_t*) * 1000);
    *count = 0;
    
    for (int i = 0; i < resolver->cache_count; i++) {
        if (!namespace || strcmp(resolver->cache[i].namespace, ns) == 0) {
            if (!is_expired(&resolver->cache[i])) {
                // Would need access to actual service objects
                // For now just return the count
                (*count)++;
            }
        }
    }
    
    return services;
}

void dns_resolver_flush_cache(dns_resolver_t* resolver) {
    if (!resolver) return;
    
    resolver->cache_count = 0;
    fprintf(stderr, "[dns-resolver] Cache flushed\n");
}
