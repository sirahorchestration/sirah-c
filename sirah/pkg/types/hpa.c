#include "hpa.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdbool.h>

// ============ Spec Lifecycle ============

k8s_hpa_spec_t* k8s_hpa_spec_new() {
    k8s_hpa_spec_t* spec = (k8s_hpa_spec_t*)malloc(sizeof(k8s_hpa_spec_t));
    if (!spec) return NULL;
    
    spec->scale_target_ref = NULL;
    spec->min_replicas = 1;
    spec->max_replicas = 10;
    spec->target_cpu_utilization = 80;
    spec->target_memory_utilization = 80;
    spec->custom_metrics = NULL;
    spec->num_custom_metrics = 0;
    spec->upscale_window_seconds = 60;
    spec->downscale_window_seconds = 300;
    spec->upscale_policy_percent = 50;
    spec->downscale_policy_percent = 50;
    
    return spec;
}

void k8s_hpa_spec_free(k8s_hpa_spec_t* spec) {
    if (!spec) return;
    
    free(spec->scale_target_ref);
    
    if (spec->custom_metrics) {
        for (int i = 0; i < spec->num_custom_metrics; i++) {
            if (spec->custom_metrics[i]) {
                free(spec->custom_metrics[i]->name);
                free(spec->custom_metrics[i]);
            }
        }
        free(spec->custom_metrics);
    }
    
    free(spec);
}

// ============ Status Lifecycle ============

k8s_hpa_status_t* k8s_hpa_status_new() {
    k8s_hpa_status_t* status = (k8s_hpa_status_t*)malloc(sizeof(k8s_hpa_status_t));
    if (!status) return NULL;
    
    status->current_replicas = 1;
    status->desired_replicas = 1;
    status->current_cpu_utilization = 0;
    status->current_memory_utilization = 0;
    status->last_scale_time = time(NULL);
    status->last_scale_replicas = 1;
    status->conditions = NULL;
    status->last_update_time = time(NULL);
    
    return status;
}

void k8s_hpa_status_free(k8s_hpa_status_t* status) {
    if (!status) return;
    
    free(status->conditions);
    free(status);
}

// ============ HPA Lifecycle ============

k8s_hpa_t* k8s_hpa_new(const char* name, const char* namespace) {
    if (!name || !namespace) return NULL;
    
    k8s_hpa_t* hpa = (k8s_hpa_t*)malloc(sizeof(k8s_hpa_t));
    if (!hpa) return NULL;
    
    hpa->metadata.name = (char*)malloc(strlen(name) + 1);
    if (!hpa->metadata.name) { free(hpa); return NULL; }
    strcpy(hpa->metadata.name, name);
    
    hpa->metadata.namespace = (char*)malloc(strlen(namespace) + 1);
    if (!hpa->metadata.namespace) { free(hpa->metadata.name); free(hpa); return NULL; }
    strcpy(hpa->metadata.namespace, namespace);
    
    hpa->metadata.uid = (char*)malloc(37);  // UUID length
    if (!hpa->metadata.uid) { free(hpa->metadata.namespace); free(hpa->metadata.name); free(hpa); return NULL; }
    sprintf(hpa->metadata.uid, "hpa-%ld", time(NULL));
    
    hpa->metadata.labels = NULL;
    hpa->metadata.num_labels = 0;
    hpa->metadata.creation_timestamp = time(NULL);
    
    hpa->spec = k8s_hpa_spec_new();
    if (!hpa->spec) { free(hpa->metadata.uid); free(hpa->metadata.namespace); free(hpa->metadata.name); free(hpa); return NULL; }
    
    hpa->status = k8s_hpa_status_new();
    if (!hpa->status) { k8s_hpa_spec_free(hpa->spec); free(hpa->metadata.uid); free(hpa->metadata.namespace); free(hpa->metadata.name); free(hpa); return NULL; }
    
    hpa->conditions = NULL;
    hpa->num_conditions = 0;
    
    return hpa;
}

void k8s_hpa_free(k8s_hpa_t* hpa) {
    if (!hpa) return;
    
    free(hpa->metadata.name);
    free(hpa->metadata.namespace);
    free(hpa->metadata.uid);
    
    if (hpa->metadata.labels) {
        for (int i = 0; i < hpa->metadata.num_labels; i++) {
            free(hpa->metadata.labels[i]);
        }
        free(hpa->metadata.labels);
    }
    
    k8s_hpa_spec_free(hpa->spec);
    k8s_hpa_status_free(hpa->status);
    
    if (hpa->conditions) {
        for (int i = 0; i < hpa->num_conditions; i++) {
            if (hpa->conditions[i]) {
                free(hpa->conditions[i]->type);
                free(hpa->conditions[i]->status);
                free(hpa->conditions[i]->reason);
                free(hpa->conditions[i]->message);
                free(hpa->conditions[i]);
            }
        }
        free(hpa->conditions);
    }
    
    free(hpa);
}

// ============ Configuration ============

