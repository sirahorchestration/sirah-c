/*
 * dns_resolver.c
 * 
 * DNS resolver implementation
 */

#include "dns_resolver.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

/**
 * Create DNS resolver
 */
dns_resolver_t* dns_resolver_create(const char *cluster_domain, const char *cluster_dns_ip) {
    if (!cluster_domain || !cluster_dns_ip) {
        return NULL;
    }
    
    dns_resolver_t *resolver = malloc(sizeof(dns_resolver_t));
    if (!resolver) {
        return NULL;
    }
    
    memset(resolver, 0, sizeof(dns_resolver_t));
    
    resolver->cluster_domain = strdup(cluster_domain);
    resolver->cluster_dns_ip = strdup(cluster_dns_ip);
    
    if (!resolver->cluster_domain || !resolver->cluster_dns_ip) {
        free(resolver->cluster_domain);
        free(resolver->cluster_dns_ip);
        free(resolver);
        return NULL;
    }
    
    // Initialize DNS records array
    resolver->record_capacity = 128;
    resolver->records = malloc(sizeof(dns_record_t) * resolver->record_capacity);
    if (!resolver->records) {
        free(resolver->cluster_domain);
        free(resolver->cluster_dns_ip);
        free(resolver);
        return NULL;
    }
    
    pthread_mutex_init(&resolver->mutex, NULL);
    
    return resolver;
}

/**
 * Free DNS resolver
 */
void dns_resolver_free(dns_resolver_t *resolver) {
    if (!resolver) return;
    
    // Free records
    for (uint32_t i = 0; i < resolver->record_count; i++) {
        free(resolver->records[i].fqdn);
        free(resolver->records[i].value);
        free(resolver->records[i].service_name);
        free(resolver->records[i].pod_name);
        free(resolver->records[i].namespace);
    }
    free(resolver->records);
    
    free(resolver->cluster_domain);
    free(resolver->cluster_dns_ip);
    
    pthread_mutex_destroy(&resolver->mutex);
    free(resolver);
}

/**
 * Generate service FQDN
 */
char* dns_resolver_generate_service_fqdn(const char *service_name,
                                          const char *namespace,
                                          const char *cluster_domain) {
    if (!service_name || !namespace || !cluster_domain) {
        return NULL;
    }
    
    char fqdn[256];
    snprintf(fqdn, sizeof(fqdn), "%s.%s.svc.%s",
             service_name, namespace, cluster_domain);
    
    return strdup(fqdn);
}

/**
 * Generate pod FQDN
 */
char* dns_resolver_generate_pod_fqdn(const char *pod_name,
                                      const char *pod_ip,
                                      const char *namespace,
                                      const char *cluster_domain) {
    if (!pod_name || !pod_ip || !namespace || !cluster_domain) {
        return NULL;
    }
    
    // Replace dots with dashes in IP
    char ip_dashed[32];
    strcpy(ip_dashed, pod_ip);
    for (char *p = ip_dashed; *p; p++) {
        if (*p == '.') *p = '-';
    }
    
    char fqdn[256];
    snprintf(fqdn, sizeof(fqdn), "%s.%s.pod.%s",
             ip_dashed, namespace, cluster_domain);
    
    return strdup(fqdn);
}

/**
 * Register service DNS entry
 */
bool dns_resolver_register_service(dns_resolver_t *resolver,
                                    const char *service_name,
                                    const char *namespace,
                                    const char *service_ip,
                                    char **pod_ips,
                                    uint32_t pod_ip_count,
                                    bool is_headless) {
    if (!resolver || !service_name || !namespace || !service_ip) {
        return false;
    }
    
    pthread_mutex_lock(&resolver->mutex);
    
    // Expand array if needed
    if (resolver->record_count >= resolver->record_capacity) {
        resolver->record_capacity *= 2;
        dns_record_t *new_records = realloc(resolver->records,
                                             sizeof(dns_record_t) * resolver->record_capacity);
        if (!new_records) {
            pthread_mutex_unlock(&resolver->mutex);
            return false;
        }
        resolver->records = new_records;
    }
    
    // Create DNS record
    dns_record_t *record = &resolver->records[resolver->record_count];
    memset(record, 0, sizeof(dns_record_t));
    
    record->fqdn = dns_resolver_generate_service_fqdn(service_name, namespace, resolver->cluster_domain);
    record->type = DNS_RECORD_A;
    record->value = strdup(service_ip);
    record->ttl = 30;
    record->service_name = strdup(service_name);
    record->namespace = strdup(namespace);
    
    if (!record->fqdn || !record->value || !record->service_name || !record->namespace) {
        free(record->fqdn);
        free(record->value);
        free(record->service_name);
        free(record->namespace);
        pthread_mutex_unlock(&resolver->mutex);
        return false;
    }
    
    resolver->record_count++;
    pthread_mutex_unlock(&resolver->mutex);
    
    return true;
}

