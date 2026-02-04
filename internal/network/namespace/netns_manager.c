/*
 * netns_manager.c
 * 
 * Network namespace management implementation
 */

#include "netns_manager.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <time.h>

/**
 * Create network namespace manager
 */
netns_manager_t* netns_manager_create(void) {
    netns_manager_t *manager = malloc(sizeof(netns_manager_t));
    if (!manager) {
        return NULL;
    }
    
    memset(manager, 0, sizeof(netns_manager_t));
    
    manager->namespace_capacity = 32;
    manager->namespaces = malloc(sizeof(network_namespace_t) * manager->namespace_capacity);
    if (!manager->namespaces) {
        free(manager);
        return NULL;
    }
    
    if (pthread_mutex_init(&manager->mutex, NULL) != 0) {
        free(manager->namespaces);
        free(manager);
        return NULL;
    }
    
    return manager;
}

/**
 * Free network namespace manager
 */
void netns_manager_free(netns_manager_t *manager) {
    if (!manager) return;
    
    // Free all namespaces
    for (uint32_t i = 0; i < manager->namespace_count; i++) {
        free(manager->namespaces[i].namespace_id);
        free(manager->namespaces[i].ns_name);
        free(manager->namespaces[i].ns_path);
        free(manager->namespaces[i].veth_host);
        free(manager->namespaces[i].veth_ns);
        free(manager->namespaces[i].container_ip);
        free(manager->namespaces[i].container_gateway);
        pthread_mutex_destroy(&manager->namespaces[i].mutex);
    }
    
    free(manager->namespaces);
    pthread_mutex_destroy(&manager->mutex);
    free(manager);
}

/**
 * Create new network namespace
 */
