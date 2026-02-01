#include "cluster_status.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

static k8s_cluster_status_t* g_cluster_status = NULL;

// ============ Cluster Capacity ============

k8s_cluster_capacity_t* k8s_cluster_capacity_new() {
    k8s_cluster_capacity_t* capacity = (k8s_cluster_capacity_t*)malloc(sizeof(k8s_cluster_capacity_t));
    if (!capacity) return NULL;
    
    capacity->total_cpu_millicores = 0;
    capacity->total_memory_bytes = 0;
    capacity->total_storage_bytes = 0;
    capacity->allocatable_cpu_millicores = 0;
    capacity->allocatable_memory_bytes = 0;
    capacity->allocatable_storage_bytes = 0;
    
    return capacity;
}

void k8s_cluster_capacity_free(k8s_cluster_capacity_t* capacity) {
    free(capacity);
}

int k8s_cluster_capacity_set_total(k8s_cluster_capacity_t* capacity, long long cpu_m, long long mem_b, long long stor_b) {
    if (!capacity) return -1;
    
    capacity->total_cpu_millicores = cpu_m;
    capacity->total_memory_bytes = mem_b;
    capacity->total_storage_bytes = stor_b;
    
    return 0;
}

int k8s_cluster_capacity_set_allocatable(k8s_cluster_capacity_t* capacity, long long cpu_m, long long mem_b, long long stor_b) {
    if (!capacity) return -1;
    
    capacity->allocatable_cpu_millicores = cpu_m;
    capacity->allocatable_memory_bytes = mem_b;
    capacity->allocatable_storage_bytes = stor_b;
    
    return 0;
}

int k8s_cluster_capacity_cpu_utilization(k8s_cluster_capacity_t* capacity, k8s_cluster_usage_t* usage) {
    if (!capacity || !usage) return 0;
    if (capacity->allocatable_cpu_millicores == 0) return 0;
    
    return (int)((usage->used_cpu_millicores * 100) / capacity->allocatable_cpu_millicores);
}

int k8s_cluster_capacity_memory_utilization(k8s_cluster_capacity_t* capacity, k8s_cluster_usage_t* usage) {
    if (!capacity || !usage) return 0;
    if (capacity->allocatable_memory_bytes == 0) return 0;
    
    return (int)((usage->used_memory_bytes * 100) / capacity->allocatable_memory_bytes);
}

int k8s_cluster_capacity_storage_utilization(k8s_cluster_capacity_t* capacity, k8s_cluster_usage_t* usage) {
    if (!capacity || !usage) return 0;
    if (capacity->allocatable_storage_bytes == 0) return 0;
    
    return (int)((usage->used_storage_bytes * 100) / capacity->allocatable_storage_bytes);
}

// ============ Cluster Status ============

k8s_cluster_status_t* k8s_cluster_status_new(const char* cluster_name) {
    if (!cluster_name) return NULL;
    
    k8s_cluster_status_t* status = (k8s_cluster_status_t*)malloc(sizeof(k8s_cluster_status_t));
    if (!status) return NULL;
    
    status->cluster_name = (char*)malloc(strlen(cluster_name) + 1);
    if (!status->cluster_name) { free(status); return NULL; }
    strcpy(status->cluster_name, cluster_name);
    
    status->kubernetes_version = (char*)malloc(32);
    if (!status->kubernetes_version) { free(status->cluster_name); free(status); return NULL; }
    strcpy(status->kubernetes_version, "1.24.0");  // Default version
    
    status->cluster_creation_time = time(NULL);
    status->last_update_time = time(NULL);
    
    // Initialize capacity and usage
    status->capacity.total_cpu_millicores = 0;
    status->capacity.total_memory_bytes = 0;
    status->capacity.total_storage_bytes = 0;
    status->capacity.allocatable_cpu_millicores = 0;
    status->capacity.allocatable_memory_bytes = 0;
    status->capacity.allocatable_storage_bytes = 0;
    
    status->usage.used_cpu_millicores = 0;
    status->usage.used_memory_bytes = 0;
    status->usage.used_storage_bytes = 0;
    status->usage.num_pods = 0;
    status->usage.num_nodes = 0;
    status->usage.num_deployments = 0;
    status->usage.num_services = 0;
    
    // Initialize status summaries
    status->node_status.total_nodes = 0;
    status->node_status.ready_nodes = 0;
    status->node_status.not_ready_nodes = 0;
    status->node_status.unknown_nodes = 0;
    
    status->pod_status.total_pods = 0;
    status->pod_status.running_pods = 0;
    status->pod_status.pending_pods = 0;
    status->pod_status.failed_pods = 0;
    status->pod_status.succeeded_pods = 0;
    status->pod_status.unknown_pods = 0;
    
    status->conditions = NULL;
    status->num_conditions = 0;
    status->health_score = 100;
    
    return status;
}

void k8s_cluster_status_free(k8s_cluster_status_t* status) {
    if (!status) return;
    
    free(status->cluster_name);
    free(status->kubernetes_version);
    
    if (status->conditions) {
        for (int i = 0; i < status->num_conditions; i++) {
            free(status->conditions[i]);
        }
        free(status->conditions);
    }
    
    free(status);
}

int k8s_cluster_status_set_kubernetes_version(k8s_cluster_status_t* status, const char* version) {
    if (!status || !version) return -1;
    if (strlen(version) >= 32) return -1;
    
    strcpy(status->kubernetes_version, version);
    return 0;
}

int k8s_cluster_status_set_capacity(k8s_cluster_status_t* status, k8s_cluster_capacity_t* capacity) {
    if (!status || !capacity) return -1;
    
    status->capacity = *capacity;
    return 0;
}

