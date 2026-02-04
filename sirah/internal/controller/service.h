// internal/controller/service.h
// Service Controller - Manages Service objects and endpoint discovery
// Watches Services and Pods, discovers endpoints matching selectors,
// implements load balancing strategies, and updates endpoints dynamically

#ifndef SIRAH_SERVICE_CONTROLLER_H
#define SIRAH_SERVICE_CONTROLLER_H

#include "../../pkg/types/service.h"

typedef struct {
    char name[256];
    char namespace[256];
    char service_type[32];        // "ClusterIP", "NodePort", "LoadBalancer"
    char cluster_ip[16];          // e.g., "10.0.0.1"
    int node_port;                // For NodePort type
    char* selector_json;          // JSON representation of selector labels
    int target_port;
} service_endpoint_config_t;

typedef struct endpoint {
    char pod_ip[16];              // Pod IP address
    char pod_name[256];           // Pod name
    char pod_namespace[256];      // Pod namespace
    int port;                     // Container port
    int ready;                    // 1 if pod is ready, 0 otherwise
    time_t added_at;             // When endpoint was added
} endpoint_t;

typedef struct {
    char* api_server_url;
    void* curl_handle;
    int update_interval;
    int load_balance_strategy;    // 0=round-robin, 1=least-connections, 2=session-affinity
} service_controller_t;

// Load balancing strategies
#define LB_ROUND_ROBIN          0
#define LB_LEAST_CONNECTIONS    1
#define LB_SESSION_AFFINITY     2

// Create new service controller
service_controller_t* service_controller_new(const char* api_server_url);

// Free service controller
void service_controller_free(service_controller_t* controller);

// Initialize service controller
int service_controller_init(service_controller_t* controller);

// Shutdown service controller
void service_controller_shutdown(service_controller_t* controller);

// Run service controller (main loop)
int service_controller_run(service_controller_t* controller);

// Discover endpoints for a service (pods matching selector)
// Returns array of endpoint_t, caller must free
endpoint_t** service_discover_endpoints(service_controller_t* controller,
                                       const char* namespace, const char* service_name,
                                       json_object* selector, int* out_count);

// Update endpoints for a service in API server
int service_update_endpoints(service_controller_t* controller,
                            const char* namespace, const char* service_name,
                            endpoint_t** endpoints, int endpoint_count);

// Select endpoint using load balancing strategy
endpoint_t* service_select_endpoint(endpoint_t** endpoints, int count, 
                                   int strategy);

// Check if pod matches service selector
int service_pod_matches_selector(json_object* pod, json_object* selector);

// Get service details from API
json_object* service_get_service(service_controller_t* controller,
                                const char* namespace, const char* service_name);

// Get all pods matching labels
json_object* service_get_pods_by_labels(service_controller_t* controller,
                                       const char* namespace, json_object* labels);

#endif // SIRAH_SERVICE_CONTROLLER_H
