#ifndef SIRAH_LOAD_BALANCER_H
#define SIRAH_LOAD_BALANCER_H

#include "../types/service.h"

// Load balancing strategy
typedef enum {
    LB_ROUND_ROBIN = 0,    // Cycle through endpoints
    LB_LEAST_CONN = 1,     // Least connections
    LB_IP_HASH = 2,        // Hash of client IP
    LB_CLIENT_IP = 3       // Session affinity by client IP
} lb_strategy_t;

// Endpoint with load balancing metadata
typedef struct {
    k8s_endpoint_t endpoint;
    int connection_count;   // For least_conn strategy
    time_t last_used;       // For round robin
} lb_endpoint_t;

// Load balancer instance
typedef struct {
    char service_name[256];
    char namespace[64];
    
    lb_endpoint_t* endpoints;
    int endpoint_count;
    
    lb_strategy_t strategy;
    int round_robin_index;  // Current index for round robin
    
    int session_timeout;    // For session affinity
} load_balancer_t;

// Operations
load_balancer_t* load_balancer_new(const char* service_name, const char* namespace,
                                   lb_strategy_t strategy);
void load_balancer_free(load_balancer_t* lb);

// Add endpoint to load balancer
int load_balancer_add_endpoint(load_balancer_t* lb, const char* pod_ip, 
                               const char* pod_name);

// Remove endpoint from load balancer
int load_balancer_remove_endpoint(load_balancer_t* lb, const char* pod_ip);

// Select endpoint based on strategy
// client_ip: optional, used for IP hash and client IP strategies
k8s_endpoint_t* load_balancer_select_endpoint(load_balancer_t* lb, const char* client_ip);

// Update endpoint connection count (for least_conn strategy)
int load_balancer_endpoint_connected(load_balancer_t* lb, const char* pod_ip);
int load_balancer_endpoint_disconnected(load_balancer_t* lb, const char* pod_ip);

// Clear all endpoints
void load_balancer_clear_endpoints(load_balancer_t* lb);

// Get endpoint count
int load_balancer_get_endpoint_count(load_balancer_t* lb);

#endif