int k8s_hpa_set_scale_target(k8s_hpa_t* hpa, const char* target_ref) {
    if (!hpa || !target_ref) return -1;
    if (!hpa->spec) return -1;
    
    free(hpa->spec->scale_target_ref);
    hpa->spec->scale_target_ref = (char*)malloc(strlen(target_ref) + 1);
    if (!hpa->spec->scale_target_ref) return -1;
    
    strcpy(hpa->spec->scale_target_ref, target_ref);
    return 0;
}

int k8s_hpa_set_replicas(k8s_hpa_t* hpa, int min_replicas, int max_replicas) {
    if (!hpa || !hpa->spec || min_replicas < 1 || max_replicas < min_replicas) return -1;
    
    hpa->spec->min_replicas = min_replicas;
    hpa->spec->max_replicas = max_replicas;
    
    return 0;
}

int k8s_hpa_set_target_cpu(k8s_hpa_t* hpa, int target_percent) {
    if (!hpa || !hpa->spec || target_percent < 1 || target_percent > 100) return -1;
    
    hpa->spec->target_cpu_utilization = target_percent;
    return 0;
}

int k8s_hpa_set_target_memory(k8s_hpa_t* hpa, int target_percent) {
    if (!hpa || !hpa->spec || target_percent < 1 || target_percent > 100) return -1;
    
    hpa->spec->target_memory_utilization = target_percent;
    return 0;
}

int k8s_hpa_add_custom_metric(k8s_hpa_t* hpa, const char* name, k8s_hpa_metric_type_t type, int target_value) {
    if (!hpa || !name || target_value <= 0) return -1;
    if (!hpa->spec) return -1;
    if (hpa->spec->num_custom_metrics >= 10) return -1;  // Max 10 custom metrics
    
    k8s_hpa_metric_spec_t** new_metrics = (k8s_hpa_metric_spec_t**)realloc(
        hpa->spec->custom_metrics,
        (hpa->spec->num_custom_metrics + 1) * sizeof(k8s_hpa_metric_spec_t*)
    );
    if (!new_metrics) return -1;
    
    hpa->spec->custom_metrics = new_metrics;
    
    k8s_hpa_metric_spec_t* metric = (k8s_hpa_metric_spec_t*)malloc(sizeof(k8s_hpa_metric_spec_t));
    if (!metric) return -1;
    
    metric->type = type;
    metric->name = (char*)malloc(strlen(name) + 1);
    if (!metric->name) { free(metric); return -1; }
    strcpy(metric->name, name);
    
    metric->target_value = target_value;
    metric->current_value = 0;
    
    hpa->spec->custom_metrics[hpa->spec->num_custom_metrics] = metric;
    hpa->spec->num_custom_metrics++;
    
    return 0;
}

// ============ Behavior Configuration ============

int k8s_hpa_set_upscale_behavior(k8s_hpa_t* hpa, int window_seconds, int percent) {
    if (!hpa || !hpa->spec || window_seconds < 0 || percent < 0 || percent > 100) return -1;
    
    hpa->spec->upscale_window_seconds = window_seconds;
    hpa->spec->upscale_policy_percent = percent;
    
    return 0;
}

int k8s_hpa_set_downscale_behavior(k8s_hpa_t* hpa, int window_seconds, int percent) {
    if (!hpa || !hpa->spec || window_seconds < 0 || percent < 0 || percent > 100) return -1;
    
    hpa->spec->downscale_window_seconds = window_seconds;
    hpa->spec->downscale_policy_percent = percent;
    
    return 0;
}

// ============ Status Tracking ============

int k8s_hpa_update_current_metrics(k8s_hpa_t* hpa, int cpu_util, int mem_util) {
    if (!hpa || !hpa->status) return -1;
    if (cpu_util < 0 || mem_util < 0) return -1;
    
    hpa->status->current_cpu_utilization = cpu_util;
    hpa->status->current_memory_utilization = mem_util;
    hpa->status->last_update_time = time(NULL);
    
    return 0;
}

int k8s_hpa_set_replicas_count(k8s_hpa_t* hpa, int current, int desired) {
    if (!hpa || !hpa->status || current < 0 || desired < 0) return -1;
    
    hpa->status->current_replicas = current;
    hpa->status->desired_replicas = desired;
    
    return 0;
}

int k8s_hpa_mark_scaled(k8s_hpa_t* hpa, int replica_count) {
    if (!hpa || !hpa->status || replica_count < 1) return -1;
    
    hpa->status->last_scale_time = time(NULL);
    hpa->status->last_scale_replicas = replica_count;
    hpa->status->current_replicas = replica_count;
    
    return 0;
}

// ============ Condition Management ============

