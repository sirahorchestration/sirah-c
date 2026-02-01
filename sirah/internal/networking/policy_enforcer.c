#include "policy_enforcer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Global policy enforcer instance
static k8s_policy_enforcer_t* g_policy_enforcer = NULL;

// ============ Traffic Flow ============

k8s_traffic_flow_t* k8s_traffic_flow_new(const char* source_pod,
                                         const char* source_ns,
                                         const char* source_ip,
                                         const char* dest_pod,
                                         const char* dest_ns,
                                         const char* dest_ip,
                                         int port,
                                         const char* protocol,
                                         const char* direction) {
    if (!source_pod || !source_ns || !dest_pod || !dest_ns || !protocol || !direction) {
        return NULL;
    }
    
    if (port <= 0 || port > 65535) {
        return NULL;
    }
    
    k8s_traffic_flow_t* flow = calloc(1, sizeof(k8s_traffic_flow_t));
    if (!flow) return NULL;
    
    flow->source_pod = malloc(strlen(source_pod) + 1);
    flow->source_namespace = malloc(strlen(source_ns) + 1);
    flow->dest_pod = malloc(strlen(dest_pod) + 1);
    flow->dest_namespace = malloc(strlen(dest_ns) + 1);
    flow->protocol = malloc(strlen(protocol) + 1);
    flow->direction = malloc(strlen(direction) + 1);
    
    if (!flow->source_pod || !flow->source_namespace || !flow->dest_pod ||
        !flow->dest_namespace || !flow->protocol || !flow->direction) {
        k8s_traffic_flow_free(flow);
        return NULL;
    }
    
    strcpy(flow->source_pod, source_pod);
    strcpy(flow->source_namespace, source_ns);
    strcpy(flow->dest_pod, dest_pod);
    strcpy(flow->dest_namespace, dest_ns);
    strcpy(flow->protocol, protocol);
    strcpy(flow->direction, direction);
    
    if (source_ip) {
        flow->source_ip = malloc(strlen(source_ip) + 1);
        if (flow->source_ip) strcpy(flow->source_ip, source_ip);
    }
    
    if (dest_ip) {
        flow->dest_ip = malloc(strlen(dest_ip) + 1);
        if (flow->dest_ip) strcpy(flow->dest_ip, dest_ip);
    }
    
    flow->port = port;
    return flow;
}

void k8s_traffic_flow_free(k8s_traffic_flow_t* flow) {
    if (!flow) return;
    
    free(flow->source_pod);
    free(flow->source_namespace);
    free(flow->source_ip);
    free(flow->dest_pod);
    free(flow->dest_namespace);
    free(flow->dest_ip);
    free(flow->protocol);
    free(flow->direction);
    free(flow->source_labels_json);
    free(flow->dest_labels_json);
    
    free(flow);
}

int k8s_traffic_flow_set_labels(k8s_traffic_flow_t* flow,
                                bool is_source,
                                const char* labels_json) {
    if (!flow || !labels_json) return -1;
    
    if (is_source) {
        if (flow->source_labels_json) free(flow->source_labels_json);
        flow->source_labels_json = malloc(strlen(labels_json) + 1);
        if (!flow->source_labels_json) return -1;
        strcpy(flow->source_labels_json, labels_json);
    } else {
        if (flow->dest_labels_json) free(flow->dest_labels_json);
        flow->dest_labels_json = malloc(strlen(labels_json) + 1);
        if (!flow->dest_labels_json) return -1;
        strcpy(flow->dest_labels_json, labels_json);
    }
    
    return 0;
}

// ============ Enforcer Initialization ============

k8s_policy_enforcer_t* k8s_policy_enforcer_new() {
    k8s_policy_enforcer_t* enforcer = calloc(1, sizeof(k8s_policy_enforcer_t));
    if (!enforcer) return NULL;
    
    enforcer->num_policies = 0;
    enforcer->default_deny_ingress = false;
    enforcer->default_deny_egress = false;
    
    return enforcer;
}

