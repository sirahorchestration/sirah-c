#include "rbac.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// ==================== Helper Functions ====================

static int add_string_to_array(char* array[], int* count, int max, const char* str) {
    if (!str || !array || !count) return -1;
    if (*count >= max) return -1;
    array[*count] = strdup(str);
    (*count)++;
    return 0;
}

// ==================== Policy Rule Functions ====================

k8s_policy_rule_t* k8s_policy_rule_new() {
    return calloc(1, sizeof(k8s_policy_rule_t));
}

void k8s_policy_rule_free(k8s_policy_rule_t* rule) {
    if (!rule) return;
    for (int i = 0; i < rule->num_verbs; i++) free(rule->verbs[i]);
    for (int i = 0; i < rule->num_apigroups; i++) free(rule->api_groups[i]);
    for (int i = 0; i < rule->num_resources; i++) free(rule->resources[i]);
    for (int i = 0; i < rule->num_resourcenames; i++) free(rule->resource_names[i]);
    free(rule);
}

int k8s_policy_rule_add_verb(k8s_policy_rule_t* rule, const char* verb) {
    if (!rule) return -1;
    return add_string_to_array(rule->verbs, &rule->num_verbs, K8S_MAX_VERBS, verb);
}

int k8s_policy_rule_add_api_group(k8s_policy_rule_t* rule, const char* group) {
    if (!rule) return -1;
    return add_string_to_array(rule->api_groups, &rule->num_apigroups, K8S_MAX_API_GROUPS, group);
}

int k8s_policy_rule_add_resource(k8s_policy_rule_t* rule, const char* resource) {
    if (!rule) return -1;
    return add_string_to_array(rule->resources, &rule->num_resources, K8S_MAX_RESOURCES, resource);
}

int k8s_policy_rule_add_resource_name(k8s_policy_rule_t* rule, const char* name) {
    if (!rule) return -1;
    return add_string_to_array(rule->resource_names, &rule->num_resourcenames, K8S_MAX_RESOURCE_NAMES, name);
}

// ==================== Role Functions ====================

k8s_role_t* k8s_role_new(const char* name, const char* namespace) {
    k8s_role_t* role = calloc(1, sizeof(k8s_role_t));
    if (!role) return NULL;
    role->metadata = k8s_metadata_new(name, namespace);
    return role;
}

void k8s_role_free(k8s_role_t* role) {
    if (!role) return;
    if (role->metadata) k8s_metadata_free(role->metadata);
    for (int i = 0; i < role->spec.num_rules; i++) {
        k8s_policy_rule_free(&role->spec.rules[i]);
    }
    free(role);
}

int k8s_role_add_rule(k8s_role_t* role, k8s_policy_rule_t* rule) {
    if (!role || !rule) return -1;
    if (role->spec.num_rules >= K8S_MAX_POLICY_RULES) return -1;
    role->spec.rules[role->spec.num_rules] = *rule;
    role->spec.num_rules++;
    return 0;
}

char* k8s_role_to_json(k8s_role_t* role) {
    if (!role || !role->metadata) return NULL;
    static char json[8192];
    snprintf(json, sizeof(json), "{\"apiVersion\":\"rbac.authorization.k8s.io/v1\",\"kind\":\"Role\",\"metadata\":{\"name\":\"%s\",\"namespace\":\"%s\"}}",
        role->metadata->name ? role->metadata->name : "",
        role->metadata->namespace ? role->metadata->namespace : "");
    return json;
}

k8s_role_t* k8s_role_from_json(const char* json) {
    return NULL;  // TODO
}

// ==================== RoleBinding Functions ====================

k8s_role_binding_t* k8s_role_binding_new(const char* name, const char* namespace, const char* role_name) {
    k8s_role_binding_t* binding = calloc(1, sizeof(k8s_role_binding_t));
    if (!binding) return NULL;
    binding->metadata = k8s_metadata_new(name, namespace);
    binding->spec.role_name = strdup(role_name);
    return binding;
}

void k8s_role_binding_free(k8s_role_binding_t* binding) {
    if (!binding) return;
    if (binding->metadata) k8s_metadata_free(binding->metadata);
    if (binding->spec.role_name) free(binding->spec.role_name);
    for (int i = 0; i < binding->spec.num_subjects; i++) {
        if (binding->spec.subjects[i].kind) free(binding->spec.subjects[i].kind);
        if (binding->spec.subjects[i].name) free(binding->spec.subjects[i].name);
        if (binding->spec.subjects[i].namespace) free(binding->spec.subjects[i].namespace);
    }
    free(binding);
}

int k8s_role_binding_add_subject(k8s_role_binding_t* binding, const char* kind, const char* name, const char* namespace) {
    if (!binding) return -1;
    if (binding->spec.num_subjects >= K8S_MAX_SUBJECTS) return -1;
    int idx = binding->spec.num_subjects;
    binding->spec.subjects[idx].kind = strdup(kind);
    binding->spec.subjects[idx].name = strdup(name);
    if (namespace) binding->spec.subjects[idx].namespace = strdup(namespace);
    binding->spec.num_subjects++;
    return 0;
}

char* k8s_role_binding_to_json(k8s_role_binding_t* binding) {
    if (!binding || !binding->metadata) return NULL;
    static char json[4096];
    snprintf(json, sizeof(json), "{\"apiVersion\":\"rbac.authorization.k8s.io/v1\",\"kind\":\"RoleBinding\",\"metadata\":{\"name\":\"%s\",\"namespace\":\"%s\"}}",
        binding->metadata->name ? binding->metadata->name : "",
        binding->metadata->namespace ? binding->metadata->namespace : "");
    return json;
}

