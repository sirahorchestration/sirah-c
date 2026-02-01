#ifndef K8S_AFFINITY_SCHEDULER_H
#define K8S_AFFINITY_SCHEDULER_H

#include "../../pkg/types/affinity.h"
#include "../../pkg/types/node.h"
#include "../../pkg/types/pod.h"
#include <stdbool.h>

typedef struct {
    int max_nodes;
    k8s_node_t** nodes;
    int num_nodes;
    
    // Cache for affinity evaluations
    char** cache_keys;
    bool* cache_results;
    int cache_size;
} k8s_affinity_scheduler_t;

// Lifecycle
k8s_affinity_scheduler_t* k8s_affinity_scheduler_new(int max_nodes);
void k8s_affinity_scheduler_free(k8s_affinity_scheduler_t* scheduler);

// Node management
int k8s_affinity_scheduler_add_node(k8s_affinity_scheduler_t* scheduler, k8s_node_t* node);
int k8s_affinity_scheduler_remove_node(k8s_affinity_scheduler_t* scheduler, const char* node_name);
k8s_node_t* k8s_affinity_scheduler_get_node(k8s_affinity_scheduler_t* scheduler, const char* node_name);

// Affinity evaluation
// Returns score (0-100) for how well node matches pod affinity
// Higher score = better match
int k8s_affinity_scheduler_score_node(
    k8s_affinity_scheduler_t* scheduler,
    k8s_pod_t* pod,
    k8s_node_t* node
);

// Check if node can accept pod (required affinities)
bool k8s_affinity_scheduler_can_place(
    k8s_affinity_scheduler_t* scheduler,
    k8s_pod_t* pod,
    k8s_node_t* node
);

// Find best node for pod (considering affinity and taints)
k8s_node_t* k8s_affinity_scheduler_select_node(
    k8s_affinity_scheduler_t* scheduler,
    k8s_pod_t* pod,
    k8s_node_t** available_nodes,
    int num_available
);

// ============ Label Matching ============

// Check if pod labels match selector labels
bool k8s_affinity_scheduler_labels_match(const char** pod_labels, int num_pod_labels,
                                          const char** selector_labels, int num_selector_labels);

// Check if node has label with specific value
bool k8s_affinity_scheduler_node_has_label(k8s_node_t* node, const char* key, const char* value);

// Get node label value
const char* k8s_affinity_scheduler_get_node_label(k8s_node_t* node, const char* key);

// ============ Taint Evaluation ============

// Check if pod has toleration for all node taints
bool k8s_affinity_scheduler_tolerations_satisfy_taints(
    k8s_toleration_t** tolerations, int num_tolerations,
    k8s_taint_t** taints, int num_taints
);

// ============ Cache Management ============

void k8s_affinity_scheduler_clear_cache(k8s_affinity_scheduler_t* scheduler);

// Global instance
k8s_affinity_scheduler_t* k8s_affinity_scheduler_global();

#endif