/**
 * Unregister service DNS entry
 */
bool dns_resolver_unregister_service(dns_resolver_t *resolver,
                                      const char *service_name,
                                      const char *namespace) {
    if (!resolver || !service_name || !namespace) {
        return false;
    }
    
    pthread_mutex_lock(&resolver->mutex);
    
    for (uint32_t i = 0; i < resolver->record_count; i++) {
        if (resolver->records[i].service_name &&
            strcmp(resolver->records[i].service_name, service_name) == 0 &&
            strcmp(resolver->records[i].namespace, namespace) == 0) {
            
            // Free record
            free(resolver->records[i].fqdn);
            free(resolver->records[i].value);
            free(resolver->records[i].service_name);
            free(resolver->records[i].pod_name);
            free(resolver->records[i].namespace);
            
            // Shift remaining records
            if (i < resolver->record_count - 1) {
                memmove(&resolver->records[i], &resolver->records[i + 1],
                        sizeof(dns_record_t) * (resolver->record_count - i - 1));
            }
            
            resolver->record_count--;
            pthread_mutex_unlock(&resolver->mutex);
            return true;
        }
    }
    
    pthread_mutex_unlock(&resolver->mutex);
    return false;
}

/**
 * Register pod DNS entry
 */
bool dns_resolver_register_pod(dns_resolver_t *resolver,
                                const char *pod_name,
                                const char *namespace,
                                const char *pod_ip) {
    if (!resolver || !pod_name || !namespace || !pod_ip) {
        return false;
    }
    
    pthread_mutex_lock(&resolver->mutex);
    
    // Expand array if needed
    if (resolver->record_count >= resolver->record_capacity) {
        resolver->record_capacity *= 2;
        dns_record_t *new_records = realloc(resolver->records,
                                             sizeof(dns_record_t) * resolver->record_capacity);
        if (!new_records) {
            pthread_mutex_unlock(&resolver->mutex);
            return false;
        }
        resolver->records = new_records;
    }
    
    // Create DNS record
    dns_record_t *record = &resolver->records[resolver->record_count];
    memset(record, 0, sizeof(dns_record_t));
    
    record->fqdn = dns_resolver_generate_pod_fqdn(pod_name, pod_ip, namespace, resolver->cluster_domain);
    record->type = DNS_RECORD_A;
    record->value = strdup(pod_ip);
    record->ttl = 30;
    record->pod_name = strdup(pod_name);
    record->namespace = strdup(namespace);
    
    if (!record->fqdn || !record->value || !record->pod_name || !record->namespace) {
        free(record->fqdn);
        free(record->value);
        free(record->pod_name);
        free(record->namespace);
        pthread_mutex_unlock(&resolver->mutex);
        return false;
    }
    
    resolver->record_count++;
    pthread_mutex_unlock(&resolver->mutex);
    
    return true;
}

/**
 * Unregister pod DNS entry
 */
bool dns_resolver_unregister_pod(dns_resolver_t *resolver,
                                  const char *pod_name,
                                  const char *namespace) {
    if (!resolver || !pod_name || !namespace) {
        return false;
    }
    
    pthread_mutex_lock(&resolver->mutex);
    
    for (uint32_t i = 0; i < resolver->record_count; i++) {
        if (resolver->records[i].pod_name &&
            strcmp(resolver->records[i].pod_name, pod_name) == 0 &&
            strcmp(resolver->records[i].namespace, namespace) == 0) {
            
            // Free record
            free(resolver->records[i].fqdn);
            free(resolver->records[i].value);
            free(resolver->records[i].service_name);
            free(resolver->records[i].pod_name);
            free(resolver->records[i].namespace);
            
            // Shift remaining records
            if (i < resolver->record_count - 1) {
                memmove(&resolver->records[i], &resolver->records[i + 1],
                        sizeof(dns_record_t) * (resolver->record_count - i - 1));
            }
            
            resolver->record_count--;
            pthread_mutex_unlock(&resolver->mutex);
            return true;
        }
    }
    
    pthread_mutex_unlock(&resolver->mutex);
    return false;
}

