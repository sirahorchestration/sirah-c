#include "load_balancer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

// Simple hash function for IP addresses
static unsigned int hash_ip(const char* ip) {
    if (!ip) return 0;
    
    unsigned int hash = 0;
    for (int i = 0; ip[i]; i++) {
        hash = hash * 31 + ip[i];
    }
    return hash;
}

load_balancer_t* load_balancer_new(const char* service_name, const char* namespace,
                                   lb_strategy_t strategy) {
    if (!service_name || !namespace) return NULL;
    
    load_balancer_t* lb = (load_balancer_t*)malloc(sizeof(load_balancer_t));
    strcpy(lb->service_name, service_name);
    strcpy(lb->namespace, namespace);
    
    lb->endpoints = (lb_endpoint_t*)malloc(sizeof(lb_endpoint_t) * 100);
    lb->endpoint_count = 0;
    
    lb->strategy = strategy;
    lb->round_robin_index = 0;
    lb->session_timeout = 10800;  // 3 hours
    
    return lb;
}

void load_balancer_free(load_balancer_t* lb) {
    if (!lb) return;
    
    for (int i = 0; i < lb->endpoint_count; i++) {
        free(lb->endpoints[i].endpoint.pod_ip);
        free(lb->endpoints[i].endpoint.pod_name);
        if (lb->endpoints[i].endpoint.node_name) {
            free(lb->endpoints[i].endpoint.node_name);
        }
        if (lb->endpoints[i].endpoint.hostname) {
            free(lb->endpoints[i].endpoint.hostname);
        }
    }
    
    free(lb->endpoints);
    free(lb);
}

int load_balancer_add_endpoint(load_balancer_t* lb, const char* pod_ip, 
                               const char* pod_name) {
    if (!lb || !pod_ip || !pod_name) return -1;
    if (lb->endpoint_count >= 100) return -1;
    
    // Check if endpoint already exists
    for (int i = 0; i < lb->endpoint_count; i++) {
        if (strcmp(lb->endpoints[i].endpoint.pod_ip, pod_ip) == 0) {
            return 0;  // Already exists
        }
    }
    
    // Add new endpoint
    int idx = lb->endpoint_count;
    lb->endpoints[idx].endpoint.pod_ip = strdup(pod_ip);
    lb->endpoints[idx].endpoint.pod_name = strdup(pod_name);
    lb->endpoints[idx].endpoint.ready = 1;
    lb->endpoints[idx].connection_count = 0;
    lb->endpoints[idx].last_used = time(NULL);
    
    lb->endpoint_count++;
    
    fprintf(stderr, "[load-balancer] Added endpoint %s (%s) to %s/%s\n",
            pod_ip, pod_name, lb->namespace, lb->service_name);
    
    return 0;
}

int load_balancer_remove_endpoint(load_balancer_t* lb, const char* pod_ip) {
    if (!lb || !pod_ip) return -1;
    
    for (int i = 0; i < lb->endpoint_count; i++) {
        if (strcmp(lb->endpoints[i].endpoint.pod_ip, pod_ip) == 0) {
            free(lb->endpoints[i].endpoint.pod_ip);
            free(lb->endpoints[i].endpoint.pod_name);
            if (lb->endpoints[i].endpoint.node_name) {
                free(lb->endpoints[i].endpoint.node_name);
            }
            
            // Shift remaining endpoints
            memmove(&lb->endpoints[i], &lb->endpoints[i + 1],
                   sizeof(lb_endpoint_t) * (lb->endpoint_count - i - 1));
            lb->endpoint_count--;
            
            fprintf(stderr, "[load-balancer] Removed endpoint %s from %s/%s\n",
                    pod_ip, lb->namespace, lb->service_name);
            
            return 0;
        }
    }
    
    return -1;
}

k8s_endpoint_t* load_balancer_select_endpoint(load_balancer_t* lb, const char* client_ip) {
    if (!lb || lb->endpoint_count == 0) return NULL;
    
    int selected = -1;
    
    switch (lb->strategy) {
        case LB_ROUND_ROBIN: {
            // Simple round robin
            selected = lb->round_robin_index % lb->endpoint_count;
            lb->round_robin_index++;
            break;
        }
        
        case LB_LEAST_CONN: {
            // Select endpoint with least connections
            int min_conn = INT_MAX;
            selected = 0;
            for (int i = 0; i < lb->endpoint_count; i++) {
                if (lb->endpoints[i].connection_count < min_conn) {
                    min_conn = lb->endpoints[i].connection_count;
                    selected = i;
                }
            }
            break;
        }
        
        case LB_IP_HASH:
        case LB_CLIENT_IP: {
            // Hash-based selection
            if (client_ip) {
                unsigned int hash = hash_ip(client_ip);
                selected = hash % lb->endpoint_count;
            } else {
                selected = lb->round_robin_index % lb->endpoint_count;
                lb->round_robin_index++;
            }
            break;
        }
        
        default:
            selected = 0;
    }
    
    if (selected >= 0 && selected < lb->endpoint_count) {
        lb->endpoints[selected].last_used = time(NULL);
        return &lb->endpoints[selected].endpoint;
    }
    
    return NULL;
}

int load_balancer_endpoint_connected(load_balancer_t* lb, const char* pod_ip) {
    if (!lb || !pod_ip) return -1;
    
    for (int i = 0; i < lb->endpoint_count; i++) {
        if (strcmp(lb->endpoints[i].endpoint.pod_ip, pod_ip) == 0) {
            lb->endpoints[i].connection_count++;
            return 0;
        }
    }
    
    return -1;
}

int load_balancer_endpoint_disconnected(load_balancer_t* lb, const char* pod_ip) {
    if (!lb || !pod_ip) return -1;
    
    for (int i = 0; i < lb->endpoint_count; i++) {
        if (strcmp(lb->endpoints[i].endpoint.pod_ip, pod_ip) == 0) {
            if (lb->endpoints[i].connection_count > 0) {
                lb->endpoints[i].connection_count--;
            }
            return 0;
        }
    }
    
    return -1;
}

void load_balancer_clear_endpoints(load_balancer_t* lb) {
    if (!lb) return;
    
    for (int i = 0; i < lb->endpoint_count; i++) {
        free(lb->endpoints[i].endpoint.pod_ip);
        free(lb->endpoints[i].endpoint.pod_name);
        if (lb->endpoints[i].endpoint.node_name) {
            free(lb->endpoints[i].endpoint.node_name);
        }
    }
    
    lb->endpoint_count = 0;
}

int load_balancer_get_endpoint_count(load_balancer_t* lb) {
    if (!lb) return 0;
    return lb->endpoint_count;
}
