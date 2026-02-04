/*
 * ipam_manager.c
 * 
 * IP Address Management implementation
 */

#include "ipam_manager.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <pthread.h>

/**
 * Convert IP string to uint32_t (network byte order)
 */
static uint32_t ip_string_to_int(const char *ip_str) {
    struct in_addr addr;
    if (inet_pton(AF_INET, ip_str, &addr) != 1) {
        return 0;
    }
    return addr.s_addr;  // Already in network byte order
}

/**
 * Convert uint32_t (network byte order) to IP string
 */
static char* ip_int_to_string(uint32_t ip_int) {
    struct in_addr addr;
    addr.s_addr = ip_int;
    char *result = malloc(16);
    if (result) {
        inet_ntop(AF_INET, &addr, result, 16);
    }
    return result;
}

/**
 * Extract network and broadcast from CIDR notation
 */
static void parse_cidr(const char *cidr_str, uint32_t *out_network, uint32_t *out_broadcast, uint32_t *out_prefix) {
    char *slash = strchr(cidr_str, '/');
    if (!slash) {
        *out_network = 0;
        *out_broadcast = 0;
        *out_prefix = 0;
        return;
    }
    
    // Parse IP part
    size_t ip_len = slash - cidr_str;
    char ip_part[16];
    strncpy(ip_part, cidr_str, ip_len);
    ip_part[ip_len] = '\0';
    
    uint32_t ip = ip_string_to_int(ip_part);
    uint32_t prefix = atoi(slash + 1);
    
    // Calculate network and broadcast
    uint32_t mask = (0xFFFFFFFF << (32 - prefix));
    *out_network = ip & mask;
    *out_broadcast = *out_network | ~mask;
    *out_prefix = prefix;
}

/**
 * Create IPAM manager
 */
ipam_manager_t* ipam_manager_create(const ipam_config_t *config) {
    if (!config || !config->cluster_cidr || !config->service_cidr) {
        return NULL;
    }
    
    ipam_manager_t *manager = malloc(sizeof(ipam_manager_t));
    if (!manager) return NULL;
    
    memset(manager, 0, sizeof(ipam_manager_t));
    
    // Copy configuration
    manager->config.cluster_cidr = strdup(config->cluster_cidr);
    manager->config.service_cidr = strdup(config->service_cidr);
    manager->config.node_cidr_prefix = strdup(config->node_cidr_prefix);
    manager->config.cluster_prefix_len = config->cluster_prefix_len;
    manager->config.node_prefix_len = config->node_prefix_len;
    manager->config.service_prefix_len = config->service_prefix_len;
    
    if (!manager->config.cluster_cidr || !manager->config.service_cidr || !manager->config.node_cidr_prefix) {
        free(manager->config.cluster_cidr);
        free(manager->config.service_cidr);
        free(manager->config.node_cidr_prefix);
        free(manager);
        return NULL;
    }
    
    // Parse cluster CIDR
    parse_cidr(config->cluster_cidr, &manager->cluster_start, &manager->cluster_end, NULL);
    
    // Parse service CIDR
    parse_cidr(config->service_cidr, &manager->service_start, &manager->service_end, NULL);
    
    // Initialize node array
    manager->node_capacity = 32;
    manager->nodes = malloc(sizeof(node_allocation_t) * manager->node_capacity);
    if (!manager->nodes) {
        free(manager->config.cluster_cidr);
        free(manager->config.service_cidr);
        free(manager->config.node_cidr_prefix);
        free(manager);
        return NULL;
    }
    
    // Initialize IP pools
    manager->pod_ips.ip_capacity = 256;
    manager->pod_ips.allocated_ips = malloc(sizeof(uint32_t) * manager->pod_ips.ip_capacity);
    
    manager->service_ips.ip_capacity = 64;
    manager->service_ips.allocated_ips = malloc(sizeof(uint32_t) * manager->service_ips.ip_capacity);
    
    if (!manager->pod_ips.allocated_ips || !manager->service_ips.allocated_ips) {
        free(manager->pod_ips.allocated_ips);
        free(manager->service_ips.allocated_ips);
        free(manager->nodes);
        free(manager->config.cluster_cidr);
        free(manager->config.service_cidr);
        free(manager->config.node_cidr_prefix);
        free(manager);
        return NULL;
    }
    
    pthread_mutex_init(&manager->mutex, NULL);
    pthread_mutex_init(&manager->pod_ips.mutex, NULL);
    pthread_mutex_init(&manager->service_ips.mutex, NULL);
    
    return manager;
}