bool netns_manager_create_namespace(netns_manager_t *manager,
                                    const char *namespace_id,
                                    const network_namespace_config_t *config) {
    if (!manager || !namespace_id || !config) {
        return false;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    // Expand capacity if needed
    if (manager->namespace_count >= manager->namespace_capacity) {
        manager->namespace_capacity *= 2;
        network_namespace_t *new_ns = realloc(manager->namespaces,
                                               sizeof(network_namespace_t) * manager->namespace_capacity);
        if (!new_ns) {
            pthread_mutex_unlock(&manager->mutex);
            return false;
        }
        manager->namespaces = new_ns;
    }
    
    // Initialize namespace
    uint32_t idx = manager->namespace_count;
    network_namespace_t *ns = &manager->namespaces[idx];
    memset(ns, 0, sizeof(network_namespace_t));
    
    ns->namespace_id = strdup(namespace_id);
    ns->ns_name = strdup(config->ns_name);
    ns->veth_host = strdup(config->veth_host_name);
    ns->veth_ns = strdup(config->veth_container_name);
    ns->container_ip = strdup(config->container_ip);
    ns->container_gateway = strdup(config->container_gateway);
    ns->subnet_prefix_len = config->subnet_prefix_len;
    ns->creation_time = time(NULL);
    ns->last_modified = time(NULL);
    
    // Build namespace path
    char ns_path[256];
    snprintf(ns_path, sizeof(ns_path), "/var/run/netns/%s", config->ns_name);
    ns->ns_path = strdup(ns_path);
    
    if (!ns->namespace_id || !ns->ns_name || !ns->ns_path ||
        !ns->veth_host || !ns->veth_ns || !ns->container_ip ||
        !ns->container_gateway) {
        // Cleanup on error
        free(ns->namespace_id);
        free(ns->ns_name);
        free(ns->ns_path);
        free(ns->veth_host);
        free(ns->veth_ns);
        free(ns->container_ip);
        free(ns->container_gateway);
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }
    
    if (pthread_mutex_init(&ns->mutex, NULL) != 0) {
        free(ns->namespace_id);
        free(ns->ns_name);
        free(ns->ns_path);
        free(ns->veth_host);
        free(ns->veth_ns);
        free(ns->container_ip);
        free(ns->container_gateway);
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }
    
    // In production, would:
    // 1. ip netns add <ns_name>
    // 2. Create veth pair
    // 3. Move one end to namespace
    // 4. Configure IP
    
    ns->created = true;
    ns->veth_created = true;
    ns->ip_configured = true;
    
    manager->namespace_count++;
    pthread_mutex_unlock(&manager->mutex);
    
    return true;
}

/**
 * Delete network namespace
 */
bool netns_manager_delete_namespace(netns_manager_t *manager,
                                    const char *namespace_id) {
    if (!manager || !namespace_id) {
        return false;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    for (uint32_t i = 0; i < manager->namespace_count; i++) {
        if (strcmp(manager->namespaces[i].namespace_id, namespace_id) == 0) {
            // In production, would:
            // 1. Disconnect any processes in namespace
            // 2. Remove veth pair
            // 3. ip netns del <ns_name>
            
            network_namespace_t *ns = &manager->namespaces[i];
            free(ns->namespace_id);
            free(ns->ns_name);
            free(ns->ns_path);
            free(ns->veth_host);
            free(ns->veth_ns);
            free(ns->container_ip);
            free(ns->container_gateway);
            pthread_mutex_destroy(&ns->mutex);
            
            // Shift remaining namespaces
            if (i < manager->namespace_count - 1) {
                memmove(&manager->namespaces[i], &manager->namespaces[i + 1],
                        sizeof(network_namespace_t) * (manager->namespace_count - i - 1));
            }
            
            manager->namespace_count--;
            pthread_mutex_unlock(&manager->mutex);
            return true;
        }
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return false;
}

/**
 * Get namespace by pod ID
 */
network_namespace_t* netns_manager_get_namespace(netns_manager_t *manager,
                                                 const char *namespace_id) {
    if (!manager || !namespace_id) {
        return NULL;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    for (uint32_t i = 0; i < manager->namespace_count; i++) {
        if (strcmp(manager->namespaces[i].namespace_id, namespace_id) == 0) {
            pthread_mutex_unlock(&manager->mutex);
            return &manager->namespaces[i];
        }
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return NULL;
}

/**
 * Get all namespaces
 */
network_namespace_t* netns_manager_get_all(netns_manager_t *manager,
                                           uint32_t *out_count) {
    if (!manager || !out_count) {
        return NULL;
    }
    
    pthread_mutex_lock(&manager->mutex);
    *out_count = manager->namespace_count;
    pthread_mutex_unlock(&manager->mutex);
    
    return manager->namespaces;
}

/**
 * Add interface to namespace
 */
bool netns_manager_add_interface(netns_manager_t *manager,
                                 const char *namespace_id,
                                 const char *interface_name) {
    if (!manager || !namespace_id || !interface_name) {
        return false;
    }
    
    network_namespace_t *ns = netns_manager_get_namespace(manager, namespace_id);
    if (!ns) {
        return false;
    }
    
    // In production, would:
    // ip link set <interface_name> netns <ns_path>
    
    pthread_mutex_lock(&ns->mutex);
    ns->veth_created = true;
    ns->last_modified = time(NULL);
    pthread_mutex_unlock(&ns->mutex);
    
    return true;
}

/**
 * Execute command in namespace context
 */
bool netns_manager_exec_in_namespace(netns_manager_t *manager,
                                     const char *namespace_id,
                                     const char *command) {
    if (!manager || !namespace_id || !command) {
        return false;
    }
    
    network_namespace_t *ns = netns_manager_get_namespace(manager, namespace_id);
    if (!ns) {
        return false;
    }
    
    // In production, would:
    // 1. Fork process
    // 2. Call setns() to enter namespace
    // 3. Execute command via system() or exec()
    // 4. Return exit status
    
    pthread_mutex_lock(&ns->mutex);
    ns->last_modified = time(NULL);
    pthread_mutex_unlock(&ns->mutex);
    
    return true;
}

/**
 * Configure IP address in namespace
 */
bool netns_manager_configure_ip(netns_manager_t *manager,
                                const char *namespace_id,
                                const char *ip_address,
                                const char *gateway_ip,
                                uint32_t prefix_len) {
    if (!manager || !namespace_id || !ip_address || !gateway_ip) {
        return false;
    }
    
    network_namespace_t *ns = netns_manager_get_namespace(manager, namespace_id);
    if (!ns) {
        return false;
    }
    
    // In production, would execute in namespace:
    // ip addr add <ip_address>/<prefix_len> dev <veth_ns>
    // ip route add default via <gateway_ip>
    
    pthread_mutex_lock(&ns->mutex);
    free(ns->container_ip);
    ns->container_ip = strdup(ip_address);
    free(ns->container_gateway);
    ns->container_gateway = strdup(gateway_ip);
    ns->subnet_prefix_len = prefix_len;
    ns->ip_configured = true;
    ns->last_modified = time(NULL);
    pthread_mutex_unlock(&ns->mutex);
    
    return true;
}

/**
 * Check if namespace exists
 */
bool netns_manager_namespace_exists(netns_manager_t *manager,
                                    const char *namespace_id) {
    if (!manager || !namespace_id) {
        return false;
    }
    
    return netns_manager_get_namespace(manager, namespace_id) != NULL;
}

/**
 * Get namespace statistics
 */
void netns_manager_get_stats(netns_manager_t *manager,
                             uint32_t *out_total,
                             uint32_t *out_configured) {
    if (!manager) return;
    
    pthread_mutex_lock(&manager->mutex);
    
    if (out_total) {
        *out_total = manager->namespace_count;
    }
    
    if (out_configured) {
        uint32_t configured = 0;
        for (uint32_t i = 0; i < manager->namespace_count; i++) {
            if (manager->namespaces[i].ip_configured) {
                configured++;
            }
        }
        *out_configured = configured;
    }
    
    pthread_mutex_unlock(&manager->mutex);
}

/**
 * Cleanup stale namespaces
 */
uint32_t netns_manager_cleanup_stale(netns_manager_t *manager,
                                     uint32_t stale_seconds) {
    if (!manager) {
        return 0;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    uint32_t cleaned = 0;
    time_t now = time(NULL);
    
    for (int i = (int)manager->namespace_count - 1; i >= 0; i--) {
        time_t age = now - manager->namespaces[i].last_modified;
        
        if (age > stale_seconds) {
            // Free resources
            network_namespace_t *ns = &manager->namespaces[i];
            free(ns->namespace_id);
            free(ns->ns_name);
            free(ns->ns_path);
            free(ns->veth_host);
            free(ns->veth_ns);
            free(ns->container_ip);
            free(ns->container_gateway);
            pthread_mutex_destroy(&ns->mutex);
            
            // Shift remaining
            if ((uint32_t)i < manager->namespace_count - 1) {
                memmove(&manager->namespaces[i], &manager->namespaces[i + 1],
                        sizeof(network_namespace_t) * (manager->namespace_count - i - 1));
            }
            
            manager->namespace_count--;
            cleaned++;
        }
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return cleaned;
}
