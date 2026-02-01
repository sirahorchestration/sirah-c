#include <types/network_policy.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <json-c/json.h>

// ============ Helper Functions ============

static int add_string_to_array(char*** array, int* count, const char* value) {
    if (!array || !count || !value) return -1;
    
    char** new_array = realloc(*array, (*count + 1) * sizeof(char*));
    if (!new_array) return -1;
    
    new_array[*count] = malloc(strlen(value) + 1);
    if (!new_array[*count]) {
        free(new_array);
        return -1;
    }
    
    strcpy(new_array[*count], value);
    *array = new_array;
    (*count)++;
    return 0;
}

// ============ Label Selector ============

k8s_network_label_selector_t* k8s_network_label_selector_new() {
    k8s_network_label_selector_t* selector = calloc(1, sizeof(k8s_network_label_selector_t));
    if (!selector) return NULL;
    
    selector->num_labels = 0;
    selector->num_expressions = 0;
    return selector;
}

void k8s_network_label_selector_free(k8s_network_label_selector_t* selector) {
    if (!selector) return;
    
    for (int i = 0; i < selector->num_labels; i++) {
        free(selector->match_labels[i]);
    }
    free(selector->match_labels);
    
    for (int i = 0; i < selector->num_expressions; i++) {
        free(selector->match_expressions[i]);
    }
    free(selector->match_expressions);
    
    free(selector);
}

int k8s_network_label_selector_add_label(k8s_network_label_selector_t* selector,
                                         const char* key,
                                         const char* value) {
    if (!selector || !key || !value) return -1;
    
    char label[256];
    snprintf(label, sizeof(label), "%s=%s", key, value);
    
    return add_string_to_array(&selector->match_labels, &selector->num_labels, label);
}

bool k8s_network_label_selector_matches(k8s_network_label_selector_t* selector, const char* labels_json) {
    if (!selector || !labels_json) return false;
    
    // Parse labels from JSON and check against selectors
    // TODO: Implement full label matching logic with expressions
    // For now, accept all labels (permissive mode)
    return true;
}

// ============ Peer ============

k8s_network_peer_t* k8s_network_peer_new() {
    k8s_network_peer_t* peer = calloc(1, sizeof(k8s_network_peer_t));
    if (!peer) return NULL;
    
    return peer;
}

void k8s_network_peer_free(k8s_network_peer_t* peer) {
    if (!peer) return;
    
    if (peer->pod_selector) {
        k8s_network_label_selector_free(&peer->pod_selector->pod_selector);
        free(peer->pod_selector);
    }
    
    if (peer->ns_selector) {
        k8s_network_label_selector_free(&peer->ns_selector->ns_selector);
        free(peer->ns_selector);
    }
    
    if (peer->cidr_block) {
        free(peer->cidr_block->cidr);
        free(peer->cidr_block->except);
        free(peer->cidr_block);
    }
    
    free(peer);
}

int k8s_network_peer_set_pod_selector(k8s_network_peer_t* peer, k8s_network_pod_selector_t* selector) {
    if (!peer || !selector) return -1;
    
    peer->pod_selector = selector;
    return 0;
}

int k8s_network_peer_set_ns_selector(k8s_network_peer_t* peer, k8s_network_ns_selector_t* selector) {
    if (!peer || !selector) return -1;
    
    peer->ns_selector = selector;
    return 0;
}

int k8s_network_peer_set_cidr(k8s_network_peer_t* peer, const char* cidr) {
    if (!peer || !cidr) return -1;
    
    peer->cidr_block = calloc(1, sizeof(k8s_network_cidr_block_t));
    if (!peer->cidr_block) return -1;
    
    peer->cidr_block->cidr = malloc(strlen(cidr) + 1);
    if (!peer->cidr_block->cidr) {
        free(peer->cidr_block);
        return -1;
    }
    
    strcpy(peer->cidr_block->cidr, cidr);
    return 0;
}

// ============ Rule ============

k8s_network_rule_t* k8s_network_rule_new() {
    k8s_network_rule_t* rule = calloc(1, sizeof(k8s_network_rule_t));
    if (!rule) return NULL;
    
    return rule;
}

void k8s_network_rule_free(k8s_network_rule_t* rule) {
    if (!rule) return;
    
    for (int i = 0; i < rule->num_peers; i++) {
        if (rule->peers[i]) {
            k8s_network_peer_free(rule->peers[i]);
        }
    }
    free(rule->peers);
    
    for (int i = 0; i < rule->num_ports; i++) {
        if (rule->ports[i]) {
            free(rule->ports[i]->protocol);
            free(rule->ports[i]);
        }
    }
    free(rule->ports);
    
    free(rule);
}