void k8s_policy_enforcer_free(k8s_policy_enforcer_t* enforcer) {
    if (!enforcer) return;
    
    for (int i = 0; i < enforcer->num_policies; i++) {
        if (enforcer->policies[i]) {
            k8s_network_policy_free(enforcer->policies[i]);
        }
    }
    free(enforcer->policies);
    
    free(enforcer);
}

// ============ Policy Management ============

int k8s_policy_enforcer_add_policy(k8s_policy_enforcer_t* enforcer, k8s_network_policy_t* policy) {
    if (!enforcer || !policy) return -1;
    
    k8s_network_policy_t** new_policies = realloc(enforcer->policies,
                                                   (enforcer->num_policies + 1) * sizeof(k8s_network_policy_t*));
    if (!new_policies) return -1;
    
    new_policies[enforcer->num_policies] = policy;
    enforcer->policies = new_policies;
    enforcer->num_policies++;
    
    return 0;
}

int k8s_policy_enforcer_remove_policy(k8s_policy_enforcer_t* enforcer,
                                      const char* name,
                                      const char* namespace) {
    if (!enforcer || !name || !namespace) return -1;
    
    for (int i = 0; i < enforcer->num_policies; i++) {
        k8s_network_policy_t* policy = enforcer->policies[i];
        if (policy && policy->metadata &&
            strcmp(policy->metadata->name, name) == 0 &&
            strcmp(policy->metadata->namespace, namespace) == 0) {
            
            k8s_network_policy_free(policy);
            
            for (int j = i; j < enforcer->num_policies - 1; j++) {
                enforcer->policies[j] = enforcer->policies[j + 1];
            }
            enforcer->num_policies--;
            return 0;
        }
    }
    
    return -1;
}

int k8s_policy_enforcer_update_policy(k8s_policy_enforcer_t* enforcer, k8s_network_policy_t* policy) {
    if (!enforcer || !policy || !policy->metadata) return -1;
    
    for (int i = 0; i < enforcer->num_policies; i++) {
        k8s_network_policy_t* existing = enforcer->policies[i];
        if (existing && existing->metadata &&
            strcmp(existing->metadata->name, policy->metadata->name) == 0 &&
            strcmp(existing->metadata->namespace, policy->metadata->namespace) == 0) {
            
            k8s_network_policy_free(existing);
            enforcer->policies[i] = policy;
            return 0;
        }
    }
    
    return -1;  // Policy not found
}

// ============ Policy Evaluation ============

static bool k8s_cidr_contains_ip(const char* cidr, const char* ip) {
    if (!cidr || !ip) return false;
    
    // TODO: Implement proper CIDR matching
    // For now, use simple prefix matching
    char* slash = strchr(cidr, '/');
    if (!slash) return strcmp(cidr, ip) == 0;
    
    int prefix_len = atoi(slash + 1);
    int cidr_parts = slash - cidr;
    
    // Simple check: if IPs start with same prefix, allow
    return strncmp(cidr, ip, cidr_parts) == 0;
}

static bool k8s_policy_matches_pod(k8s_network_policy_t* policy,
                                   k8s_traffic_flow_t* flow,
                                   bool is_source) {
    if (!policy || !flow) return false;
    
    // Get the pod and namespace we're checking
    const char* check_pod = is_source ? flow->source_pod : flow->dest_pod;
    const char* check_ns = is_source ? flow->source_namespace : flow->dest_namespace;
    const char* labels = is_source ? flow->source_labels_json : flow->dest_labels_json;
    
    // Policy's pod selector applies to traffic endpoint
    if (strcmp(check_ns, policy->metadata->namespace) != 0) {
        return false;
    }
    
    // Match pod selector labels
    if (labels) {
        return k8s_network_label_selector_matches(&policy->spec->pod_selector.pod_selector, labels);
    }
    
    return false;
}

