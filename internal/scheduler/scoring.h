#ifndef SIRAH_SCHEDULER_SCORING_H
#define SIRAH_SCHEDULER_SCORING_H

#include "scheduler.h"

// Scoring algorithm result
typedef struct {
    int score;
    char* reason;
} score_result_t;

// Bin-packing scoring (prefer nodes with least remaining capacity)
int scoring_bin_packing(node_state_t* node, pod_request_t* request);

// Pod spread scoring (prefer nodes with fewer pods)
int scoring_pod_spread(node_state_t* node, int total_pods_in_cluster);

// Pod affinity scoring (prefer nodes with matching pods)
int scoring_pod_affinity(scheduler_t* scheduler, node_state_t* node, 
                         pod_spec_t* pod);

// Pod anti-affinity scoring (penalize nodes with anti-affinity pods)
int scoring_pod_anti_affinity(scheduler_t* scheduler, node_state_t* node, 
                              pod_spec_t* pod);

// Node affinity scoring (prefer nodes with matching labels)
int scoring_node_affinity(node_state_t* node, pod_spec_t* pod);

// Topology spread scoring (prefer nodes that improve spread)
int scoring_topology_spread(scheduler_t* scheduler, node_state_t* node, 
                           pod_spec_t* pod);

// Combined scoring (weighted sum of all scoring functions)
int scoring_combined(scheduler_t* scheduler, node_state_t* node, 
                    pod_spec_t* pod);

#endif // SIRAH_SCHEDULER_SCORING_H
