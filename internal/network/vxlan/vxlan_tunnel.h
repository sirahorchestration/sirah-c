/*
 * vxlan_tunnel.h
 * 
 * VXLAN (Virtual Extensible LAN) tunnel management for pod-to-pod networking
 * 
 * VXLAN is an overlay network protocol (RFC 7348) that encapsulates Ethernet frames
 * in UDP packets, allowing virtual networks to span across physical networks.
 * 
 * Architecture:
 *   - Each node has a VXLAN tunnel endpoint (VTEP)
 *   - Pod-to-pod traffic within a node uses host network directly
 *   - Pod-to-pod traffic across nodes:
 *     1. Traffic from pod on node-A → VM's veth interface
 *     2. Traffic reaches host namespace on node-A
 *     3. VXLAN VTEP on node-A encapsulates frame in UDP to node-B's VTEP
 *     4. VXLAN VTEP on node-B decapsulates and sends to destination pod
 * 
 * Features:
 *   - Tunnel creation and management
 *   - Forwarding Database (FDB) for MAC→VTEP mappings
 *   - Route management (pod CIDR routes point to VXLAN device)
 *   - Dynamic endpoint discovery
 *   - Health checking and failover
 *   - MTU handling (VXLAN adds 50 bytes overhead)
 * 
 * Kubernetes v1.28 Conformance:
 *   - Implements overlay network for pod communication
 *   - Supports per-node pod CIDR allocation
 *   - Provides cross-node pod connectivity
 *   - Integrates with node CNI (Container Network Interface) pattern
 */

#ifndef SIRAH_VXLAN_TUNNEL_H
#define SIRAH_VXLAN_TUNNEL_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>

/**
 * VXLAN tunnel endpoint information
 */
typedef struct {
    char *node_name;              // Node identifier (e.g., "worker-1")
    uint32_t vtep_ip;             // VTEP IP address (network byte order)
    char *vtep_ip_str;            // VTEP IP as string (e.g., "10.0.0.1")
    uint16_t vxlan_port;          // VXLAN UDP port (default 4789)
    char *pod_cidr;               // Pod CIDR for this node (e.g., "10.244.1.0/24")
    uint32_t vni;                 // VXLAN Network Identifier (typically 100)
    time_t last_seen;             // Last heartbeat time
    bool healthy;                 // Health status
    uint32_t failed_checks;       // Consecutive failed health checks
} vxlan_endpoint_t;

/**
 * FDB (Forwarding Database) entry
 */
typedef struct {
    uint8_t mac_address[6];       // MAC address
    uint32_t vtep_ip;             // VTEP IP for this MAC (network byte order)
    time_t last_updated;
} vxlan_fdb_entry_t;

/**
 * VXLAN tunnel instance
 */
typedef struct {
    char *device_name;            // VXLAN device name (e.g., "vxlan100")
    uint32_t vni;                 // VXLAN Network Identifier
    uint32_t local_vtep_ip;       // This node's VTEP IP
    uint16_t listen_port;         // UDP port for VXLAN (default 4789)
    uint32_t mtu;                 // MTU (typically 1450 for 50-byte VXLAN overhead)
    
    // Remote endpoints
    vxlan_endpoint_t *endpoints;
    uint32_t endpoint_count;
    uint32_t endpoint_capacity;
    
    // Forwarding database
    vxlan_fdb_entry_t *fdb;
    uint32_t fdb_count;
    uint32_t fdb_capacity;
    
    // State
    bool created;                 // Device created on kernel
    pthread_mutex_t mutex;
    
    // Health checking
    uint32_t health_check_interval;  // Seconds between health checks
    time_t last_health_check;
    
} vxlan_tunnel_t;

/**
 * Create VXLAN tunnel instance
 * 
 * @param device_name Name for vxlan device (e.g., "vxlan100")
 * @param vni VXLAN Network Identifier (typically 100)
 * @param local_vtep_ip This node's VTEP IP address as string
 * @param listen_port UDP port for VXLAN (4789 is standard)
 * @return Tunnel instance or NULL on error
 */
vxlan_tunnel_t* vxlan_tunnel_create(const char *device_name,
                                     uint32_t vni,
                                     const char *local_vtep_ip,
                                     uint16_t listen_port);

/**
 * Destroy VXLAN tunnel and cleanup resources
 * 
 * @param tunnel Tunnel instance to destroy
 */
void vxlan_tunnel_destroy(vxlan_tunnel_t *tunnel);

/**
 * Initialize kernel VXLAN device
 * 
 * Creates the VXLAN device on the kernel using netlink
 * Must be called once after creating tunnel instance
 * Requires root/CAP_NET_ADMIN privileges
 * 
 * @param tunnel Tunnel instance
 * @return true on success
 */