int k8s_network_rule_add_peer(k8s_network_rule_t* rule, k8s_network_peer_t* peer) {
    if (!rule || !peer) return -1;
    
    k8s_network_peer_t** new_peers = realloc(rule->peers, (rule->num_peers + 1) * sizeof(k8s_network_peer_t*));
    if (!new_peers) return -1;
    
    new_peers[rule->num_peers] = peer;
    rule->peers = new_peers;
    rule->num_peers++;
    return 0;
}

int k8s_network_rule_add_port(k8s_network_rule_t* rule, const char* protocol, int port) {
    if (!rule || !protocol || port <= 0 || port > 65535) return -1;
    
    k8s_network_port_t** new_ports = realloc(rule->ports, (rule->num_ports + 1) * sizeof(k8s_network_port_t*));
    if (!new_ports) return -1;
    
    k8s_network_port_t* new_port = calloc(1, sizeof(k8s_network_port_t));
    if (!new_port) {
        free(new_ports);
        return -1;
    }
    
    new_port->protocol = malloc(strlen(protocol) + 1);
    if (!new_port->protocol) {
        free(new_port);
        free(new_ports);
        return -1;
    }
    
    strcpy(new_port->protocol, protocol);
    new_port->port = port;
    
    new_ports[rule->num_ports] = new_port;
    rule->ports = new_ports;
    rule->num_ports++;
    return 0;
}

// ============ Spec ============

k8s_network_policy_spec_t* k8s_network_policy_spec_new() {
    k8s_network_policy_spec_t* spec = calloc(1, sizeof(k8s_network_policy_spec_t));
    if (!spec) return NULL;
    
    return spec;
}

void k8s_network_policy_spec_free(k8s_network_policy_spec_t* spec) {
    if (!spec) return;
    
    k8s_network_label_selector_free(&spec->pod_selector.pod_selector);
    
    for (int i = 0; i < spec->num_ingress; i++) {
        if (spec->ingress_rules[i]) {
            k8s_network_rule_free(spec->ingress_rules[i]);
        }
    }
    free(spec->ingress_rules);
    
    for (int i = 0; i < spec->num_egress; i++) {
        if (spec->egress_rules[i]) {
            k8s_network_rule_free(spec->egress_rules[i]);
        }
    }
    free(spec->egress_rules);
    
    for (int i = 0; i < spec->num_policy_types; i++) {
        free(spec->policy_types[i]);
    }
    free(spec->policy_types);
    
    free(spec);
}

int k8s_network_policy_add_ingress_rule(k8s_network_policy_spec_t* spec, k8s_network_rule_t* rule) {
    if (!spec || !rule) return -1;
    
    k8s_network_rule_t** new_rules = realloc(spec->ingress_rules, (spec->num_ingress + 1) * sizeof(k8s_network_rule_t*));
    if (!new_rules) return -1;
    
    new_rules[spec->num_ingress] = rule;
    spec->ingress_rules = new_rules;
    spec->num_ingress++;
    return 0;
}

int k8s_network_policy_add_egress_rule(k8s_network_policy_spec_t* spec, k8s_network_rule_t* rule) {
    if (!spec || !rule) return -1;
    
    k8s_network_rule_t** new_rules = realloc(spec->egress_rules, (spec->num_egress + 1) * sizeof(k8s_network_rule_t*));
    if (!new_rules) return -1;
    
    new_rules[spec->num_egress] = rule;
    spec->egress_rules = new_rules;
    spec->num_egress++;
    return 0;
}

int k8s_network_policy_add_policy_type(k8s_network_policy_spec_t* spec, const char* type) {
    if (!spec || !type) return -1;
    
    return add_string_to_array(&spec->policy_types, &spec->num_policy_types, type);
}

// ============ Status ============

k8s_network_policy_status_t* k8s_network_policy_status_new() {
    k8s_network_policy_status_t* status = calloc(1, sizeof(k8s_network_policy_status_t));
    if (!status) return NULL;
    
    return status;
}

void k8s_network_policy_status_free(k8s_network_policy_status_t* status) {
    if (!status) return;
    
    for (int i = 0; i < status->num_conditions; i++) {
        free(status->conditions[i]);
    }
    free(status->conditions);
    
    free(status);
}