int k8s_cluster_status_update_usage(k8s_cluster_status_t* status, k8s_cluster_usage_t* usage) {
    if (!status || !usage) return -1;
    
    status->usage = *usage;
    status->last_update_time = time(NULL);
    
    return 0;
}

int k8s_cluster_status_set_node_status(k8s_cluster_status_t* status, int total, int ready, int not_ready, int unknown) {
    if (!status || total < 0) return -1;
    
    status->node_status.total_nodes = total;
    status->node_status.ready_nodes = ready;
    status->node_status.not_ready_nodes = not_ready;
    status->node_status.unknown_nodes = unknown;
    
    return 0;
}

int k8s_cluster_status_set_pod_status(k8s_cluster_status_t* status, int total, int running, int pending, int failed, int succeeded, int unknown) {
    if (!status || total < 0) return -1;
    
    status->pod_status.total_pods = total;
    status->pod_status.running_pods = running;
    status->pod_status.pending_pods = pending;
    status->pod_status.failed_pods = failed;
    status->pod_status.succeeded_pods = succeeded;
    status->pod_status.unknown_pods = unknown;
    
    return 0;
}

int k8s_cluster_status_add_condition(k8s_cluster_status_t* status, const char* condition) {
    if (!status || !condition || status->num_conditions >= 20) return -1;
    
    char** new_conditions = (char**)realloc(status->conditions, (status->num_conditions + 1) * sizeof(char*));
    if (!new_conditions) return -1;
    
    status->conditions = new_conditions;
    status->conditions[status->num_conditions] = (char*)malloc(strlen(condition) + 1);
    if (!status->conditions[status->num_conditions]) return -1;
    
    strcpy(status->conditions[status->num_conditions], condition);
    status->num_conditions++;
    
    return 0;
}

void k8s_cluster_status_clear_conditions(k8s_cluster_status_t* status) {
    if (!status) return;
    
    if (status->conditions) {
        for (int i = 0; i < status->num_conditions; i++) {
            free(status->conditions[i]);
        }
        free(status->conditions);
        status->conditions = NULL;
    }
    
    status->num_conditions = 0;
}

int k8s_cluster_status_calculate_health_score(k8s_cluster_status_t* status) {
    if (!status) return 0;
    
    int score = 100;
    
    // Deduct points for unhealthy nodes
    if (status->node_status.total_nodes > 0) {
        int unhealthy = status->node_status.not_ready_nodes + status->node_status.unknown_nodes;
        int node_penalty = (unhealthy * 20) / status->node_status.total_nodes;
        score -= node_penalty;
    }
    
    // Deduct points for failed pods
    if (status->pod_status.total_pods > 0) {
        int unhealthy = status->pod_status.failed_pods;
        int pod_penalty = (unhealthy * 10) / status->pod_status.total_pods;
        score -= pod_penalty;
    }
    
    // Deduct points for high resource utilization (>90%)
    int cpu_util = k8s_cluster_capacity_cpu_utilization(&status->capacity, &status->usage);
    int mem_util = k8s_cluster_capacity_memory_utilization(&status->capacity, &status->usage);
    
    if (cpu_util > 90 || mem_util > 90) {
        score -= 15;
    } else if (cpu_util > 75 || mem_util > 75) {
        score -= 5;
    }
    
    // Ensure score stays in range [0, 100]
    if (score < 0) score = 0;
    if (score > 100) score = 100;
    
    status->health_score = score;
    return score;
}

char* k8s_cluster_status_to_json(k8s_cluster_status_t* status) {
    if (!status) return NULL;
    
    char* json = (char*)malloc(4096);
    if (!json) return NULL;
    
    k8s_cluster_status_calculate_health_score(status);
    
    int cpu_util = k8s_cluster_capacity_cpu_utilization(&status->capacity, &status->usage);
    int mem_util = k8s_cluster_capacity_memory_utilization(&status->capacity, &status->usage);
    
    sprintf(json,
        "{\"clusterName\":\"%s\",\"kubernetesVersion\":\"%s\",\"healthScore\":%d,"
        "\"nodes\":{\"total\":%d,\"ready\":%d,\"notReady\":%d},"
        "\"pods\":{\"total\":%d,\"running\":%d,\"pending\":%d,\"failed\":%d},"
        "\"resourceUtilization\":{\"cpuPercent\":%d,\"memoryPercent\":%d}}",
        status->cluster_name, status->kubernetes_version, status->health_score,
        status->node_status.total_nodes, status->node_status.ready_nodes, status->node_status.not_ready_nodes,
        status->pod_status.total_pods, status->pod_status.running_pods, status->pod_status.pending_pods, status->pod_status.failed_pods,
        cpu_util, mem_util
    );
    
    return json;
}

char* k8s_cluster_status_summary(k8s_cluster_status_t* status) {
    if (!status) return NULL;
    
    char* summary = (char*)malloc(1024);
    if (!summary) return NULL;
    
    k8s_cluster_status_calculate_health_score(status);
    
    sprintf(summary,
        "Cluster: %s\n"
        "Version: %s\n"
        "Health: %d%%\n"
        "Nodes: %d/%d ready\n"
        "Pods: %d running, %d pending, %d failed\n",
        status->cluster_name, status->kubernetes_version, status->health_score,
        status->node_status.ready_nodes, status->node_status.total_nodes,
        status->pod_status.running_pods, status->pod_status.pending_pods, status->pod_status.failed_pods
    );
    
    return summary;
}

// ============ Global Instance ============

k8s_cluster_status_t* k8s_cluster_status_global() {
    if (!g_cluster_status) {
        g_cluster_status = k8s_cluster_status_new("sirah-unikernel");
    }
    return g_cluster_status;
}