/**
 * Free IPAM manager
 */
void ipam_manager_free(ipam_manager_t *manager) {
    if (!manager) return;
    
    // Free nodes
    for (uint32_t i = 0; i < manager->node_count; i++) {
        free(manager->nodes[i].node_name);
        free(manager->nodes[i].pod_cidr_str);
        free(manager->nodes[i].allocated_ips);
    }
    free(manager->nodes);
    
    // Free IP pools
    free(manager->pod_ips.allocated_ips);
    free(manager->service_ips.allocated_ips);
    
    // Free config
    free(manager->config.cluster_cidr);
    free(manager->config.service_cidr);
    free(manager->config.node_cidr_prefix);
    
    pthread_mutex_destroy(&manager->mutex);
    pthread_mutex_destroy(&manager->pod_ips.mutex);
    pthread_mutex_destroy(&manager->service_ips.mutex);
    
    free(manager);
}

/**
 * Register node with IPAM
 */
char* ipam_manager_register_node(ipam_manager_t *manager, const char *node_name) {
    if (!manager || !node_name) {
        return NULL;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    // Check if node already registered
    for (uint32_t i = 0; i < manager->node_count; i++) {
        if (strcmp(manager->nodes[i].node_name, node_name) == 0) {
            char *cidr = strdup(manager->nodes[i].pod_cidr_str);
            pthread_mutex_unlock(&manager->mutex);
            return cidr;
        }
    }
    
    // Expand node array if needed
    if (manager->node_count >= manager->node_capacity) {
        manager->node_capacity *= 2;
        node_allocation_t *new_nodes = realloc(manager->nodes,
                                                sizeof(node_allocation_t) * manager->node_capacity);
        if (!new_nodes) {
            pthread_mutex_unlock(&manager->mutex);
            return NULL;
        }
        manager->nodes = new_nodes;
    }
    
    // Calculate pod CIDR for this node
    // Allocate a /24 from the cluster /16
    // Node 1: 10.244.1.0/24, Node 2: 10.244.2.0/24, etc.
    
    uint32_t node_index = manager->node_count;
    uint32_t node_ip = ntohl(manager->cluster_start) + (node_index << 8);
    
    // Create node allocation
    node_allocation_t *node = &manager->nodes[node_index];
    memset(node, 0, sizeof(node_allocation_t));
    
    node->node_name = strdup(node_name);
    node->node_cidr_start = htonl(node_ip);
    node->node_cidr_end = htonl(node_ip + 255);
    
    // Generate CIDR string
    char cidr_str[32];
    struct in_addr addr;
    addr.s_addr = htonl(node_ip);
    inet_ntop(AF_INET, &addr, cidr_str, 32);
    strncat(cidr_str, "/24", 4);
    
    node->pod_cidr_str = strdup(cidr_str);
    
    // Initialize IP array for node
    node->ip_capacity = 256;
    node->allocated_ips = malloc(sizeof(uint32_t) * node->ip_capacity);
    if (!node->allocated_ips) {
        free(node->node_name);
        free(node->pod_cidr_str);
        pthread_mutex_unlock(&manager->mutex);
        return NULL;
    }
    
    manager->node_count++;
    
    char *result = strdup(node->pod_cidr_str);
    pthread_mutex_unlock(&manager->mutex);
    
    return result;
}

/**
 * Unregister node from IPAM
 */
bool ipam_manager_unregister_node(ipam_manager_t *manager, const char *node_name) {
    if (!manager || !node_name) {
        return false;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    for (uint32_t i = 0; i < manager->node_count; i++) {
        if (strcmp(manager->nodes[i].node_name, node_name) == 0) {
            // Free node resources
            free(manager->nodes[i].node_name);
            free(manager->nodes[i].pod_cidr_str);
            free(manager->nodes[i].allocated_ips);
            
            // Shift remaining nodes
            if (i < manager->node_count - 1) {
                memmove(&manager->nodes[i], &manager->nodes[i + 1],
                        sizeof(node_allocation_t) * (manager->node_count - i - 1));
            }
            
            manager->node_count--;
            pthread_mutex_unlock(&manager->mutex);
            return true;
        }
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return false;
}

/**
 * Allocate pod IP address
 */
char* ipam_manager_allocate_pod_ip(ipam_manager_t *manager,
                                    const char *node_name,
                                    const char *pod_name,
                                    const char *pod_namespace) {
    if (!manager || !node_name || !pod_name) {
        return NULL;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    // Find node
    node_allocation_t *node = NULL;
    for (uint32_t i = 0; i < manager->node_count; i++) {
        if (strcmp(manager->nodes[i].node_name, node_name) == 0) {
            node = &manager->nodes[i];
            break;
        }
    }
    
    if (!node) {
        pthread_mutex_unlock(&manager->mutex);
        return NULL;
    }
    
    // Get next available IP from node's CIDR
    uint32_t allocated_ip = 0;
    if (ipam_manager_get_next_available_ip(manager, node->node_cidr_start, node->node_cidr_end, &allocated_ip)) {
        // Add to node's allocated IPs
        if (node->ip_count >= node->ip_capacity) {
            node->ip_capacity *= 2;
            uint32_t *new_ips = realloc(node->allocated_ips, sizeof(uint32_t) * node->ip_capacity);
            if (!new_ips) {
                pthread_mutex_unlock(&manager->mutex);
                return NULL;
            }
            node->allocated_ips = new_ips;
        }
        
        node->allocated_ips[node->ip_count] = allocated_ip;
        node->ip_count++;
        
        // Add to global pool
        if (manager->pod_ips.ip_count >= manager->pod_ips.ip_capacity) {
            manager->pod_ips.ip_capacity *= 2;
            uint32_t *new_ips = realloc(manager->pod_ips.allocated_ips,
                                        sizeof(uint32_t) * manager->pod_ips.ip_capacity);
            if (!new_ips) {
                pthread_mutex_unlock(&manager->mutex);
                return NULL;
            }
            manager->pod_ips.allocated_ips = new_ips;
        }
        
        manager->pod_ips.allocated_ips[manager->pod_ips.ip_count] = allocated_ip;
        manager->pod_ips.ip_count++;
        
        // Convert to string
        char *ip_str = ip_int_to_string(allocated_ip);
        pthread_mutex_unlock(&manager->mutex);
        return ip_str;
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return NULL;
}

/**
 * Release pod IP address
 */
bool ipam_manager_release_pod_ip(ipam_manager_t *manager, const char *ip_address) {
    if (!manager || !ip_address) {
        return false;
    }
    
    uint32_t ip = ip_string_to_int(ip_address);
    
    pthread_mutex_lock(&manager->mutex);
    
    // Remove from global pool
    for (uint32_t i = 0; i < manager->pod_ips.ip_count; i++) {
        if (manager->pod_ips.allocated_ips[i] == ip) {
            if (i < manager->pod_ips.ip_count - 1) {
                memmove(&manager->pod_ips.allocated_ips[i],
                        &manager->pod_ips.allocated_ips[i + 1],
                        sizeof(uint32_t) * (manager->pod_ips.ip_count - i - 1));
            }
            manager->pod_ips.ip_count--;
            break;
        }
    }
    
    // Remove from node pool
    for (uint32_t i = 0; i < manager->node_count; i++) {
        for (uint32_t j = 0; j < manager->nodes[i].ip_count; j++) {
            if (manager->nodes[i].allocated_ips[j] == ip) {
                if (j < manager->nodes[i].ip_count - 1) {
                    memmove(&manager->nodes[i].allocated_ips[j],
                            &manager->nodes[i].allocated_ips[j + 1],
                            sizeof(uint32_t) * (manager->nodes[i].ip_count - j - 1));
                }
                manager->nodes[i].ip_count--;
                pthread_mutex_unlock(&manager->mutex);
                return true;
            }
        }
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return false;
}

/**
 * Allocate service IP
 */
char* ipam_manager_allocate_service_ip(ipam_manager_t *manager,
                                        const char *service_name,
                                        const char *service_namespace) {
    if (!manager || !service_name) {
        return NULL;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    uint32_t allocated_ip = 0;
    if (ipam_manager_get_next_available_ip(manager, manager->service_start, manager->service_end, &allocated_ip)) {
        if (manager->service_ips.ip_count >= manager->service_ips.ip_capacity) {
            manager->service_ips.ip_capacity *= 2;
            uint32_t *new_ips = realloc(manager->service_ips.allocated_ips,
                                        sizeof(uint32_t) * manager->service_ips.ip_capacity);
            if (!new_ips) {
                pthread_mutex_unlock(&manager->mutex);
                return NULL;
            }
            manager->service_ips.allocated_ips = new_ips;
        }
        
        manager->service_ips.allocated_ips[manager->service_ips.ip_count] = allocated_ip;
        manager->service_ips.ip_count++;
        
        char *ip_str = ip_int_to_string(allocated_ip);
        pthread_mutex_unlock(&manager->mutex);
        return ip_str;
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return NULL;
}

/**
 * Release service IP
 */
bool ipam_manager_release_service_ip(ipam_manager_t *manager, const char *ip_address) {
    if (!manager || !ip_address) {
        return false;
    }
    
    uint32_t ip = ip_string_to_int(ip_address);
    
    pthread_mutex_lock(&manager->mutex);
    
    for (uint32_t i = 0; i < manager->service_ips.ip_count; i++) {
        if (manager->service_ips.allocated_ips[i] == ip) {
            if (i < manager->service_ips.ip_count - 1) {
                memmove(&manager->service_ips.allocated_ips[i],
                        &manager->service_ips.allocated_ips[i + 1],
                        sizeof(uint32_t) * (manager->service_ips.ip_count - i - 1));
            }
            manager->service_ips.ip_count--;
            pthread_mutex_unlock(&manager->mutex);
            return true;
        }
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return false;
}

/**
 * Get node's pod CIDR
 */
char* ipam_manager_get_node_pod_cidr(ipam_manager_t *manager, const char *node_name) {
    if (!manager || !node_name) {
        return NULL;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    for (uint32_t i = 0; i < manager->node_count; i++) {
        if (strcmp(manager->nodes[i].node_name, node_name) == 0) {
            char *cidr = strdup(manager->nodes[i].pod_cidr_str);
            pthread_mutex_unlock(&manager->mutex);
            return cidr;
        }
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return NULL;
}

/**
 * Get allocated IP info
 */
allocated_ip_t* ipam_manager_get_ip_info(ipam_manager_t *manager, const char *ip_address) {
    if (!manager || !ip_address) {
        return NULL;
    }
    
    uint32_t ip = ip_string_to_int(ip_address);
    
    pthread_mutex_lock(&manager->mutex);
    
    for (uint32_t i = 0; i < manager->node_count; i++) {
        for (uint32_t j = 0; j < manager->nodes[i].ip_count; j++) {
            if (manager->nodes[i].allocated_ips[j] == ip) {
                allocated_ip_t *info = malloc(sizeof(allocated_ip_t));
                if (info) {
                    info->ip_address = ip;
                    info->ip_string = strdup(ip_address);
                    info->node_name = strdup(manager->nodes[i].node_name);
                    info->is_service_ip = false;
                    info->pod_name = NULL;
                    info->pod_namespace = NULL;
                }
                pthread_mutex_unlock(&manager->mutex);
                return info;
            }
        }
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return NULL;
}

/**
 * Get all node IPs
 */
char** ipam_manager_get_node_ips(ipam_manager_t *manager,
                                  const char *node_name,
                                  uint32_t *out_count) {
    if (!manager || !node_name || !out_count) {
        return NULL;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    for (uint32_t i = 0; i < manager->node_count; i++) {
        if (strcmp(manager->nodes[i].node_name, node_name) == 0) {
            char **ips = malloc(sizeof(char*) * manager->nodes[i].ip_count);
            if (ips) {
                for (uint32_t j = 0; j < manager->nodes[i].ip_count; j++) {
                    ips[j] = ip_int_to_string(manager->nodes[i].allocated_ips[j]);
                }
                *out_count = manager->nodes[i].ip_count;
            }
            pthread_mutex_unlock(&manager->mutex);
            return ips;
        }
    }
    
    pthread_mutex_unlock(&manager->mutex);
    *out_count = 0;
    return NULL;
}

/**
 * Check if IP is available
 */
bool ipam_manager_is_ip_available(ipam_manager_t *manager, const char *ip_address) {
    if (!manager || !ip_address) {
        return false;
    }
    
    uint32_t ip = ip_string_to_int(ip_address);
    
    pthread_mutex_lock(&manager->mutex);
    
    // Check pod IPs
    for (uint32_t i = 0; i < manager->pod_ips.ip_count; i++) {
        if (manager->pod_ips.allocated_ips[i] == ip) {
            pthread_mutex_unlock(&manager->mutex);
            return false;
        }
    }
    
    // Check service IPs
    for (uint32_t i = 0; i < manager->service_ips.ip_count; i++) {
        if (manager->service_ips.allocated_ips[i] == ip) {
            pthread_mutex_unlock(&manager->mutex);
            return false;
        }
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return true;
}

/**
 * Get IPAM statistics
 */
void ipam_manager_get_stats(ipam_manager_t *manager,
                            uint32_t *out_nodes,
                            uint32_t *out_allocated_pods,
                            uint32_t *out_allocated_services,
                            uint32_t *out_available_pods,
                            uint32_t *out_available_services) {
    if (!manager) return;
    
    pthread_mutex_lock(&manager->mutex);
    
    if (out_nodes) *out_nodes = manager->node_count;
    if (out_allocated_pods) *out_allocated_pods = manager->pod_ips.ip_count;
    if (out_allocated_services) *out_allocated_services = manager->service_ips.ip_count;
    
    // Calculate available
    uint32_t total_pod_ips = 0;
    for (uint32_t i = 0; i < manager->node_count; i++) {
        total_pod_ips += 256;  // Each node gets /24 = 256 IPs
    }
    uint32_t total_service_ips = (ntohl(manager->service_end) - ntohl(manager->service_start));
    
    if (out_available_pods) *out_available_pods = total_pod_ips - manager->pod_ips.ip_count;
    if (out_available_services) *out_available_services = total_service_ips - manager->service_ips.ip_count;
    
    pthread_mutex_unlock(&manager->mutex);
}

/**
 * Free allocated IP info
 */
void ipam_manager_free_ip_info(allocated_ip_t *info) {
    if (!info) return;
    free(info->ip_string);
    free(info->node_name);
    free(info->pod_name);
    free(info->pod_namespace);
    free(info);
}

/**
 * Free IP address array
 */
void ipam_manager_free_ips(char **ips, uint32_t count) {
    if (!ips) return;
    for (uint32_t i = 0; i < count; i++) {
        free(ips[i]);
    }
    free(ips);
}

/**
 * Get next available IP
 */
bool ipam_manager_get_next_available_ip(ipam_manager_t *manager,
                                        uint32_t cidr_start,
                                        uint32_t cidr_end,
                                        uint32_t *out_ip) {
    if (!manager || !out_ip) {
        return false;
    }
    
    // Skip network and gateway addresses
    uint32_t start = ntohl(cidr_start) + 2;  // Skip .0 and .1 (gateway)
    uint32_t end = ntohl(cidr_end) - 1;     // Skip broadcast
    
    for (uint32_t ip = start; ip <= end; ip++) {
        uint32_t ip_network_order = htonl(ip);
        
        // Check if allocated
        bool allocated = false;
        for (uint32_t i = 0; i < manager->pod_ips.ip_count; i++) {
            if (manager->pod_ips.allocated_ips[i] == ip_network_order) {
                allocated = true;
                break;
            }
        }
        
        if (!allocated) {
            *out_ip = ip_network_order;
            return true;
        }
    }
    
    return false;
}
