#ifndef K8S_POLICY_ENFORCER_H
#define K8S_POLICY_ENFORCER_H

#include <types/network_policy.h>
#include <stdbool.h>

// Policy enforcement result
typedef enum {
    K8S_POLICY_ALLOW = 0,
    K8S_POLICY_DENY = 1,
    K8S_POLICY_NO_POLICY = 2
} k8s_policy_result_t;

// Traffic flow context
typedef struct {
    char* source_pod;
    char* source_namespace;
    char* source_ip;
    char* source_labels_json;
    char* dest_pod;
    char* dest_namespace;
    char* dest_ip;
    char* dest_labels_json;
    int port;
    char* protocol;
    char* direction;  // "ingress" or "egress"
} k8s_traffic_flow_t;

// Policy enforcer engine
typedef struct {
    k8s_network_policy_t** policies;
    int num_policies;
    bool default_deny_ingress;
    bool default_deny_egress;
} k8s_policy_enforcer_t;

// ============ Enforcer Creation/Destruction ============

k8s_policy_enforcer_t* k8s_policy_enforcer_new();
void k8s_policy_enforcer_free(k8s_policy_enforcer_t* enforcer);

// ============ Policy Management ============

int k8s_policy_enforcer_add_policy(k8s_policy_enforcer_t* enforcer, k8s_network_policy_t* policy);
int k8s_policy_enforcer_remove_policy(k8s_policy_enforcer_t* enforcer, const char* name, const char* namespace);
int k8s_policy_enforcer_update_policy(k8s_policy_enforcer_t* enforcer, k8s_network_policy_t* policy);

// ============ Traffic Evaluation ============

k8s_traffic_flow_t* k8s_traffic_flow_new(const char* source_pod,
                                         const char* source_ns,
                                         const char* source_ip,
                                         const char* dest_pod,
                                         const char* dest_ns,
                                         const char* dest_ip,
                                         int port,
                                         const char* protocol,
                                         const char* direction);

void k8s_traffic_flow_free(k8s_traffic_flow_t* flow);

int k8s_traffic_flow_set_labels(k8s_traffic_flow_t* flow,
                                bool is_source,
                                const char* labels_json);

// Evaluate if traffic is allowed by policies
k8s_policy_result_t k8s_policy_enforcer_evaluate(k8s_policy_enforcer_t* enforcer,
                                                 k8s_traffic_flow_t* flow);

// Check if traffic passes specific policy
bool k8s_policy_enforcer_matches_policy(k8s_policy_enforcer_t* enforcer,
                                        k8s_network_policy_t* policy,
                                        k8s_traffic_flow_t* flow);

// ============ Global Enforcer ============

k8s_policy_enforcer_t* k8s_policy_enforcer_global();

#endif