k8s_role_binding_t* k8s_role_binding_from_json(const char* json) {
    return NULL;  // TODO
}

// ==================== ClusterRole Functions ====================

k8s_cluster_role_t* k8s_cluster_role_new(const char* name) {
    k8s_cluster_role_t* role = calloc(1, sizeof(k8s_cluster_role_t));
    if (!role) return NULL;
    role->metadata = k8s_metadata_new(name, "");
    return role;
}

void k8s_cluster_role_free(k8s_cluster_role_t* role) {
    if (!role) return;
    if (role->metadata) k8s_metadata_free(role->metadata);
    for (int i = 0; i < role->spec.num_rules; i++) {
        k8s_policy_rule_free(&role->spec.rules[i]);
    }
    free(role);
}

int k8s_cluster_role_add_rule(k8s_cluster_role_t* role, k8s_policy_rule_t* rule) {
    if (!role || !rule) return -1;
    if (role->spec.num_rules >= K8S_MAX_POLICY_RULES) return -1;
    role->spec.rules[role->spec.num_rules] = *rule;
    role->spec.num_rules++;
    return 0;
}

char* k8s_cluster_role_to_json(k8s_cluster_role_t* role) {
    if (!role || !role->metadata) return NULL;
    static char json[8192];
    snprintf(json, sizeof(json), "{\"apiVersion\":\"rbac.authorization.k8s.io/v1\",\"kind\":\"ClusterRole\",\"metadata\":{\"name\":\"%s\"}}",
        role->metadata->name ? role->metadata->name : "");
    return json;
}

k8s_cluster_role_t* k8s_cluster_role_from_json(const char* json) {
    return NULL;  // TODO
}

// ==================== ClusterRoleBinding Functions ====================

k8s_cluster_role_binding_t* k8s_cluster_role_binding_new(const char* name, const char* cluster_role_name) {
    k8s_cluster_role_binding_t* binding = calloc(1, sizeof(k8s_cluster_role_binding_t));
    if (!binding) return NULL;
    binding->metadata = k8s_metadata_new(name, "");
    binding->spec.cluster_role_name = strdup(cluster_role_name);
    return binding;
}

void k8s_cluster_role_binding_free(k8s_cluster_role_binding_t* binding) {
    if (!binding) return;
    if (binding->metadata) k8s_metadata_free(binding->metadata);
    if (binding->spec.cluster_role_name) free(binding->spec.cluster_role_name);
    for (int i = 0; i < binding->spec.num_subjects; i++) {
        if (binding->spec.subjects[i].kind) free(binding->spec.subjects[i].kind);
        if (binding->spec.subjects[i].name) free(binding->spec.subjects[i].name);
        if (binding->spec.subjects[i].namespace) free(binding->spec.subjects[i].namespace);
    }
    free(binding);
}

int k8s_cluster_role_binding_add_subject(k8s_cluster_role_binding_t* binding, const char* kind, const char* name, const char* namespace) {
    if (!binding) return -1;
    if (binding->spec.num_subjects >= K8S_MAX_SUBJECTS) return -1;
    int idx = binding->spec.num_subjects;
    binding->spec.subjects[idx].kind = strdup(kind);
    binding->spec.subjects[idx].name = strdup(name);
    if (namespace) binding->spec.subjects[idx].namespace = strdup(namespace);
    binding->spec.num_subjects++;
    return 0;
}

char* k8s_cluster_role_binding_to_json(k8s_cluster_role_binding_t* binding) {
    if (!binding || !binding->metadata) return NULL;
    static char json[4096];
    snprintf(json, sizeof(json), "{\"apiVersion\":\"rbac.authorization.k8s.io/v1\",\"kind\":\"ClusterRoleBinding\",\"metadata\":{\"name\":\"%s\"}}",
        binding->metadata->name ? binding->metadata->name : "");
    return json;
}

k8s_cluster_role_binding_t* k8s_cluster_role_binding_from_json(const char* json) {
    return NULL;  // TODO
}

// ==================== ServiceAccount Functions ====================

k8s_service_account_t* k8s_service_account_new(const char* name, const char* namespace) {
    k8s_service_account_t* sa = calloc(1, sizeof(k8s_service_account_t));
    if (!sa) return NULL;
    sa->metadata = k8s_metadata_new(name, namespace);
    sa->spec.automount_service_account_token = true;
    return sa;
}

void k8s_service_account_free(k8s_service_account_t* sa) {
    if (!sa) return;
    if (sa->metadata) k8s_metadata_free(sa->metadata);
    free(sa);
}

char* k8s_service_account_to_json(k8s_service_account_t* sa) {
    if (!sa || !sa->metadata) return NULL;
    static char json[2048];
    snprintf(json, sizeof(json), "{\"apiVersion\":\"v1\",\"kind\":\"ServiceAccount\",\"metadata\":{\"name\":\"%s\",\"namespace\":\"%s\"}}",
        sa->metadata->name ? sa->metadata->name : "",
        sa->metadata->namespace ? sa->metadata->namespace : "");
    return json;
}

k8s_service_account_t* k8s_service_account_from_json(const char* json) {
    return NULL;  // TODO
}
