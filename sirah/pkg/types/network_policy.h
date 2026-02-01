#ifndef K8S_NETWORK_POLICY_H
#define K8S_NETWORK_POLICY_H

#include <types/common.h>
#include <stdbool.h>

// NetworkPolicy directions
typedef enum {
    K8S_NETWORK_DIRECTION_INGRESS = 0,
    K8S_NETWORK_DIRECTION_EGRESS = 1
} k8s_network_direction_t;

// CIDR block for traffic
typedef struct {
    char* cidr;           // e.g., "10.0.0.0/8"
    char* except;         // Optional exception CIDR
} k8s_network_cidr_block_t;

// Label selector for pod/namespace matching
typedef struct {
    char** match_labels;   // Array of "key=value" pairs
    int num_labels;
    char** match_expressions;  // Array of expressions
    int num_expressions;
} k8s_network_label_selector_t;

// Pod selector
typedef struct {
    k8s_network_label_selector_t pod_selector;
} k8s_network_pod_selector_t;

// Namespace selector
typedef struct {
    k8s_network_label_selector_t ns_selector;
} k8s_network_ns_selector_t;

// Network policy peer (source/destination)
typedef struct {
    k8s_network_pod_selector_t* pod_selector;
    k8s_network_ns_selector_t* ns_selector;
    k8s_network_cidr_block_t* cidr_block;
} k8s_network_peer_t;

// Port specification
typedef struct {
    char* protocol;  // "TCP", "UDP"
    int port;
} k8s_network_port_t;

// Network policy rule (ingress or egress)
typedef struct {
    k8s_network_peer_t** peers;     // Array of peers
    int num_peers;
    k8s_network_port_t** ports;     // Array of ports
    int num_ports;
} k8s_network_rule_t;

// NetworkPolicy specification
typedef struct {
    k8s_network_pod_selector_t pod_selector;
    k8s_network_rule_t** ingress_rules;
    int num_ingress;
    k8s_network_rule_t** egress_rules;
    int num_egress;
    char** policy_types;  // "Ingress", "Egress"
    int num_policy_types;
} k8s_network_policy_spec_t;

// NetworkPolicy status
typedef struct {
    int num_observed_generation;
    char** conditions;  // Status conditions
    int num_conditions;
} k8s_network_policy_status_t;

// Main NetworkPolicy object
typedef struct {
    k8s_metadata_t* metadata;
    k8s_network_policy_spec_t* spec;
    k8s_network_policy_status_t* status;
} k8s_network_policy_t;

// Custom resource for network policy instance
typedef struct {
    k8s_metadata_t* metadata;
    char* policy_json;  // Serialized policy definition
    char* status_json;  // Status information
    bool enabled;       // Whether policy is enforced
} k8s_network_policy_resource_t;

// ============ NetworkPolicy Functions ============

k8s_network_policy_t* k8s_network_policy_new(const char* name, const char* namespace);
void k8s_network_policy_free(k8s_network_policy_t* policy);

// Spec functions
k8s_network_policy_spec_t* k8s_network_policy_spec_new();
void k8s_network_policy_spec_free(k8s_network_policy_spec_t* spec);

// Label selector functions
k8s_network_label_selector_t* k8s_network_label_selector_new();
void k8s_network_label_selector_free(k8s_network_label_selector_t* selector);
int k8s_network_label_selector_add_label(k8s_network_label_selector_t* selector, const char* key, const char* value);
bool k8s_network_label_selector_matches(k8s_network_label_selector_t* selector, const char* labels_json);

// Peer functions
k8s_network_peer_t* k8s_network_peer_new();
void k8s_network_peer_free(k8s_network_peer_t* peer);
int k8s_network_peer_set_pod_selector(k8s_network_peer_t* peer, k8s_network_pod_selector_t* selector);
int k8s_network_peer_set_ns_selector(k8s_network_peer_t* peer, k8s_network_ns_selector_t* selector);
int k8s_network_peer_set_cidr(k8s_network_peer_t* peer, const char* cidr);

// Rule functions
k8s_network_rule_t* k8s_network_rule_new();
void k8s_network_rule_free(k8s_network_rule_t* rule);
int k8s_network_rule_add_peer(k8s_network_rule_t* rule, k8s_network_peer_t* peer);
int k8s_network_rule_add_port(k8s_network_rule_t* rule, const char* protocol, int port);

// Spec configuration
int k8s_network_policy_add_ingress_rule(k8s_network_policy_spec_t* spec, k8s_network_rule_t* rule);
int k8s_network_policy_add_egress_rule(k8s_network_policy_spec_t* spec, k8s_network_rule_t* rule);
int k8s_network_policy_add_policy_type(k8s_network_policy_spec_t* spec, const char* type);

// Serialization
char* k8s_network_policy_to_json(k8s_network_policy_t* policy);
k8s_network_policy_t* k8s_network_policy_from_json(const char* json);

// Policy enforcement
bool k8s_network_policy_allows_traffic(k8s_network_policy_t* policy,
                                       const char* direction,
                                       const char* source_pod,
                                       const char* source_ns,
                                       const char* source_ip,
                                       const char* dest_pod,
                                       const char* dest_ns,
                                       const char* dest_ip,
                                       int port,
                                       const char* protocol);

#endif
