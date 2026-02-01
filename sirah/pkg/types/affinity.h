#ifndef K8S_AFFINITY_H
#define K8S_AFFINITY_H

#include <stdbool.h>

// Node selector term (used in affinity rules)
typedef struct {
    char** match_expressions;  // Key operator value expressions
    int num_expressions;
} k8s_node_selector_term_t;

// Node affinity requirement
typedef struct {
    k8s_node_selector_term_t** terms;
    int num_terms;
} k8s_node_affinity_req_t;

// Pod affinity term
typedef struct {
    char** pod_selector_labels;    // Pod labels to match
    int num_labels;
    char** namespaces;             // Namespaces to search
    int num_namespaces;
    char* topology_key;            // Node label key for grouping
} k8s_pod_affinity_term_t;

// Pod affinity requirement
typedef struct {
    k8s_pod_affinity_term_t** terms;
    int num_terms;
    int weight;  // For preferred affinities (1-100)
} k8s_pod_affinity_req_t;

// Node affinity
typedef struct {
    k8s_node_affinity_req_t* required_during_scheduling;
    k8s_pod_affinity_req_t** preferred_during_scheduling;
    int num_preferred;
} k8s_node_affinity_t;

// Pod affinity (prefer/require pods together)
typedef struct {
    k8s_pod_affinity_req_t** required;
    int num_required;
    k8s_pod_affinity_req_t** preferred;
    int num_preferred;
} k8s_pod_affinity_t;

// Pod anti-affinity (keep pods apart)
typedef struct {
    k8s_pod_affinity_req_t** required;
    int num_required;
    k8s_pod_affinity_req_t** preferred;
    int num_preferred;
} k8s_pod_anti_affinity_t;

// Full affinity specification
typedef struct {
    k8s_node_affinity_t* node_affinity;
    k8s_pod_affinity_t* pod_affinity;
    k8s_pod_anti_affinity_t* pod_anti_affinity;
} k8s_affinity_t;

// Taint (prevents pod scheduling)
typedef struct k8s_taint {
    char* key;
    char* value;
    char* effect;  // NoSchedule, NoExecute, PreferNoSchedule
    int toleration_seconds;  // For NoExecute
} k8s_taint_t;

// Toleration (allows pod on tainted nodes)
typedef struct {
    char* key;
    char* operator;  // Equal, Exists
    char* value;
    char* effect;  // NoSchedule, NoExecute, PreferNoSchedule
    int toleration_seconds;
} k8s_toleration_t;

// ============ Affinity Functions ============

k8s_affinity_t* k8s_affinity_new();
void k8s_affinity_free(k8s_affinity_t* affinity);

k8s_node_affinity_t* k8s_node_affinity_new();
void k8s_node_affinity_free(k8s_node_affinity_t* affinity);

k8s_pod_affinity_t* k8s_pod_affinity_new();
void k8s_pod_affinity_free(k8s_pod_affinity_t* affinity);

k8s_pod_anti_affinity_t* k8s_pod_anti_affinity_new();
void k8s_pod_anti_affinity_free(k8s_pod_anti_affinity_t* affinity);

// Add requirements and preferences
int k8s_node_affinity_add_required_term(k8s_node_affinity_t* affinity, k8s_node_selector_term_t* term);
int k8s_node_affinity_add_preferred_term(k8s_node_affinity_t* affinity, k8s_pod_affinity_req_t* term, int weight);

int k8s_pod_affinity_add_required(k8s_pod_affinity_t* affinity, k8s_pod_affinity_term_t* term);
int k8s_pod_affinity_add_preferred(k8s_pod_affinity_t* affinity, k8s_pod_affinity_term_t* term, int weight);

int k8s_pod_anti_affinity_add_required(k8s_pod_anti_affinity_t* affinity, k8s_pod_affinity_term_t* term);
int k8s_pod_anti_affinity_add_preferred(k8s_pod_anti_affinity_t* affinity, k8s_pod_affinity_term_t* term, int weight);

// ============ Taint & Toleration Functions ============

k8s_taint_t* k8s_taint_new(const char* key, const char* value, const char* effect);
void k8s_taint_free(k8s_taint_t* taint);

k8s_toleration_t* k8s_toleration_new(const char* key, const char* operator, const char* value, const char* effect);
void k8s_toleration_free(k8s_toleration_t* toleration);

// Check if toleration matches taint
bool k8s_toleration_matches_taint(k8s_toleration_t* toleration, k8s_taint_t* taint);

#endif
