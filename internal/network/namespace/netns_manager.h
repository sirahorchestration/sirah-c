/*
 * netns_manager.h
 * 
 * Network namespace management for VM isolation
 * 
 * Linux network namespaces provide process-level network stack isolation.
 * Each QEMU VM runs in its own network namespace with:
 *   - Isolated network interfaces
 *   - Independent routing table
 *   - Separate ARP cache
 *   - Isolated TCP/UDP socket state
 * 
 * This enables:
 *   - Multiple VMs on same host with conflicting IPs
 *   - Network isolation for security
 *   - VM network configuration without affecting host
 * 
 * Architecture:
 *   1. Create network namespace (ip netns add)
 *   2. Create veth pair: one in host, one in namespace
 *   3. Configure host-side veth to bridge
 *   4. Move namespace-side veth into namespace
 *   5. Configure IP inside namespace
 *   6. Add default route through host gateway
 *   7. VM (QEMU) runs in namespace with veth as network device
 * 
 * Kubernetes v1.28 Conformance:
 *   - Provides network isolation for pods (each pod gets namespace)
 *   - Supports multi-pod per node (multiple namespaces)
 *   - Integrates with VXLAN overlay for cross-node traffic
 *   - Supports IPv4 addressing within pod CIDR
 */

#ifndef SIRAH_NETNS_MANAGER_H
#define SIRAH_NETNS_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

/**
 * Network namespace configuration
 */
typedef struct {
    char *ns_name;                // Namespace identifier (e.g., "pod-uuid")
    char *ns_path;                // Full path to namespace (/var/run/netns/...)
    char *veth_host_name;         // Host-side veth name
    char *veth_container_name;    // Container-side veth name
    char *container_ip;           // IP address in namespace
    uint32_t container_ip_int;    // IP as integer (network byte order)
    char *container_gateway;      // Gateway IP for default route
    uint32_t subnet_prefix_len;   // Prefix length (e.g., 24 for /24)
} network_namespace_config_t;

/**
 * Network namespace instance
 */
typedef struct {
    char *namespace_id;           // Unique ID for this namespace
    char *ns_name;                // Linux namespace name
    char *ns_path;                // Full path to namespace
    
    // Veth pair
    char *veth_host;              // Host-side interface name
    char *veth_ns;                // Namespace-side interface name
    
    // IP configuration
    char *container_ip;           // Pod IP address
    char *container_gateway;      // Host gateway IP
    uint32_t subnet_prefix_len;
    
    // State
    bool created;                 // Namespace created on kernel
    bool veth_created;            // Veth pair created
    bool ip_configured;           // IP configured inside namespace
    
    time_t creation_time;
    time_t last_modified;
    
    pthread_mutex_t mutex;
} network_namespace_t;

/**
 * Network namespace manager
 */
typedef struct {
    network_namespace_t *namespaces;
    uint32_t namespace_count;
    uint32_t namespace_capacity;
    
    pthread_mutex_t mutex;
} netns_manager_t;

/**
 * Create network namespace manager
 */
netns_manager_t* netns_manager_create(void);

/**
 * Free network namespace manager
 */
void netns_manager_free(netns_manager_t *manager);

/**
 * Create new network namespace
 * 
 * Creates a new isolated network namespace for a pod/VM
 * Includes veth pair setup and basic IP configuration
 * 
 * @param manager Manager instance
 * @param namespace_id Unique pod identifier
 * @param config Namespace configuration
 * @return true on success
 */
bool netns_manager_create_namespace(netns_manager_t *manager,
                                    const char *namespace_id,
                                    const network_namespace_config_t *config);

/**
 * Delete network namespace
 * 
 * Removes namespace and all associated interfaces
 * Called when pod terminates
 * 
 * @param manager Manager instance
 * @param namespace_id Pod identifier
 * @return true on success
 */
bool netns_manager_delete_namespace(netns_manager_t *manager,
                                    const char *namespace_id);

/**
 * Get namespace by pod ID
 * 
 * @param manager Manager instance
 * @param namespace_id Pod identifier
 * @return Namespace instance or NULL if not found
 */
network_namespace_t* netns_manager_get_namespace(netns_manager_t *manager,
                                                 const char *namespace_id);

/**
 * Get all namespaces
 * 
 * @param manager Manager instance
 * @param out_count Number of namespaces
 * @return Array of namespaces (caller should not free)
 */
network_namespace_t* netns_manager_get_all(netns_manager_t *manager,
                                           uint32_t *out_count);

/**
 * Add interface to namespace
 * 
 * Attaches a pre-configured veth interface to a namespace
 * Called after veth pair is created in host
 * 
 * @param manager Manager instance
 * @param namespace_id Pod identifier
 * @param interface_name Interface to attach
 * @return true on success
 */
bool netns_manager_add_interface(netns_manager_t *manager,
                                 const char *namespace_id,
                                 const char *interface_name);

/**
 * Execute command in namespace context
 * 
 * Runs a command (fork + exec) inside a specific namespace
 * Useful for IP configuration, route setup, etc.
 * 
 * @param manager Manager instance
 * @param namespace_id Pod identifier
 * @param command Command to execute
 * @return true on success
 */
bool netns_manager_exec_in_namespace(netns_manager_t *manager,
                                     const char *namespace_id,
                                     const char *command);

/**
 * Configure IP address in namespace
 * 
 * Sets up IPv4 address and gateway for pod
 * Called after namespace creation
 * 
 * @param manager Manager instance
 * @param namespace_id Pod identifier
 * @param ip_address IP address (e.g., "10.244.1.10")
 * @param gateway_ip Gateway IP (e.g., "10.244.1.1")
 * @param prefix_len Subnet prefix (e.g., 24 for /24)
 * @return true on success
 */
bool netns_manager_configure_ip(netns_manager_t *manager,
                                const char *namespace_id,
                                const char *ip_address,
                                const char *gateway_ip,
                                uint32_t prefix_len);

/**
 * Check if namespace exists
 */
bool netns_manager_namespace_exists(netns_manager_t *manager,
                                    const char *namespace_id);

/**
 * Get namespace statistics
 */
void netns_manager_get_stats(netns_manager_t *manager,
                             uint32_t *out_total,
                             uint32_t *out_configured);

/**
 * Cleanup stale namespaces
 * 
 * Removes namespaces older than threshold
 * Should be called periodically to cleanup after pod termination
 * 
 * @param manager Manager instance
 * @param stale_seconds Age threshold in seconds
 * @return number of namespaces cleaned
 */
uint32_t netns_manager_cleanup_stale(netns_manager_t *manager,
                                     uint32_t stale_seconds);

#endif // SIRAH_NETNS_MANAGER_H
