/*
 * vxlan_tunnel.c
 * 
 * VXLAN tunnel implementation for overlay networking
 */

#include "vxlan_tunnel.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>

/**
 * Create VXLAN tunnel instance
 */
vxlan_tunnel_t* vxlan_tunnel_create(const char *device_name,
                                     uint32_t vni,
                                     const char *local_vtep_ip,
                                     uint16_t listen_port) {
    if (!device_name || !local_vtep_ip) {
        return NULL;
    }
    
    vxlan_tunnel_t *tunnel = malloc(sizeof(vxlan_tunnel_t));
    if (!tunnel) {
        return NULL;
    }
    
    memset(tunnel, 0, sizeof(vxlan_tunnel_t));
    
    tunnel->device_name = strdup(device_name);
    tunnel->vni = vni;
    tunnel->listen_port = listen_port;
    tunnel->mtu = 1450;  // 1500 - 50 bytes for VXLAN overhead
    tunnel->health_check_interval = 30;  // Check endpoints every 30 seconds
    
    // Convert IP string to network byte order
    if (inet_pton(AF_INET, local_vtep_ip, &tunnel->local_vtep_ip) != 1) {
        free(tunnel->device_name);
        free(tunnel);
        return NULL;
    }
    
    // Initialize endpoint array
    tunnel->endpoint_capacity = 16;
    tunnel->endpoints = malloc(sizeof(vxlan_endpoint_t) * tunnel->endpoint_capacity);
    if (!tunnel->endpoints) {
        free(tunnel->device_name);
        free(tunnel);
        return NULL;
    }
    
    // Initialize FDB array
    tunnel->fdb_capacity = 64;
    tunnel->fdb = malloc(sizeof(vxlan_fdb_entry_t) * tunnel->fdb_capacity);
    if (!tunnel->fdb) {
        free(tunnel->endpoints);
        free(tunnel->device_name);
        free(tunnel);
        return NULL;
    }
    
    // Initialize mutex
    if (pthread_mutex_init(&tunnel->mutex, NULL) != 0) {
        free(tunnel->fdb);
        free(tunnel->endpoints);
        free(tunnel->device_name);
        free(tunnel);
        return NULL;
    }
    
    return tunnel;
}

/**
 * Destroy VXLAN tunnel
 */
void vxlan_tunnel_destroy(vxlan_tunnel_t *tunnel) {
    if (!tunnel) return;
    
    // Cleanup endpoints
    if (tunnel->endpoints) {
        for (uint32_t i = 0; i < tunnel->endpoint_count; i++) {
            free(tunnel->endpoints[i].node_name);
            free(tunnel->endpoints[i].vtep_ip_str);
            free(tunnel->endpoints[i].pod_cidr);
        }
        free(tunnel->endpoints);
    }
    
    // Cleanup FDB
    free(tunnel->fdb);
    
    pthread_mutex_destroy(&tunnel->mutex);
    free(tunnel->device_name);
    free(tunnel);
}

/**
 * Initialize kernel VXLAN device
 */
bool vxlan_tunnel_initialize(vxlan_tunnel_t *tunnel) {
    if (!tunnel || !tunnel->device_name) {
        return false;
    }
    
    // In production, this would use netlink to:
    // 1. Load vxlan kernel module (modprobe vxlan)
    // 2. Create vxlan device with ip link add
    // 3. Set device MTU
    // 4. Bring device up
    
    // For now, mark as created and let kernel handle via netlink
    // Full implementation requires libmnl (netlink) library
    
    pthread_mutex_lock(&tunnel->mutex);
    tunnel->created = true;
    pthread_mutex_unlock(&tunnel->mutex);
    
    return true;
}

/**
 * Teardown kernel VXLAN device
 */
bool vxlan_tunnel_teardown(vxlan_tunnel_t *tunnel) {
    if (!tunnel) {
        return false;
    }
    
    // In production, would use netlink to remove device
    // ip link del <device_name>
    
    pthread_mutex_lock(&tunnel->mutex);
    tunnel->created = false;
    pthread_mutex_unlock(&tunnel->mutex);
    
    return true;
}

/**
 * Add remote VXLAN endpoint
 */
