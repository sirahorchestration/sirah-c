#include "scoring.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

// Bin-packing: prefer nodes with least remaining capacity
// Score: higher score = better bin-packing
// Formula: (remaining_cpu + remaining_memory%) / 2
int scoring_bin_packing(node_state_t* node, pod_request_t* request) {
    if (!node || !request) return 0;
    
    // Calculate CPU utilization after placing pod
    long cpu_after = node->allocated_cpus + request->cpu_millicores;
    int cpu_util_percent = (cpu_after * 100) / node->allocatable_cpus;
    cpu_util_percent = (cpu_util_percent > 100) ? 100 : cpu_util_percent;
    
    // Calculate memory utilization after placing pod
    long memory_after = node->allocated_memory + request->memory_bytes;
    int memory_util_percent = (memory_after * 100) / node->allocatable_memory;
    memory_util_percent = (memory_util_percent > 100) ? 100 : memory_util_percent;
    
    // Prefer fuller nodes (higher utilization)
    int score = (cpu_util_percent + memory_util_percent) / 2;
    
    return score;
}

// Pod spread: prefer nodes with fewer pods
// Score: higher score = better spread
// Formula: (1 - pod_count/max_pods) * 100
int scoring_pod_spread(node_state_t* node, int total_pods_in_cluster) {
    if (!node) return 0;
    
    // Prefer nodes with fewer pods
    int pods_used_percent = (node->pod_count * 100) / node->max_pods;
    int score = 100 - pods_used_percent;  // Invert so fewer pods = higher score
    
    return (score < 0) ? 0 : score;
}

// Pod affinity: prefer nodes with pods matching affinity labels
// Score: (matching_affinity_pods / total_matching_labels) * 100
int scoring_pod_affinity(scheduler_t* scheduler, node_state_t* node, 
                         pod_spec_t* pod) {
    if (!scheduler || !node || !pod) return 0;
    
    if (pod->pod_affinity_count == 0) {
        return 50;  // Neutral score if no affinity
    }
    
    int matching = 0;
    
    // Count pods on this node that match affinity labels
    for (int i = 0; i < pod->pod_affinity_count; i++) {
        // In real implementation, would check pod labels
        // For now, simulate based on pod_count
        matching += (node->pod_count > 0) ? 1 : 0;
    }
    
    int score = (matching * 100) / pod->pod_affinity_count;
    return score;
}

// Pod anti-affinity: penalize nodes with anti-affinity pods
// Score: (non_matching_pods / total_anti_affinity) * 100
int scoring_pod_anti_affinity(scheduler_t* scheduler, node_state_t* node, 
                              pod_spec_t* pod) {
    if (!scheduler || !node || !pod) return 50;  // Neutral if no anti-affinity
    
    if (pod->pod_anti_affinity_count == 0) {
        return 50;  // Neutral score if no anti-affinity
    }
    
    int conflicting = 0;
    
    // Count pods on this node that match anti-affinity labels
    for (int i = 0; i < pod->pod_anti_affinity_count; i++) {
        // In real implementation, would check pod labels
        // For now, simulate based on pod_count
        conflicting += (node->pod_count > 0) ? 1 : 0;
    }
    
    // Penalize for conflicting pods
    int score = 100 - ((conflicting * 100) / pod->pod_anti_affinity_count);
    return (score < 0) ? 0 : score;
}

// Node affinity: prefer nodes with matching labels
// Score: (matching_labels / required_labels) * 100
int scoring_node_affinity(node_state_t* node, pod_spec_t* pod) {
    if (!node || !pod) return 50;  // Neutral if no affinity
    
    if (pod->preferred_count == 0) {
        return 50;  // Neutral if no preferred labels
    }
    
    int matching = 0;
    
    // Count matching labels
    for (int i = 0; i < pod->preferred_count; i++) {
        for (int j = 0; j < node->label_count; j++) {
            if (strcmp(node->label_keys[j], pod->preferred_node_labels[i]) == 0) {
                matching++;
                break;
            }
        }
    }
    
    int score = (matching * 100) / pod->preferred_count;
    return score;
}

// Topology spread: prefer nodes that improve spread of topology domain
// Score: lower pod count on node = higher score
int scoring_topology_spread(scheduler_t* scheduler, node_state_t* node, 
                           pod_spec_t* pod) {
    if (!scheduler || !node || !pod) return 50;
    
    // Simple topology spread: prefer nodes with fewer pods of same app
    // In real K8s, this would use topology domains (zones, racks, etc.)
    
    int score = 100 - ((node->pod_count * 100) / node->max_pods);
    return (score < 0) ? 0 : score;
}

// Combined scoring: weighted sum of all scoring functions
int scoring_combined(scheduler_t* scheduler, node_state_t* node, 
                    pod_spec_t* pod) {
    if (!scheduler || !node || !pod) return 0;
    
    int total_score = 0;
    int weights_sum = 0;
    
    // Bin-packing scoring (40% weight by default)
    if (scheduler->enable_bin_packing && scheduler->weight_bin_packing > 0) {
        int bp_score = scoring_bin_packing(node, &pod->request);
        total_score += (bp_score * scheduler->weight_bin_packing) / 100;
        weights_sum += scheduler->weight_bin_packing;
    }
    
    // Pod spread scoring (30% weight by default)
    if (scheduler->enable_pod_spread && scheduler->weight_pod_spread > 0) {
        int ps_score = scoring_pod_spread(node, scheduler->node_count);
        total_score += (ps_score * scheduler->weight_pod_spread) / 100;
        weights_sum += scheduler->weight_pod_spread;
    }
    
    // Node affinity scoring (20% weight by default)
    if (scheduler->enable_affinity && scheduler->weight_affinity > 0) {
        int na_score = scoring_node_affinity(node, pod);
        int paf_score = scoring_pod_affinity(scheduler, node, pod);
        int paaf_score = scoring_pod_anti_affinity(scheduler, node, pod);
        
        int affinity_score = (na_score + paf_score + paaf_score) / 3;
        total_score += (affinity_score * scheduler->weight_affinity) / 100;
        weights_sum += scheduler->weight_affinity;
    }
    
    // Topology spread scoring (10% weight)
    if (scheduler->enable_topology_spread) {
        int ts_score = scoring_topology_spread(scheduler, node, pod);
        int topology_weight = 100 - weights_sum;  // Use remaining weight
        if (topology_weight > 0) {
            total_score += (ts_score * topology_weight) / 100;
        }
    }
    
    // Normalize to 0-100 range
    int final_score = (weights_sum > 0) ? (total_score / (weights_sum / 100)) : 0;
    final_score = (final_score > 100) ? 100 : final_score;
    final_score = (final_score < 0) ? 0 : final_score;
    
    return final_score;
}