bool vxlan_tunnel_initialize(vxlan_tunnel_t *tunnel);

/**
 * Teardown kernel VXLAN device
 * 
 * Removes the VXLAN device from kernel
 * 
 * @param tunnel Tunnel instance
 * @return true on success
 */
bool vxlan_tunnel_teardown(vxlan_tunnel_t *tunnel);

/**
 * Add remote VXLAN endpoint (peer node)
 * 
 * Adds information about another node's VTEP
 * Updates FDB for connectivity to that node's pod CIDR
 * 
 * @param tunnel Tunnel instance
 * @param node_name Remote node name
 * @param vtep_ip Remote VTEP IP address as string
 * @param pod_cidr Pod CIDR for remote node
 * @return true on success
 */
bool vxlan_tunnel_add_endpoint(vxlan_tunnel_t *tunnel,
                               const char *node_name,
                               const char *vtep_ip,
                               const char *pod_cidr);

/**
 * Remove remote VXLAN endpoint
 * 
 * @param tunnel Tunnel instance
 * @param node_name Remote node name to remove
 * @return true on success
 */
bool vxlan_tunnel_remove_endpoint(vxlan_tunnel_t *tunnel,
                                  const char *node_name);

/**
 * Add route for pod CIDR through VXLAN device
 * 
 * Example: route 10.244.1.0/24 via vxlan100
 * Ensures traffic to remote pods is sent to VXLAN device
 * 
 * @param tunnel Tunnel instance
 * @param pod_cidr Pod CIDR to route
 * @return true on success
 */
bool vxlan_tunnel_add_route(vxlan_tunnel_t *tunnel,
                            const char *pod_cidr);

/**
 * Remove route for pod CIDR
 * 
 * @param tunnel Tunnel instance
 * @param pod_cidr Pod CIDR to remove
 * @return true on success
 */
bool vxlan_tunnel_remove_route(vxlan_tunnel_t *tunnel,
                               const char *pod_cidr);

/**
 * Update FDB entry (MAC→VTEP mapping)
 * 
 * FDB (Forwarding Database) maps destination MAC addresses to VXLAN endpoints
 * Used by kernel to determine which VTEP to send encapsulated frames to
 * 
 * @param tunnel Tunnel instance
 * @param mac_address MAC address (6 bytes)
 * @param vtep_ip VTEP IP where this MAC is located
 * @return true on success
 */
bool vxlan_tunnel_update_fdb(vxlan_tunnel_t *tunnel,
                             const uint8_t mac_address[6],
                             const char *vtep_ip);

/**
 * Health check for remote endpoints
 * 
 * Verifies connectivity to all registered VXLAN endpoints
 * Marks endpoints as unhealthy if unreachable
 * Triggers failover or route updates if needed
 * 
 * @param tunnel Tunnel instance
 * @return number of healthy endpoints
 */
uint32_t vxlan_tunnel_health_check(vxlan_tunnel_t *tunnel);

/**
 * Get endpoint by node name
 * 
 * @param tunnel Tunnel instance
 * @param node_name Node name to find
 * @return Endpoint pointer or NULL if not found
 */
vxlan_endpoint_t* vxlan_tunnel_get_endpoint(vxlan_tunnel_t *tunnel,
                                            const char *node_name);

/**
 * Get list of all endpoints
 * 
 * @param tunnel Tunnel instance
 * @param out_count Number of endpoints returned
 * @return Array of endpoints (caller should not free)
 */
vxlan_endpoint_t* vxlan_tunnel_get_endpoints(vxlan_tunnel_t *tunnel,
                                             uint32_t *out_count);

/**
 * Get tunnel statistics
 * 
 * @param tunnel Tunnel instance
 * @param out_endpoint_count Number of active endpoints
 * @param out_fdb_count Number of FDB entries
 * @param out_healthy_endpoints Number of healthy endpoints
 */
void vxlan_tunnel_get_stats(vxlan_tunnel_t *tunnel,
                            uint32_t *out_endpoint_count,
                            uint32_t *out_fdb_count,
                            uint32_t *out_healthy_endpoints);

/**
 * Check if tunnel is initialized on kernel
 */
bool vxlan_tunnel_is_initialized(vxlan_tunnel_t *tunnel);

/**
 * Get MTU for tunnel (accounting for VXLAN overhead)
 */
uint32_t vxlan_tunnel_get_mtu(vxlan_tunnel_t *tunnel);

#endif // SIRAH_VXLAN_TUNNEL_H