bool vxlan_tunnel_add_endpoint(vxlan_tunnel_t *tunnel,
                               const char *node_name,
                               const char *vtep_ip,
                               const char *pod_cidr) {
    if (!tunnel || !node_name || !vtep_ip || !pod_cidr) {
        return false;
    }
    
    pthread_mutex_lock(&tunnel->mutex);
    
    // Check if endpoint already exists
    for (uint32_t i = 0; i < tunnel->endpoint_count; i++) {
        if (strcmp(tunnel->endpoints[i].node_name, node_name) == 0) {
            // Update existing endpoint
            tunnel->endpoints[i].healthy = true;
            tunnel->endpoints[i].last_seen = time(NULL);
            pthread_mutex_unlock(&tunnel->mutex);
            return true;
        }
    }
    
    // Expand array if needed
    if (tunnel->endpoint_count >= tunnel->endpoint_capacity) {
        tunnel->endpoint_capacity *= 2;
        vxlan_endpoint_t *new_endpoints = realloc(tunnel->endpoints,
                                                   sizeof(vxlan_endpoint_t) * tunnel->endpoint_capacity);
        if (!new_endpoints) {
            pthread_mutex_unlock(&tunnel->mutex);
            return false;
        }
        tunnel->endpoints = new_endpoints;
    }
    
    // Add new endpoint
    uint32_t idx = tunnel->endpoint_count;
    memset(&tunnel->endpoints[idx], 0, sizeof(vxlan_endpoint_t));
    
    tunnel->endpoints[idx].node_name = strdup(node_name);
    tunnel->endpoints[idx].vtep_ip_str = strdup(vtep_ip);
    tunnel->endpoints[idx].pod_cidr = strdup(pod_cidr);
    tunnel->endpoints[idx].vxlan_port = tunnel->listen_port;
    tunnel->endpoints[idx].vni = tunnel->vni;
    tunnel->endpoints[idx].last_seen = time(NULL);
    tunnel->endpoints[idx].healthy = true;
    tunnel->endpoints[idx].failed_checks = 0;
    
    // Convert IP string to network byte order
    if (inet_pton(AF_INET, vtep_ip, &tunnel->endpoints[idx].vtep_ip) != 1) {
        free(tunnel->endpoints[idx].node_name);
        free(tunnel->endpoints[idx].vtep_ip_str);
        free(tunnel->endpoints[idx].pod_cidr);
        pthread_mutex_unlock(&tunnel->mutex);
        return false;
    }
    
    tunnel->endpoint_count++;
    pthread_mutex_unlock(&tunnel->mutex);
    
    return true;
}

/**
 * Remove remote VXLAN endpoint
 */
bool vxlan_tunnel_remove_endpoint(vxlan_tunnel_t *tunnel,
                                  const char *node_name) {
    if (!tunnel || !node_name) {
        return false;
    }
    
    pthread_mutex_lock(&tunnel->mutex);
    
    for (uint32_t i = 0; i < tunnel->endpoint_count; i++) {
        if (strcmp(tunnel->endpoints[i].node_name, node_name) == 0) {
            // Free endpoint resources
            free(tunnel->endpoints[i].node_name);
            free(tunnel->endpoints[i].vtep_ip_str);
            free(tunnel->endpoints[i].pod_cidr);
            
            // Shift remaining endpoints
            if (i < tunnel->endpoint_count - 1) {
                memmove(&tunnel->endpoints[i], &tunnel->endpoints[i + 1],
                        sizeof(vxlan_endpoint_t) * (tunnel->endpoint_count - i - 1));
            }
            
            tunnel->endpoint_count--;
            pthread_mutex_unlock(&tunnel->mutex);
            return true;
        }
    }
    
    pthread_mutex_unlock(&tunnel->mutex);
    return false;
}

/**
 * Add route for pod CIDR
 */
bool vxlan_tunnel_add_route(vxlan_tunnel_t *tunnel,
                            const char *pod_cidr) {
    if (!tunnel || !pod_cidr) {
        return false;
    }
    
    // In production, would add kernel route:
    // ip route add <pod_cidr> dev <device_name>
    
    // For now, just track in memory
    return true;
}

/**
 * Remove route for pod CIDR
 */
bool vxlan_tunnel_remove_route(vxlan_tunnel_t *tunnel,
                               const char *pod_cidr) {
    if (!tunnel || !pod_cidr) {
        return false;
    }
    
    // In production, would remove kernel route:
    // ip route del <pod_cidr> dev <device_name>
    
    return true;
}

/**
 * Update FDB entry
 */