/**
 * Lookup service DNS name
 */
bool dns_resolver_lookup_service(dns_resolver_t *resolver,
                                  const char *service_name,
                                  const char *namespace,
                                  char **out_ip,
                                  uint32_t *out_count,
                                  bool *out_is_headless) {
    if (!resolver || !service_name || !namespace || !out_ip || !out_count) {
        return false;
    }
    
    pthread_mutex_lock(&resolver->mutex);
    
    for (uint32_t i = 0; i < resolver->record_count; i++) {
        if (resolver->records[i].service_name &&
            strcmp(resolver->records[i].service_name, service_name) == 0 &&
            strcmp(resolver->records[i].namespace, namespace) == 0) {
            
            *out_count = 1;
            *out_ip = malloc(sizeof(char*));
            if (*out_ip) {
                *out_ip[0] = strdup(resolver->records[i].value);
            }
            if (out_is_headless) *out_is_headless = false;
            
            pthread_mutex_unlock(&resolver->mutex);
            return true;
        }
    }
    
    pthread_mutex_unlock(&resolver->mutex);
    return false;
}

/**
 * Lookup pod DNS name
 */
bool dns_resolver_lookup_pod(dns_resolver_t *resolver,
                              const char *pod_name,
                              const char *namespace,
                              char **out_ip) {
    if (!resolver || !pod_name || !namespace || !out_ip) {
        return false;
    }
    
    pthread_mutex_lock(&resolver->mutex);
    
    for (uint32_t i = 0; i < resolver->record_count; i++) {
        if (resolver->records[i].pod_name &&
            strcmp(resolver->records[i].pod_name, pod_name) == 0 &&
            strcmp(resolver->records[i].namespace, namespace) == 0) {
            
            *out_ip = strdup(resolver->records[i].value);
            pthread_mutex_unlock(&resolver->mutex);
            return true;
        }
    }
    
    pthread_mutex_unlock(&resolver->mutex);
    return false;
}

/**
 * Reverse DNS lookup
 */
bool dns_resolver_reverse_lookup(dns_resolver_t *resolver,
                                  const char *ip_address,
                                  char **out_hostname) {
    if (!resolver || !ip_address || !out_hostname) {
        return false;
    }
    
    pthread_mutex_lock(&resolver->mutex);
    
    for (uint32_t i = 0; i < resolver->record_count; i++) {
        if (resolver->records[i].value &&
            strcmp(resolver->records[i].value, ip_address) == 0) {
            
            *out_hostname = strdup(resolver->records[i].fqdn);
            pthread_mutex_unlock(&resolver->mutex);
            return true;
        }
    }
    
    pthread_mutex_unlock(&resolver->mutex);
    return false;
}

/**
 * Update service endpoints
 */
bool dns_resolver_update_service_endpoints(dns_resolver_t *resolver,
                                            const char *service_name,
                                            const char *namespace,
                                            char **pod_ips,
                                            uint32_t pod_ip_count) {
    if (!resolver || !service_name || !namespace) {
        return false;
    }
    
    // In a real implementation, this would update the DNS records
    // for headless services to point to all pod IPs
    
    return true;
}

/**
 * Get all DNS records
 */
dns_record_t* dns_resolver_get_all_records(dns_resolver_t *resolver, uint32_t *out_count) {
    if (!resolver || !out_count) {
        return NULL;
    }
    
    pthread_mutex_lock(&resolver->mutex);
    *out_count = resolver->record_count;
    pthread_mutex_unlock(&resolver->mutex);
    
    return resolver->records;
}

/**
 * Get DNS statistics
 */
void dns_resolver_get_stats(dns_resolver_t *resolver,
                            uint32_t *out_services,
                            uint32_t *out_pods,
                            uint32_t *out_total) {
    if (!resolver) return;
    
    pthread_mutex_lock(&resolver->mutex);
    
    uint32_t services = 0, pods = 0;
    for (uint32_t i = 0; i < resolver->record_count; i++) {
        if (resolver->records[i].service_name) {
            services++;
        } else if (resolver->records[i].pod_name) {
            pods++;
        }
    }
    
    if (out_services) *out_services = services;
    if (out_pods) *out_pods = pods;
    if (out_total) *out_total = resolver->record_count;
    
    pthread_mutex_unlock(&resolver->mutex);
}
