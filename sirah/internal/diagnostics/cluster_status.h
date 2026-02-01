#ifndef K8S_CLUSTER_STATUS_H
#define K8S_CLUSTER_STATUS_H

#include <time.h>

// Cluster capacity and allocatable resources
typedef struct {
    long long total_cpu_millicores;
    long long total_memory_bytes;
    long long total_storage_bytes;
    
    long long allocatable_cpu_millicores;
    long long allocatable_memory_bytes;
    long long allocatable_storage_bytes;
} k8s_cluster_capacity_t;

// Cluster usage metrics
typedef struct {
    long long used_cpu_millicores;
    long long used_memory_bytes;
    long long used_storage_bytes;
    
    int num_pods;
    int num_nodes;
    int num_deployments;
    int num_services;
} k8s_cluster_usage_t;

// Node status summary
typedef struct {
    int total_nodes;
    int ready_nodes;
    int not_ready_nodes;
    int unknown_nodes;
} k8s_node_status_summary_t;

// Pod status summary
typedef struct {
    int total_pods;
    int running_pods;
    int pending_pods;
    int failed_pods;
    int succeeded_pods;
    int unknown_pods;
} k8s_pod_status_summary_t;

// Full cluster status
typedef struct {
    char* cluster_name;
    char* kubernetes_version;
    time_t cluster_creation_time;
    time_t last_update_time;
    
    k8s_cluster_capacity_t capacity;
    k8s_cluster_usage_t usage;
    
    k8s_node_status_summary_t node_status;
    k8s_pod_status_summary_t pod_status;
    
    // Cluster conditions
    char** conditions;
    int num_conditions;
    
    // Health score (0-100)
    int health_score;
} k8s_cluster_status_t;

// ============ Cluster Capacity ============

k8s_cluster_capacity_t* k8s_cluster_capacity_new();
void k8s_cluster_capacity_free(k8s_cluster_capacity_t* capacity);

int k8s_cluster_capacity_set_total(k8s_cluster_capacity_t* capacity, long long cpu_m, long long mem_b, long long stor_b);
int k8s_cluster_capacity_set_allocatable(k8s_cluster_capacity_t* capacity, long long cpu_m, long long mem_b, long long stor_b);

// Get utilization percentage
int k8s_cluster_capacity_cpu_utilization(k8s_cluster_capacity_t* capacity, k8s_cluster_usage_t* usage);
int k8s_cluster_capacity_memory_utilization(k8s_cluster_capacity_t* capacity, k8s_cluster_usage_t* usage);
int k8s_cluster_capacity_storage_utilization(k8s_cluster_capacity_t* capacity, k8s_cluster_usage_t* usage);

// ============ Cluster Status ============

k8s_cluster_status_t* k8s_cluster_status_new(const char* cluster_name);
void k8s_cluster_status_free(k8s_cluster_status_t* status);

// Configuration
int k8s_cluster_status_set_kubernetes_version(k8s_cluster_status_t* status, const char* version);
int k8s_cluster_status_set_capacity(k8s_cluster_status_t* status, k8s_cluster_capacity_t* capacity);

// Usage tracking
int k8s_cluster_status_update_usage(k8s_cluster_status_t* status, k8s_cluster_usage_t* usage);

// Status summaries
int k8s_cluster_status_set_node_status(k8s_cluster_status_t* status, int total, int ready, int not_ready, int unknown);
int k8s_cluster_status_set_pod_status(k8s_cluster_status_t* status, int total, int running, int pending, int failed, int succeeded, int unknown);

// Conditions
int k8s_cluster_status_add_condition(k8s_cluster_status_t* status, const char* condition);
void k8s_cluster_status_clear_conditions(k8s_cluster_status_t* status);

// Health score calculation (0-100)
int k8s_cluster_status_calculate_health_score(k8s_cluster_status_t* status);

// Serialization
char* k8s_cluster_status_to_json(k8s_cluster_status_t* status);
char* k8s_cluster_status_summary(k8s_cluster_status_t* status);

// Global instance
k8s_cluster_status_t* k8s_cluster_status_global();

#endif