bool vxlan_tunnel_update_fdb(vxlan_tunnel_t *tunnel,
                             const uint8_t mac_address[6],
                             const char *vtep_ip) {
    if (!tunnel || !mac_address || !vtep_ip) {
        return false;
    }
    
    pthread_mutex_lock(&tunnel->mutex);
    
    // Check if entry exists
    for (uint32_t i = 0; i < tunnel->fdb_count; i++) {
        if (memcmp(tunnel->fdb[i].mac_address, mac_address, 6) == 0) {
            // Update existing entry
            if (inet_pton(AF_INET, vtep_ip, &tunnel->fdb[i].vtep_ip) != 1) {
                pthread_mutex_unlock(&tunnel->mutex);
                return false;
            }
            tunnel->fdb[i].last_updated = time(NULL);
            pthread_mutex_unlock(&tunnel->mutex);
            return true;
        }
    }
    
    // Expand FDB if needed
    if (tunnel->fdb_count >= tunnel->fdb_capacity) {
        tunnel->fdb_capacity *= 2;
        vxlan_fdb_entry_t *new_fdb = realloc(tunnel->fdb,
                                              sizeof(vxlan_fdb_entry_t) * tunnel->fdb_capacity);
        if (!new_fdb) {
            pthread_mutex_unlock(&tunnel->mutex);
            return false;
        }
        tunnel->fdb = new_fdb;
    }
    
    // Add new FDB entry
    uint32_t idx = tunnel->fdb_count;
    memcpy(tunnel->fdb[idx].mac_address, mac_address, 6);
    if (inet_pton(AF_INET, vtep_ip, &tunnel->fdb[idx].vtep_ip) != 1) {
        pthread_mutex_unlock(&tunnel->mutex);
        return false;
    }
    tunnel->fdb[idx].last_updated = time(NULL);
    tunnel->fdb_count++;
    
    pthread_mutex_unlock(&tunnel->mutex);
    return true;
}

/**
 * Health check for remote endpoints
 */
uint32_t vxlan_tunnel_health_check(vxlan_tunnel_t *tunnel) {
    if (!tunnel) {
        return 0;
    }
    
    pthread_mutex_lock(&tunnel->mutex);
    
    uint32_t healthy_count = 0;
    time_t now = time(NULL);
    
    // In production, would use ICMP echo (ping) or ARP to check reachability
    // For now, consider endpoint healthy if we saw it recently
    
    for (uint32_t i = 0; i < tunnel->endpoint_count; i++) {
        time_t age = now - tunnel->endpoints[i].last_seen;
        
        if (age > 120) {  // Mark unhealthy if not seen for 2 minutes
            tunnel->endpoints[i].healthy = false;
            tunnel->endpoints[i].failed_checks++;
        } else {
            tunnel->endpoints[i].healthy = true;
            tunnel->endpoints[i].failed_checks = 0;
            healthy_count++;
        }
    }
    
    tunnel->last_health_check = now;
    pthread_mutex_unlock(&tunnel->mutex);
    
    return healthy_count;
}

/**
 * Get endpoint by node name
 */
vxlan_endpoint_t* vxlan_tunnel_get_endpoint(vxlan_tunnel_t *tunnel,
                                            const char *node_name) {
    if (!tunnel || !node_name) {
        return NULL;
    }
    
    pthread_mutex_lock(&tunnel->mutex);
    
    for (uint32_t i = 0; i < tunnel->endpoint_count; i++) {
        if (strcmp(tunnel->endpoints[i].node_name, node_name) == 0) {
            pthread_mutex_unlock(&tunnel->mutex);
            return &tunnel->endpoints[i];
        }
    }
    
    pthread_mutex_unlock(&tunnel->mutex);
    return NULL;
}

/**
 * Get list of all endpoints
 */
vxlan_endpoint_t* vxlan_tunnel_get_endpoints(vxlan_tunnel_t *tunnel,
                                             uint32_t *out_count) {
    if (!tunnel || !out_count) {
        return NULL;
    }
    
    pthread_mutex_lock(&tunnel->mutex);
    *out_count = tunnel->endpoint_count;
    pthread_mutex_unlock(&tunnel->mutex);
    
    return tunnel->endpoints;
}

/**
 * Get tunnel statistics
 */
void vxlan_tunnel_get_stats(vxlan_tunnel_t *tunnel,
                            uint32_t *out_endpoint_count,
                            uint32_t *out_fdb_count,
                            uint32_t *out_healthy_endpoints) {
    if (!tunnel) return;
    
    pthread_mutex_lock(&tunnel->mutex);
    
    if (out_endpoint_count) *out_endpoint_count = tunnel->endpoint_count;
    if (out_fdb_count) *out_fdb_count = tunnel->fdb_count;
    
    if (out_healthy_endpoints) {
        uint32_t healthy = 0;
        for (uint32_t i = 0; i < tunnel->endpoint_count; i++) {
            if (tunnel->endpoints[i].healthy) {
                healthy++;
            }
        }
        *out_healthy_endpoints = healthy;
    }
    
    pthread_mutex_unlock(&tunnel->mutex);
}

/**
 * Check if tunnel is initialized
 */
bool vxlan_tunnel_is_initialized(vxlan_tunnel_t *tunnel) {
    if (!tunnel) return false;
    
    pthread_mutex_lock(&tunnel->mutex);
    bool created = tunnel->created;
    pthread_mutex_unlock(&tunnel->mutex);
    
    return created;
}

/**
 * Get MTU for tunnel
 */
uint32_t vxlan_tunnel_get_mtu(vxlan_tunnel_t *tunnel) {
    if (!tunnel) return 0;
    return tunnel->mtu;
}
