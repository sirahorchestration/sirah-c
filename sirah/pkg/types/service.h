#ifndef SIRAH_SERVICE_H
#define SIRAH_SERVICE_H

#include "common.h"

// ServiceType enum
typedef enum {
    SERVICE_CLUSTER_IP = 0,
    SERVICE_NODE_PORT = 1,
    SERVICE_LOAD_BALANCER = 2,
    SERVICE_EXTERNAL_NAME = 3
} k8s_service_type_t;

// ServicePort definition
typedef struct {
    char* name;
    int port;              // Service port
    int target_port;       // Pod port
    int node_port;         // NodePort (for NodePort services)
    char* protocol;        // "TCP", "UDP"
} k8s_service_port_t;

// Service spec
typedef struct {
    k8s_service_type_t type;
    char* cluster_ip;      // Assigned VIP
    char* external_ip;     // For LoadBalancer
    
    k8s_service_port_t* ports;
    int num_ports;
    
    char** selectors;      // Label selectors to match pods
    int num_selectors;
    
    char* session_affinity;  // "ClientIP", "None"
    int session_timeout;
} k8s_service_spec_t;

// Endpoint (pod that backs this service)
typedef struct {
    char* pod_ip;
    char* pod_name;
    char* node_name;
    char* hostname;
    int ready;
} k8s_endpoint_t;

// Service status
typedef struct {
    k8s_endpoint_t* endpoints;
    int num_endpoints;
    
    k8s_condition_t* conditions;
    int num_conditions;
} k8s_service_status_t;

// Full Service object
typedef struct {
    k8s_metadata_t metadata;
    k8s_service_spec_t spec;
    k8s_service_status_t status;
} k8s_service_t;

// Service operations
k8s_service_t* k8s_service_new(const char* name, const char* namespace);
void k8s_service_free(k8s_service_t* service);
char* k8s_service_to_json(k8s_service_t* service);
k8s_service_t* k8s_service_from_json(const char* json_str);

int k8s_service_add_port(k8s_service_t* svc, int port, int target_port);
int k8s_service_add_selector(k8s_service_t* svc, const char* key, const char* value);
int k8s_service_add_endpoint(k8s_service_t* svc, const char* pod_ip, const char* pod_name);

#endif
