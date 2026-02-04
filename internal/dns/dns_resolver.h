/*
 * dns_resolver.h
 * 
 * DNS resolution for Kubernetes cluster
 * 
 * Features:
 *   - Service DNS names (service-name.namespace.svc.cluster.local)
 *   - Pod DNS names (pod-ip.namespace.pod.cluster.local)
 *   - Headless service support
 *   - Cluster DNS IP configuration
 *   - Forward and reverse lookups
 * 
 * Kubernetes v1.28 Conformance:
 *   - Implements cluster DNS service discovery
 *   - Service DNS pointing to service IP
 *   - Pod DNS pointing to pod IP
 *   - Headless services return all pod IPs
 *   - Default cluster domain: cluster.local
 */

#ifndef SIRAH_DNS_RESOLVER_H
#define SIRAH_DNS_RESOLVER_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/**
 * DNS record types
 */
typedef enum {
    DNS_RECORD_A,           // IPv4 address
    DNS_RECORD_CNAME,       // Canonical name (service)
    DNS_RECORD_PTR,         // Reverse DNS
    DNS_RECORD_SRV          // Service record
} dns_record_type_t;

/**
 * DNS record entry
 */
typedef struct {
    char *fqdn;             // Fully qualified domain name
    dns_record_type_t type;
    char *value;            // IP for A record, name for CNAME
    uint32_t ttl;           // Time to live (default 30s)
    char *service_name;     // Associated service
    char *pod_name;         // Associated pod
    char *namespace;
} dns_record_t;

/**
 * DNS resolver instance
 */
typedef struct {
    char *cluster_domain;   // e.g., "cluster.local"
    char *cluster_dns_ip;   // Cluster DNS service IP
    
    dns_record_t *records;
    uint32_t record_count;
    uint32_t record_capacity;
    
    pthread_mutex_t mutex;
} dns_resolver_t;

/**
 * Service DNS entry
 */
typedef struct {
    char *service_fqdn;     // service-name.namespace.svc.cluster.local
    char *service_ip;       // Cluster IP
    char **pod_ips;         // For headless services
    uint32_t pod_ip_count;
    bool is_headless;       // Headless service returns all pod IPs
} service_dns_entry_t;

/**
 * Pod DNS entry
 */
typedef struct {
    char *pod_fqdn;         // pod-ip.namespace.pod.cluster.local
    char *pod_ip;
    char *pod_name;
    char *namespace;
} pod_dns_entry_t;

/**
 * Create DNS resolver
 * 
 * @param cluster_domain Cluster domain (e.g., "cluster.local")
 * @param cluster_dns_ip Cluster DNS service IP (e.g., "10.96.0.10")
 * @return DNS resolver instance or NULL
 */
dns_resolver_t* dns_resolver_create(const char *cluster_domain, const char *cluster_dns_ip);

/**
 * Free DNS resolver
 */
void dns_resolver_free(dns_resolver_t *resolver);

/**
 * Register service DNS entry
 * 
 * Creates DNS record for service
 * Service FQDN: service-name.namespace.svc.cluster.local → service IP
 * 
 * @param resolver DNS resolver
 * @param service_name Service name
 * @param namespace Namespace
 * @param service_ip Cluster IP of service
 * @param pod_ips Array of pod IPs (for headless service)
 * @param pod_ip_count Number of pod IPs
 * @param is_headless true for headless service
 * @return true on success
 */
bool dns_resolver_register_service(dns_resolver_t *resolver,
                                    const char *service_name,
                                    const char *namespace,
                                    const char *service_ip,
                                    char **pod_ips,
                                    uint32_t pod_ip_count,
                                    bool is_headless);

/**
 * Unregister service DNS entry
 * 
 * Removes DNS record when service is deleted
 * 
 * @param resolver DNS resolver
 * @param service_name Service name
 * @param namespace Namespace
 * @return true on success
 */
bool dns_resolver_unregister_service(dns_resolver_t *resolver,
                                      const char *service_name,
                                      const char *namespace);

/**
 * Register pod DNS entry
 * 
 * Creates DNS record for pod
 * Pod FQDN: pod-ip-replaced-with-dashes.namespace.pod.cluster.local → pod IP
 * Example: 10-244-1-10.default.pod.cluster.local → 10.244.1.10
 * 
 * @param resolver DNS resolver
 * @param pod_name Pod name
 * @param namespace Namespace
 * @param pod_ip Pod IP address
 * @return true on success
 */
