/*
 * ipam_manager.h
 * 
 * IP Address Management (IPAM) for Kubernetes cluster
 * 
 * Manages:
 *   - Cluster-wide CIDR ranges
 *   - Per-node pod CIDR allocation
 *   - Service IP range (virtual IPs for services)
 *   - Pod IP allocation and deallocation
 *   - IP tracking and conflict detection
 * 
 * Kubernetes v1.28 Conformance:
 *   - Supports pod CIDR per node (typically /24)
 *   - Service CIDR for virtual IPs (typically /16)
 *   - Dynamic IP allocation on pod creation
 *   - IP release on pod deletion
 *   - No IP conflicts across cluster
 */

#ifndef SIRAH_IPAM_MANAGER_H
#define SIRAH_IPAM_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/**
 * IPAM configuration
 */
typedef struct {
    char *cluster_cidr;          // Cluster pod CIDR (e.g., "10.244.0.0/16")
    char *service_cidr;          // Service IP range (e.g., "10.96.0.0/12")
    char *node_cidr_prefix;      // Prefix for per-node allocation (e.g., "/24")
    uint32_t cluster_prefix_len; // Cluster CIDR prefix length
    uint32_t node_prefix_len;    // Per-node CIDR prefix length
    uint32_t service_prefix_len; // Service CIDR prefix length
} ipam_config_t;

/**
 * IP pool for tracking allocated IPs
 */
typedef struct {
    uint32_t *allocated_ips;     // Array of allocated IP addresses
    uint32_t ip_count;           // Number of allocated IPs
    uint32_t ip_capacity;        // Capacity of IP array
    pthread_mutex_t mutex;       // Thread safety
} ip_pool_t;

/**
 * Node allocation entry (tracks IPs per node)
 */
typedef struct {
    char *node_name;
    uint32_t node_cidr_start;    // Network byte order
    uint32_t node_cidr_end;      // Network byte order
    char *pod_cidr_str;          // e.g., "10.244.1.0/24"
    uint32_t *allocated_ips;
    uint32_t ip_count;
    uint32_t ip_capacity;
} node_allocation_t;

/**
 * IPAM Manager instance
 */
typedef struct {
    ipam_config_t config;
    
    // Cluster-wide IP tracking
    uint32_t cluster_start;      // Network byte order
    uint32_t cluster_end;
    
    // Service IP range
    uint32_t service_start;
    uint32_t service_end;
    
    // Per-node allocations
    node_allocation_t *nodes;
    uint32_t node_count;
    uint32_t node_capacity;
    
    // Global IP tracking
    ip_pool_t pod_ips;
    ip_pool_t service_ips;
    
    pthread_mutex_t mutex;
} ipam_manager_t;

/**
 * Allocated IP info
 */
typedef struct {
    uint32_t ip_address;         // Network byte order
    char *ip_string;             // Dotted decimal (e.g., "10.244.1.10")
    char *node_name;             // Assigned node
    char *pod_name;              // Associated pod
    char *pod_namespace;
    bool is_service_ip;
} allocated_ip_t;

/**
 * Create IPAM manager
 * 
 * @param config IPAM configuration
 * @return IPAM manager instance or NULL
 */
ipam_manager_t* ipam_manager_create(const ipam_config_t *config);

/**
 * Free IPAM manager
 */
void ipam_manager_free(ipam_manager_t *manager);

/**
 * Register node with IPAM
 * 
 * Allocates pod CIDR block for node
 * Called when node joins cluster
 * 
 * @param manager IPAM manager
 * @param node_name Node identifier
 * @return Allocated pod CIDR (e.g., "10.244.1.0/24") or NULL
 */
char* ipam_manager_register_node(ipam_manager_t *manager, const char *node_name);

/**
 * Unregister node from IPAM
 * 
 * Deallocates pod CIDR block for node
 * Called when node leaves cluster
 * 
 * @param manager IPAM manager
 * @param node_name Node to unregister
 * @return true on success
 */
bool ipam_manager_unregister_node(ipam_manager_t *manager, const char *node_name);

