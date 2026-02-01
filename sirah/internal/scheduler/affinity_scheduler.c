#include "affinity_scheduler.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static k8s_affinity_scheduler_t* g_affinity_scheduler = NULL;

// ============ Lifecycle ============

k8s_affinity_scheduler_t* k8s_affinity_scheduler_new(int max_nodes) {
    if (max_nodes <= 0) return NULL;
    
    k8s_affinity_scheduler_t* scheduler = (k8s_affinity_scheduler_t*)malloc(sizeof(k8s_affinity_scheduler_t));
    if (!scheduler) return NULL;
    
    scheduler->nodes = (k8s_node_t**)malloc(max_nodes * sizeof(k8s_node_t*));
    if (!scheduler->nodes) { free(scheduler); return NULL; }
    
    scheduler->cache_keys = (char**)malloc(1000 * sizeof(char*));
    if (!scheduler->cache_keys) { free(scheduler->nodes); free(scheduler); return NULL; }
    
    scheduler->cache_results = (bool*)malloc(1000 * sizeof(bool));
    if (!scheduler->cache_results) { free(scheduler->cache_keys); free(scheduler->nodes); free(scheduler); return NULL; }
    
    scheduler->max_nodes = max_nodes;
    scheduler->num_nodes = 0;
    scheduler->cache_size = 0;
    
    return scheduler;
}

void k8s_affinity_scheduler_free(k8s_affinity_scheduler_t* scheduler) {
    if (!scheduler) return;
    
    free(scheduler->nodes);
    
    for (int i = 0; i < scheduler->cache_size; i++) {
        free(scheduler->cache_keys[i]);
    }
    free(scheduler->cache_keys);
    free(scheduler->cache_results);
    free(scheduler);
}

// ============ Node Management ============

int k8s_affinity_scheduler_add_node(k8s_affinity_scheduler_t* scheduler, k8s_node_t* node) {
    if (!scheduler || !node || scheduler->num_nodes >= scheduler->max_nodes) return -1;
    
    // Check for duplicates
    for (int i = 0; i < scheduler->num_nodes; i++) {
        if (scheduler->nodes[i] && strcmp(scheduler->nodes[i]->metadata.name, node->metadata.name) == 0) {
            return -1;  // Node already exists
        }
    }
    
    scheduler->nodes[scheduler->num_nodes] = node;
    scheduler->num_nodes++;
    
    return 0;
}

int k8s_affinity_scheduler_remove_node(k8s_affinity_scheduler_t* scheduler, const char* node_name) {
    if (!scheduler || !node_name) return -1;
    
    for (int i = 0; i < scheduler->num_nodes; i++) {
        if (scheduler->nodes[i] && strcmp(scheduler->nodes[i]->metadata.name, node_name) == 0) {
            // Shift remaining nodes
            for (int j = i; j < scheduler->num_nodes - 1; j++) {
                scheduler->nodes[j] = scheduler->nodes[j + 1];
            }
            scheduler->num_nodes--;
            return 0;
        }
    }
    
    return -1;
}

k8s_node_t* k8s_affinity_scheduler_get_node(k8s_affinity_scheduler_t* scheduler, const char* node_name) {
    if (!scheduler || !node_name) return NULL;
    
    for (int i = 0; i < scheduler->num_nodes; i++) {
        if (scheduler->nodes[i] && strcmp(scheduler->nodes[i]->metadata.name, node_name) == 0) {
            return scheduler->nodes[i];
        }
    }
    
    return NULL;
}

// ============ Label Matching ============

