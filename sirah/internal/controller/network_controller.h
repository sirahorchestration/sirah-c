// internal/controller/network_controller.h
// Network Controller - Manages network resources, IP allocation, VIP routing, and network policies
// Implements IPAM (IP Address Management), virtual IP routing, and NetworkPolicy enforcement
// Watches Services, Pods, and NetworkPolicy objects to manage cluster networking

#ifndef SIRAH_NETWORK_CONTROLLER_H
#define SIRAH_NETWORK_CONTROLLER_H

#include <time.h>
#include <json-c/json.h>

// ============ Type Definitions ============

// IP allocation ranges
typedef struct {
    char cluster_cidr[32];         // e.g., "10.0.0.0/8"
    char service_cidr[32];         // e.g., "10.96.0.0/12"
    char pod_cidr[32];             // e.g., "10.0.0.0/8"
    unsigned int service_ip_start;  // e.g., 10.96.0.1 as uint32
    unsigned int service_ip_end;    // e.g., 10.111.255.255 as uint32
} ipam_config_t;

// Allocated service IP (virtual IP)
typedef struct {
    char service_name[256];
    char namespace[256];
    unsigned int cluster_ip;        // Virtual IP address as uint32
    char cluster_ip_str[16];        // String representation "10.x.x.x"
    int port;                       // Service port
    char protocol[8];               // "TCP", "UDP"
    int in_use;                     // 1 if allocated, 0 if free
    time_t allocated_at;
} service_ip_t;

// Virtual route entry
typedef struct {
    unsigned int vip;               // Virtual IP as uint32
    char vip_str[16];              // String representation
    char pod_ips[32][16];          // Array of pod IPs backing this VIP
    int num_pods;                  // Number of pods
    int current_pod_index;         // For round-robin selection
    time_t updated_at;
} virtual_route_t;

// Network policy rule (ingress or egress)
typedef struct {
    char** from_namespaces;        // Allowed source namespaces
    int num_from_namespaces;
    
    char** from_pod_labels;        // Allowed source pod labels (key=value)
    int num_from_pod_labels;
    
    int* from_ports;               // Allowed source ports
    int num_from_ports;
    
    char** to_namespaces;          // Allowed dest namespaces
    int num_to_namespaces;
    
    char** to_pod_labels;          // Allowed dest pod labels (key=value)
    int num_to_pod_labels;
    
    int* to_ports;                 // Allowed dest ports
    int num_to_ports;
    
    char protocol[8];              // "TCP", "UDP"
} network_policy_rule_t;

// Cached network policy
typedef struct {
    char name[256];
    char namespace[256];
    
    // Pod selector for this policy
    char** pod_selector_labels;    // Target pod labels (key=value)
    int num_pod_selector_labels;
    
    // Ingress rules
    network_policy_rule_t* ingress_rules;
    int num_ingress_rules;
    
    // Egress rules
    network_policy_rule_t* egress_rules;
    int num_egress_rules;
    
    // Policy types: 1=Ingress, 2=Egress, 3=Both
    int policy_types;
    
    time_t cached_at;
} cached_network_policy_t;

// Network controller main structure
typedef struct {
    char* api_server_url;
    void* curl_handle;
    
    // IPAM state
    ipam_config_t ipam_config;
    service_ip_t* allocated_ips;
    int num_allocated_ips;
    int max_allocated_ips;
    
    // Virtual routing state
    virtual_route_t* routes;
    int num_routes;
    int max_routes;
    
    // Cached network policies
    cached_network_policy_t* policies;
    int num_policies;
    int max_policies;
    
    // Reconciliation intervals (seconds)
    int ip_allocation_interval;      // 10 seconds
    int route_update_interval;        // 5 seconds
    int policy_sync_interval;         // 10 seconds
    
    int running;
    int next_allocated_ip;            // Next IP to allocate (as index into range)
} network_controller_t;

// ============ Lifecycle Functions ============

// Create new network controller
network_controller_t* network_controller_new(const char* api_server_url);

// Free network controller
void network_controller_free(network_controller_t* controller);

// Initialize network controller
int network_controller_init(network_controller_t* controller);

// Shutdown network controller
void network_controller_shutdown(network_controller_t* controller);

// Run network controller (main loop - blocking)
int network_controller_run(network_controller_t* controller);

// ============ IP Allocation (IPAM) Functions ============

// Allocate a cluster IP for a service
// Returns 0 on success, -1 on failure (e.g., IP range exhausted)
int network_allocate_service_ip(network_controller_t* controller,
                               const char* service_name,
                               const char* namespace,
                               char* out_ip_str);  // Output: "10.x.x.x"

// Release a service IP back to the pool
int network_release_service_ip(network_controller_t* controller,
                              const char* service_name,
                              const char* namespace);

// Get allocated IP for service
int network_get_service_ip(network_controller_t* controller,
                          const char* service_name,
                          const char* namespace,
                          char* out_ip_str);

// Convert uint32 IP to string "a.b.c.d"
void network_uint32_to_ip_string(unsigned int ip, char* out_str);

// Convert string "a.b.c.d" to uint32 IP
unsigned int network_ip_string_to_uint32(const char* ip_str);

// ============ Virtual Routing Functions ============

// Update routes for a service (called when pods change)
int network_update_service_routes(network_controller_t* controller,
                                 const char* service_name,
                                 const char* namespace,
                                 const char* service_ip,
                                 char** pod_ips,
                                 int pod_count);

// Select next pod for VIP using round-robin
char* network_select_backend_pod(network_controller_t* controller,
                               const char* service_ip);

// ============ NetworkPolicy Enforcement Functions ============

// Sync network policies from API server
int network_sync_policies(network_controller_t* controller);

// Check if traffic is allowed by policies
// Returns 1 if allowed, 0 if denied
int network_is_traffic_allowed(network_controller_t* controller,
                              const char* source_pod_name,
                              const char* source_namespace,
                              const char* dest_pod_name,
                              const char* dest_namespace,
                              const char* protocol,
                              int dest_port);

// Check if pod matches label selector
// Compares pod labels against selector labels
// Returns 1 if all selector labels match pod labels, 0 otherwise
int network_pod_matches_labels(json_object* pod,
                              const char** selector_labels,
                              int num_selector_labels);

// Evaluate ingress rules for a pod
// Returns 1 if ingress is allowed, 0 if denied
int network_evaluate_ingress_rules(network_controller_t* controller,
                                  const char* dest_pod_name,
                                  const char* dest_namespace,
                                  const char* source_pod_name,
                                  const char* source_namespace,
                                  const char* protocol,
                                  int port);

// Evaluate egress rules for a pod
// Returns 1 if egress is allowed, 0 if denied
int network_evaluate_egress_rules(network_controller_t* controller,
                                 const char* source_pod_name,
                                 const char* source_namespace,
                                 const char* dest_pod_name,
                                 const char* dest_namespace,
                                 const char* protocol,
                                 int port);

// Get pod metadata from API
json_object* network_get_pod(network_controller_t* controller,
                            const char* pod_name,
                            const char* namespace);

// Get service metadata from API
json_object* network_get_service(network_controller_t* controller,
                                const char* service_name,
                                const char* namespace);

// Get all network policies in namespace
json_object* network_get_policies(network_controller_t* controller,
                                 const char* namespace);

#endif // SIRAH_NETWORK_CONTROLLER_H