/**
 * Allocate pod IP address
 * 
 * Assigns IP from node's pod CIDR
 * Called when pod is created
 * 
 * @param manager IPAM manager
 * @param node_name Target node
 * @param pod_name Pod name
 * @param pod_namespace Pod namespace
 * @return Allocated IP address (e.g., "10.244.1.10") or NULL
 */
char* ipam_manager_allocate_pod_ip(ipam_manager_t *manager,
                                    const char *node_name,
                                    const char *pod_name,
                                    const char *pod_namespace);

/**
 * Release pod IP address
 * 
 * Deallocates IP back to pool
 * Called when pod is deleted
 * 
 * @param manager IPAM manager
 * @param ip_address IP to release
 * @return true on success
 */
bool ipam_manager_release_pod_ip(ipam_manager_t *manager, const char *ip_address);

/**
 * Allocate service IP (virtual IP)
 * 
 * Assigns IP from service CIDR
 * Called when service is created
 * 
 * @param manager IPAM manager
 * @param service_name Service name
 * @param service_namespace Service namespace
 * @return Allocated service IP (e.g., "10.96.1.10") or NULL
 */
char* ipam_manager_allocate_service_ip(ipam_manager_t *manager,
                                        const char *service_name,
                                        const char *service_namespace);

/**
 * Release service IP
 * 
 * Deallocates service IP back to pool
 * 
 * @param manager IPAM manager
 * @param ip_address IP to release
 * @return true on success
 */
bool ipam_manager_release_service_ip(ipam_manager_t *manager, const char *ip_address);

/**
 * Get node's pod CIDR
 * 
 * @param manager IPAM manager
 * @param node_name Node name
 * @return Pod CIDR (e.g., "10.244.1.0/24") or NULL
 */
char* ipam_manager_get_node_pod_cidr(ipam_manager_t *manager, const char *node_name);

/**
 * Get allocated IP info
 * 
 * @param manager IPAM manager
 * @param ip_address IP to lookup
 * @return Allocated IP info or NULL
 */
allocated_ip_t* ipam_manager_get_ip_info(ipam_manager_t *manager, const char *ip_address);

/**
 * Get all allocated IPs for node
 * 
 * @param manager IPAM manager
 * @param node_name Node name
 * @param out_count Number of IPs returned
 * @return Array of allocated IP addresses
 */
char** ipam_manager_get_node_ips(ipam_manager_t *manager,
                                  const char *node_name,
                                  uint32_t *out_count);

/**
 * Check if IP is available (not allocated)
 * 
 * @param manager IPAM manager
 * @param ip_address IP to check
 * @return true if available
 */
bool ipam_manager_is_ip_available(ipam_manager_t *manager, const char *ip_address);

/**
 * Get IPAM statistics
 * 
 * @param manager IPAM manager
 * @param out_nodes Number of registered nodes
 * @param out_allocated_pods Number of allocated pod IPs
 * @param out_allocated_services Number of allocated service IPs
 * @param out_available_pods Number of available pod IPs
 * @param out_available_services Number of available service IPs
 */
void ipam_manager_get_stats(ipam_manager_t *manager,
                            uint32_t *out_nodes,
                            uint32_t *out_allocated_pods,
                            uint32_t *out_allocated_services,
                            uint32_t *out_available_pods,
                            uint32_t *out_available_services);

/**
 * Free allocated IP info
 */
void ipam_manager_free_ip_info(allocated_ip_t *info);

/**
 * Free IP address array
 */
void ipam_manager_free_ips(char **ips, uint32_t count);

/**
 * Get next available IP from CIDR block
 * 
 * @param manager IPAM manager
 * @param cidr_start Start IP (network byte order)
 * @param cidr_end End IP (network byte order)
 * @param out_ip Output IP address
 * @return true if available IP found
 */
bool ipam_manager_get_next_available_ip(ipam_manager_t *manager,
                                        uint32_t cidr_start,
                                        uint32_t cidr_end,
                                        uint32_t *out_ip);

#endif // SIRAH_IPAM_MANAGER_H