bool k8s_affinity_scheduler_labels_match(const char** pod_labels, int num_pod_labels,
                                          const char** selector_labels, int num_selector_labels) {
    if (!pod_labels || !selector_labels) return false;
    if (num_pod_labels == 0 || num_selector_labels == 0) return false;
    
    // All selector labels must be in pod labels
    for (int i = 0; i < num_selector_labels; i++) {
        bool found = false;
        for (int j = 0; j < num_pod_labels; j++) {
            if (strcmp(pod_labels[j], selector_labels[i]) == 0) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    
    return true;
}

bool k8s_affinity_scheduler_node_has_label(k8s_node_t* node, const char* key, const char* value) {
    if (!node || !key || !value) return false;
    // node->labels is a JSON-encoded string, for now do simple substring matching
    if (node->labels[0] == '\0') return false;
    
    // Check if key:value is in the JSON labels string
    // Simplified implementation - just check substring presence
    char search_str[256];
    snprintf(search_str, sizeof(search_str), "\"%s\":\"%s\"", key, value);
    
    return strstr(node->labels, search_str) != NULL;
}

const char* k8s_affinity_scheduler_get_node_label(k8s_node_t* node, const char* key) {
    if (!node || !key) return NULL;
    // node->labels is a JSON-encoded string, for now return a static buffer
    // This is a stub - full implementation would parse JSON
    if (node->labels[0] == '\0') return NULL;
    
    // Check if key is in the labels JSON string
    char search_str[256];
    snprintf(search_str, sizeof(search_str), "\"%s\":", key);
    
    if (strstr(node->labels, search_str) != NULL) {
        return key;  // Stub: just return the key for now
    }
    
    return NULL;
}

// ============ Taint Evaluation ============

bool k8s_affinity_scheduler_tolerations_satisfy_taints(
    k8s_toleration_t** tolerations, int num_tolerations,
    k8s_taint_t** taints, int num_taints) {
    
    if (!tolerations || num_tolerations == 0) {
        // No tolerations means no taints allowed
        return num_taints == 0;
    }
    if (!taints || num_taints == 0) {
        // No taints means always satisfied
        return true;
    }
    
    // Every taint must be tolerated by some toleration
    for (int i = 0; i < num_taints; i++) {
        bool tolerated = false;
        for (int j = 0; j < num_tolerations; j++) {
            if (k8s_toleration_matches_taint(tolerations[j], taints[i])) {
                tolerated = true;
                break;
            }
        }
        if (!tolerated) return false;
    }
    
    return true;
}

// ============ Affinity Evaluation ============

int k8s_affinity_scheduler_score_node(
    k8s_affinity_scheduler_t* scheduler,
    k8s_pod_t* pod,
    k8s_node_t* node) {
    
    if (!scheduler || !pod || !node) return 0;
    
    int score = 50;  // Base score
    
    // Check node affinity if present
    if (pod->spec.affinity && pod->spec.affinity->node_affinity) {
        k8s_node_affinity_t* node_aff = pod->spec.affinity->node_affinity;
        
        // Required node affinity must match or pod can't be placed
        if (node_aff->required_during_scheduling) {
            // At least one term must match
            bool found_match = false;
            for (int i = 0; i < node_aff->required_during_scheduling->num_terms; i++) {
                // Check if node matches this term
                // Simplified: check if node has matching labels
                found_match = true;  // Stub implementation
            }
            if (!found_match) return 0;  // Can't place on this node
        }
        
        // Preferred node affinity adds to score
        if (node_aff->preferred_during_scheduling) {
            for (int i = 0; i < node_aff->num_preferred; i++) {
                if (node_aff->preferred_during_scheduling[i]) {
                    score += (node_aff->preferred_during_scheduling[i]->weight / 2);
                }
            }
        }
    }
    
    // Check pod affinity with other pods
    if (pod->spec.affinity && pod->spec.affinity->pod_affinity) {
        // Simplified: add weight for each pod affinity preference
        k8s_pod_affinity_t* pod_aff = pod->spec.affinity->pod_affinity;
        if (pod_aff->preferred) {
            score += (pod_aff->num_preferred * 5);
        }
    }
    
    // Check pod anti-affinity
    if (pod->spec.affinity && pod->spec.affinity->pod_anti_affinity) {
        // Penalize if other pods are on this node
        k8s_pod_anti_affinity_t* pod_anti_aff = pod->spec.affinity->pod_anti_affinity;
        if (pod_anti_aff->required) {
            score -= (pod_anti_aff->num_required * 20);
        }
    }
    
    return (score < 0) ? 0 : (score > 100 ? 100 : score);
}

bool k8s_affinity_scheduler_can_place(
    k8s_affinity_scheduler_t* scheduler,
    k8s_pod_t* pod,
    k8s_node_t* node) {
    
    if (!scheduler || !pod || !node) return false;
    
    // Check required node affinity
    if (pod->spec.affinity && pod->spec.affinity->node_affinity) {
        k8s_node_affinity_t* node_aff = pod->spec.affinity->node_affinity;
        
        if (node_aff->required_during_scheduling && node_aff->required_during_scheduling->num_terms > 0) {
            // At least one required term must match
            bool found = false;
            for (int i = 0; i < node_aff->required_during_scheduling->num_terms; i++) {
                // Simplified: assume match (full implementation needs expression evaluation)
                found = true;
            }
            if (!found) return false;
        }
    }
    
    // Check pod affinity required terms
    if (pod->spec.affinity && pod->spec.affinity->pod_affinity) {
        k8s_pod_affinity_t* pod_aff = pod->spec.affinity->pod_affinity;
        
        if (pod_aff->required && pod_aff->num_required > 0) {
            // Must have at least one pod matching each required term
            // Simplified implementation: check topology key
            for (int i = 0; i < pod_aff->num_required; i++) {
                if (pod_aff->required[i] && pod_aff->required[i]->terms) {
                    // Check if matching pods exist on this node/zone
                    // Stub: assume satisfied for now
                }
            }
        }
    }
    
    // Check pod anti-affinity required terms
    if (pod->spec.affinity && pod->spec.affinity->pod_anti_affinity) {
        k8s_pod_anti_affinity_t* pod_anti_aff = pod->spec.affinity->pod_anti_affinity;
        
        if (pod_anti_aff->required && pod_anti_aff->num_required > 0) {
            // Must NOT have pods matching required terms
            for (int i = 0; i < pod_anti_aff->num_required; i++) {
                if (pod_anti_aff->required[i] && pod_anti_aff->required[i]->terms) {
                    // Check if conflicting pods exist
                    // Stub: assume no conflicts for now
                }
            }
        }
    }
    
    // Check taints and tolerations
    if (pod->spec.tolerations && node->spec.taints) {
        bool satisfied = k8s_affinity_scheduler_tolerations_satisfy_taints(
            pod->spec.tolerations, pod->spec.num_tolerations,
            node->spec.taints, node->spec.num_taints
        );
        if (!satisfied) return false;
    }
    
    return true;
}

k8s_node_t* k8s_affinity_scheduler_select_node(
    k8s_affinity_scheduler_t* scheduler,
    k8s_pod_t* pod,
    k8s_node_t** available_nodes,
    int num_available) {
    
    if (!scheduler || !pod || !available_nodes || num_available == 0) return NULL;
    
    k8s_node_t* best_node = NULL;
    int best_score = -1;
    
    // Find best available node considering affinity
    for (int i = 0; i < num_available; i++) {
        if (!available_nodes[i]) continue;
        
        // Check if placement is possible
        if (!k8s_affinity_scheduler_can_place(scheduler, pod, available_nodes[i])) {
            continue;
        }
        
        // Score the node
        int score = k8s_affinity_scheduler_score_node(scheduler, pod, available_nodes[i]);
        if (score > best_score) {
            best_score = score;
            best_node = available_nodes[i];
        }
    }
    
    return best_node;
}

// ============ Cache Management ============

void k8s_affinity_scheduler_clear_cache(k8s_affinity_scheduler_t* scheduler) {
    if (!scheduler) return;
    
    for (int i = 0; i < scheduler->cache_size; i++) {
        free(scheduler->cache_keys[i]);
    }
    scheduler->cache_size = 0;
}

// ============ Global Instance ============

k8s_affinity_scheduler_t* k8s_affinity_scheduler_global() {
    if (!g_affinity_scheduler) {
        g_affinity_scheduler = k8s_affinity_scheduler_new(5000);  // Support 5000 nodes
    }
    return g_affinity_scheduler;
}