static bool k8s_rule_allows_traffic(k8s_network_rule_t* rule, k8s_traffic_flow_t* flow) {
    if (!rule || !flow) return true;  // No rule = allow
    
    // Check port
    if (rule->num_ports > 0) {
        bool port_match = false;
        for (int i = 0; i < rule->num_ports; i++) {
            if (rule->ports[i] &&
                rule->ports[i]->port == flow->port &&
                strcmp(rule->ports[i]->protocol, flow->protocol) == 0) {
                port_match = true;
                break;
            }
        }
        if (!port_match) return false;
    }
    
    // Check peers (source or destination)
    if (rule->num_peers == 0) {
        return true;  // No peer restrictions
    }
    
    for (int i = 0; i < rule->num_peers; i++) {
        k8s_network_peer_t* peer = rule->peers[i];
        if (!peer) continue;
        
        // Check CIDR block
        if (peer->cidr_block) {
            const char* check_ip = strcmp(flow->direction, "ingress") == 0 ?
                                   flow->source_ip : flow->dest_ip;
            if (check_ip && k8s_cidr_contains_ip(peer->cidr_block->cidr, check_ip)) {
                return true;
            }
        }
        
        // Check pod selector
        if (peer->pod_selector) {
            const char* labels = strcmp(flow->direction, "ingress") == 0 ?
                                 flow->source_labels_json : flow->dest_labels_json;
            if (labels && k8s_network_label_selector_matches(&peer->pod_selector->pod_selector, labels)) {
                return true;
            }
        }
    }
    
    return false;
}

k8s_policy_result_t k8s_policy_enforcer_evaluate(k8s_policy_enforcer_t* enforcer,
                                                 k8s_traffic_flow_t* flow) {
    if (!enforcer || !flow) return K8S_POLICY_NO_POLICY;
    
    bool policy_found = false;
    bool allowed = false;
    
    // Check each policy
    for (int i = 0; i < enforcer->num_policies; i++) {
        k8s_network_policy_t* policy = enforcer->policies[i];
        if (!policy || !policy->spec) continue;
        
        // Check if policy applies to this pod
        if (!k8s_policy_matches_pod(policy, flow, false)) {
            continue;
        }
        
        policy_found = true;
        
        // Determine which rules apply (ingress or egress)
        k8s_network_rule_t** rules = NULL;
        int num_rules = 0;
        
        if (strcmp(flow->direction, "ingress") == 0) {
            rules = policy->spec->ingress_rules;
            num_rules = policy->spec->num_ingress;
        } else if (strcmp(flow->direction, "egress") == 0) {
            rules = policy->spec->egress_rules;
            num_rules = policy->spec->num_egress;
        }
        
        // Check if any rule allows this traffic
        for (int j = 0; j < num_rules; j++) {
            if (k8s_rule_allows_traffic(rules[j], flow)) {
                allowed = true;
                break;
            }
        }
        
        if (allowed) break;
    }
    
    if (policy_found) {
        return allowed ? K8S_POLICY_ALLOW : K8S_POLICY_DENY;
    }
    
    // No policies found - use default deny rules
    if (strcmp(flow->direction, "ingress") == 0 && enforcer->default_deny_ingress) {
        return K8S_POLICY_DENY;
    }
    if (strcmp(flow->direction, "egress") == 0 && enforcer->default_deny_egress) {
        return K8S_POLICY_DENY;
    }
    
    return K8S_POLICY_ALLOW;
}

bool k8s_policy_enforcer_matches_policy(k8s_policy_enforcer_t* enforcer,
                                        k8s_network_policy_t* policy,
                                        k8s_traffic_flow_t* flow) {
    if (!enforcer || !policy || !flow) return false;
    
    return k8s_policy_matches_pod(policy, flow, false) &&
           k8s_rule_allows_traffic(NULL, flow);
}

// ============ Global Enforcer ============

k8s_policy_enforcer_t* k8s_policy_enforcer_global() {
    if (!g_policy_enforcer) {
        g_policy_enforcer = k8s_policy_enforcer_new();
    }
    return g_policy_enforcer;
}
