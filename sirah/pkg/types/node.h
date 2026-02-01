// pkg/types/node.h
#ifndef SIRAH_TYPES_NODE_H
#define SIRAH_TYPES_NODE_H

#include "common.h"
#include <time.h>

typedef enum {
    NODE_STATUS_UNKNOWN = 0,
    NODE_STATUS_READY,
    NODE_STATUS_NOT_READY,
    NODE_STATUS_TERMINATING
} k8s_node_status_t;

typedef struct {
    char hostname[256];
    char ip_address[16];
    uint32_t cpu_cores;
    uint64_t memory_bytes;
    uint64_t disk_bytes;
} k8s_node_capacity_t;

// Forward declaration for taint type (defined in affinity.h)
typedef struct k8s_taint k8s_taint_t;

// Node spec with taints
typedef struct {
    k8s_taint_t** taints;  // Node taints
    int num_taints;
} k8s_node_spec_t;

typedef struct {
    k8s_metadata_t metadata;
    k8s_node_capacity_t capacity;
    k8s_node_spec_t spec;        // Node spec with taints
    k8s_node_status_t status;
    time_t last_heartbeat;
    char labels[512];  // JSON-encoded labels
} k8s_node_t;

// Node operations
int node_to_json(const k8s_node_t* node, char* buffer);
int node_from_json(const char* json_str, k8s_node_t* node);

// Node initialization
static inline void k8s_node_init(k8s_node_t* node) {
    if (!node) return;
    node->spec.taints = NULL;
    node->spec.num_taints = 0;
}

#endif