// ============ Main NetworkPolicy ============

k8s_network_policy_t* k8s_network_policy_new(const char* name, const char* namespace) {
    if (!name || !namespace) return NULL;
    
    k8s_network_policy_t* policy = calloc(1, sizeof(k8s_network_policy_t));
    if (!policy) return NULL;
    
    policy->metadata = k8s_metadata_new(name, namespace);
    if (!policy->metadata) {
        free(policy);
        return NULL;
    }
    
    policy->spec = k8s_network_policy_spec_new();
    if (!policy->spec) {
        k8s_metadata_free(policy->metadata);
        free(policy);
        return NULL;
    }
    
    policy->status = k8s_network_policy_status_new();
    if (!policy->status) {
        k8s_network_policy_spec_free(policy->spec);
        k8s_metadata_free(policy->metadata);
        free(policy);
        return NULL;
    }
    
    return policy;
}

void k8s_network_policy_free(k8s_network_policy_t* policy) {
    if (!policy) return;
    
    if (policy->metadata) k8s_metadata_free(policy->metadata);
    if (policy->spec) k8s_network_policy_spec_free(policy->spec);
    if (policy->status) k8s_network_policy_status_free(policy->status);
    
    free(policy);
}

// ============ Serialization ============

char* k8s_network_policy_to_json(k8s_network_policy_t* policy) {
    if (!policy || !policy->metadata) return NULL;
    
    json_object* obj = json_object_new_object();
    
    // API version and kind
    json_object_object_add(obj, "apiVersion", json_object_new_string("networking.k8s.io/v1"));
    json_object_object_add(obj, "kind", json_object_new_string("NetworkPolicy"));
    
    // Metadata
    json_object* meta = json_object_new_object();
    json_object_object_add(meta, "name", json_object_new_string(policy->metadata->name));
    json_object_object_add(meta, "namespace", json_object_new_string(policy->metadata->namespace));
    if (policy->metadata->uid) {
        json_object_object_add(meta, "uid", json_object_new_string(policy->metadata->uid));
    }
    json_object_object_add(obj, "metadata", meta);
    
    // Spec
    if (policy->spec) {
        json_object* spec = json_object_new_object();
        
        // Pod selector
        json_object* pod_sel = json_object_new_object();
        json_object* labels = json_object_new_object();
        for (int i = 0; i < policy->spec->pod_selector.pod_selector.num_labels; i++) {
            const char* label = policy->spec->pod_selector.pod_selector.match_labels[i];
            char* eq = strchr(label, '=');
            if (eq) {
                *eq = '\0';
                json_object_object_add(labels, label, json_object_new_string(eq + 1));
                *eq = '=';
            }
        }
        json_object_object_add(pod_sel, "matchLabels", labels);
        json_object_object_add(spec, "podSelector", pod_sel);
        
        // Policy types
        json_object* types_arr = json_object_new_array();
        for (int i = 0; i < policy->spec->num_policy_types; i++) {
            json_object_array_add(types_arr, json_object_new_string(policy->spec->policy_types[i]));
        }
        if (policy->spec->num_policy_types > 0) {
            json_object_object_add(spec, "policyTypes", types_arr);
        } else {
            json_object_put(types_arr);
        }
        
        json_object_object_add(obj, "spec", spec);
    }
    
    const char* json_str = json_object_to_json_string(obj);
    char* result = malloc(strlen(json_str) + 1);
    if (result) strcpy(result, json_str);
    
    json_object_put(obj);
    return result;
}

k8s_network_policy_t* k8s_network_policy_from_json(const char* json) {
    if (!json) return NULL;
    
    // TODO: Implement full JSON parsing
    // For now, return NULL (stub implementation)
    return NULL;
}

// ============ Policy Enforcement ============

bool k8s_network_policy_allows_traffic(k8s_network_policy_t* policy,
                                       const char* direction,
                                       const char* source_pod,
                                       const char* source_ns,
                                       const char* source_ip,
                                       const char* dest_pod,
                                       const char* dest_ns,
                                       const char* dest_ip,
                                       int port,
                                       const char* protocol) {
    if (!policy || !direction || !protocol) return false;
    
    // TODO: Implement full policy evaluation logic
    // Check direction (ingress vs egress)
    // Match pod selectors and namespace selectors
    // Verify port and protocol
    // For now, allow all traffic (stub implementation)
    return true;
}