int k8s_hpa_add_condition(k8s_hpa_t* hpa, const char* type, const char* status, const char* reason, const char* message) {
    if (!hpa || !type || !status || !reason || !message) return -1;
    if (hpa->num_conditions >= 10) return -1;  // Max 10 conditions
    
    k8s_hpa_condition_t** new_conditions = (k8s_hpa_condition_t**)realloc(
        hpa->conditions,
        (hpa->num_conditions + 1) * sizeof(k8s_hpa_condition_t*)
    );
    if (!new_conditions) return -1;
    
    hpa->conditions = new_conditions;
    
    k8s_hpa_condition_t* condition = (k8s_hpa_condition_t*)malloc(sizeof(k8s_hpa_condition_t));
    if (!condition) return -1;
    
    condition->type = (char*)malloc(strlen(type) + 1);
    if (!condition->type) { free(condition); return -1; }
    strcpy(condition->type, type);
    
    condition->status = (char*)malloc(strlen(status) + 1);
    if (!condition->status) { free(condition->type); free(condition); return -1; }
    strcpy(condition->status, status);
    
    condition->reason = (char*)malloc(strlen(reason) + 1);
    if (!condition->reason) { free(condition->status); free(condition->type); free(condition); return -1; }
    strcpy(condition->reason, reason);
    
    condition->message = (char*)malloc(strlen(message) + 1);
    if (!condition->message) { free(condition->reason); free(condition->status); free(condition->type); free(condition); return -1; }
    strcpy(condition->message, message);
    
    condition->last_transition_time = time(NULL);
    
    hpa->conditions[hpa->num_conditions] = condition;
    hpa->num_conditions++;
    
    return 0;
}

int k8s_hpa_clear_conditions(k8s_hpa_t* hpa) {
    if (!hpa) return -1;
    
    if (hpa->conditions) {
        for (int i = 0; i < hpa->num_conditions; i++) {
            if (hpa->conditions[i]) {
                free(hpa->conditions[i]->type);
                free(hpa->conditions[i]->status);
                free(hpa->conditions[i]->reason);
                free(hpa->conditions[i]->message);
                free(hpa->conditions[i]);
            }
        }
        free(hpa->conditions);
        hpa->conditions = NULL;
    }
    
    hpa->num_conditions = 0;
    return 0;
}

// ============ Serialization ============

char* k8s_hpa_to_json(k8s_hpa_t* hpa) {
    if (!hpa) return NULL;
    
    // Simplified JSON output
    char* json = (char*)malloc(2048);
    if (!json) return NULL;
    
    sprintf(json, 
        "{\"kind\":\"HorizontalPodAutoscaler\",\"metadata\":{\"name\":\"%s\",\"namespace\":\"%s\"},\"spec\":{\"minReplicas\":%d,\"maxReplicas\":%d,\"targetCPUUtilizationPercentage\":%d}}",
        hpa->metadata.name, hpa->metadata.namespace,
        hpa->spec->min_replicas, hpa->spec->max_replicas,
        hpa->spec->target_cpu_utilization
    );
    
    return json;
}

k8s_hpa_t* k8s_hpa_from_json(const char* json_str) {
    // Stub: Implement actual JSON parsing
    return NULL;
}

// ============ Utility ============

bool k8s_hpa_can_scale_up(k8s_hpa_t* hpa) {
    if (!hpa || !hpa->spec || !hpa->status) return false;
    
    // Can scale up if desired < max_replicas and enough time has passed
    if (hpa->status->desired_replicas >= hpa->spec->max_replicas) return false;
    
    time_t now = time(NULL);
    int elapsed = (int)(now - hpa->status->last_scale_time);
    
    // Check upscale window
    if (elapsed < hpa->spec->upscale_window_seconds) return false;
    
    return true;
}

bool k8s_hpa_can_scale_down(k8s_hpa_t* hpa) {
    if (!hpa || !hpa->spec || !hpa->status) return false;
    
    // Can scale down if desired > min_replicas and enough time has passed
    if (hpa->status->desired_replicas <= hpa->spec->min_replicas) return false;
    
    time_t now = time(NULL);
    int elapsed = (int)(now - hpa->status->last_scale_time);
    
    // Check downscale window
    if (elapsed < hpa->spec->downscale_window_seconds) return false;
    
    return true;
}

int k8s_hpa_calculate_desired_replicas(k8s_hpa_t* hpa, int current_utilization, int target_utilization) {
    if (!hpa || !hpa->spec || target_utilization <= 0) return hpa->spec->min_replicas;
    
    if (current_utilization == 0) return hpa->spec->min_replicas;
    
    // Formula: desired_replicas = ceil(current_replicas * (current_util / target_util))
    int desired = (hpa->status->current_replicas * current_utilization + target_utilization - 1) / target_utilization;
    
    // Apply percentage policies
    if (desired > hpa->status->current_replicas) {
        // Upscaling
        int max_increase = (hpa->status->current_replicas * hpa->spec->upscale_policy_percent / 100);
        if (max_increase == 0) max_increase = 1;
        desired = hpa->status->current_replicas + max_increase;
    } else if (desired < hpa->status->current_replicas) {
        // Downscaling
        int max_decrease = (hpa->status->current_replicas * hpa->spec->downscale_policy_percent / 100);
        if (max_decrease == 0) max_decrease = 1;
        desired = hpa->status->current_replicas - max_decrease;
    }
    
    // Clamp to limits
    if (desired < hpa->spec->min_replicas) desired = hpa->spec->min_replicas;
    if (desired > hpa->spec->max_replicas) desired = hpa->spec->max_replicas;
    
    return desired;
}