bool dns_resolver_register_pod(dns_resolver_t *resolver,
                                const char *pod_name,
                                const char *namespace,
                                const char *pod_ip);

/**
 * Unregister pod DNS entry
 * 
 * Removes DNS record when pod is deleted
 * 
 * @param resolver DNS resolver
 * @param pod_name Pod name
 * @param namespace Namespace
 * @return true on success
 */
bool dns_resolver_unregister_pod(dns_resolver_t *resolver,
                                  const char *pod_name,
                                  const char *namespace);

/**
 * Resolve service DNS name to IP
 * 
 * Looks up service FQDN in DNS records
 * For regular service: returns service IP
 * For headless service: returns all pod IPs
 * 
 * @param resolver DNS resolver
 * @param service_name Service name
 * @param namespace Namespace
 * @param out_ip Output IP address(es)
 * @param out_count Number of IPs returned
 * @param out_is_headless true if headless service
 * @return true on success
 */
bool dns_resolver_lookup_service(dns_resolver_t *resolver,
                                  const char *service_name,
                                  const char *namespace,
                                  char **out_ip,
                                  uint32_t *out_count,
                                  bool *out_is_headless);

/**
 * Resolve pod DNS name to IP
 * 
 * Looks up pod FQDN in DNS records
 * 
 * @param resolver DNS resolver
 * @param pod_name Pod name
 * @param namespace Namespace
 * @param out_ip Output IP address
 * @return true on success
 */
bool dns_resolver_lookup_pod(dns_resolver_t *resolver,
                              const char *pod_name,
                              const char *namespace,
                              char **out_ip);

/**
 * Reverse DNS lookup (IP to hostname)
 * 
 * @param resolver DNS resolver
 * @param ip_address IP address
 * @param out_hostname Output hostname/FQDN
 * @return true on success
 */
bool dns_resolver_reverse_lookup(dns_resolver_t *resolver,
                                  const char *ip_address,
                                  char **out_hostname);

/**
 * Generate service FQDN
 * 
 * Example: "my-service.default.svc.cluster.local"
 * 
 * @param service_name Service name
 * @param namespace Namespace
 * @param cluster_domain Cluster domain
 * @return FQDN string (caller must free)
 */
char* dns_resolver_generate_service_fqdn(const char *service_name,
                                          const char *namespace,
                                          const char *cluster_domain);

/**
 * Generate pod FQDN
 * 
 * Example: "10-244-1-10.default.pod.cluster.local"
 * 
 * @param pod_name Pod name
 * @param pod_ip Pod IP address
 * @param namespace Namespace
 * @param cluster_domain Cluster domain
 * @return FQDN string (caller must free)
 */
char* dns_resolver_generate_pod_fqdn(const char *pod_name,
                                      const char *pod_ip,
                                      const char *namespace,
                                      const char *cluster_domain);

/**
 * Update service endpoints
 * 
 * Called when service endpoints change (pods added/removed)
 * 
 * @param resolver DNS resolver
 * @param service_name Service name
 * @param namespace Namespace
 * @param pod_ips Updated array of pod IPs
 * @param pod_ip_count Number of pods
 * @return true on success
 */
bool dns_resolver_update_service_endpoints(dns_resolver_t *resolver,
                                            const char *service_name,
                                            const char *namespace,
                                            char **pod_ips,
                                            uint32_t pod_ip_count);

/**
 * Get all DNS records for debugging/inspection
 * 
 * @param resolver DNS resolver
 * @param out_count Number of records
 * @return Array of DNS records (caller should not free)
 */
dns_record_t* dns_resolver_get_all_records(dns_resolver_t *resolver, uint32_t *out_count);

/**
 * Get DNS statistics
 * 
 * @param resolver DNS resolver
 * @param out_services Number of service records
 * @param out_pods Number of pod records
 * @param out_total Total DNS records
 */
void dns_resolver_get_stats(dns_resolver_t *resolver,
                            uint32_t *out_services,
                            uint32_t *out_pods,
                            uint32_t *out_total);

#endif // SIRAH_DNS_RESOLVER_H
