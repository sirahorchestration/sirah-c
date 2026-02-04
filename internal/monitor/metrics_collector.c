/*
 * metrics_collector.c
 *
 * Per-instance metrics collection implementation
 */

#include "metrics_collector.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/**
 * Create metrics collector
 */
metrics_collector_t* metrics_collector_create(const char *instance_id,
                                              const char *pod_name,
                                              const char *namespace,
                                              uint32_t pid,
                                              uint64_t memory_limit,
                                              uint64_t cpu_limit_millicores) {
    if (!instance_id || !pod_name || !namespace) {
        return NULL;
    }
    
    metrics_collector_t *collector = malloc(sizeof(metrics_collector_t));
    if (!collector) return NULL;
    
    memset(collector, 0, sizeof(metrics_collector_t));
    
    collector->instance_id = strdup(instance_id);
    collector->pod_name = strdup(pod_name);
    collector->namespace = strdup(namespace);
    
    if (!collector->instance_id || !collector->pod_name || !collector->namespace) {
        free(collector->instance_id);
        free(collector->pod_name);
        free(collector->namespace);
        free(collector);
        return NULL;
    }
    
    collector->pid = pid;
    collector->memory_limit_bytes = memory_limit;
    collector->cpu_limit_millicores = cpu_limit_millicores;
    collector->sample_interval_seconds = 10;  // Default 10-second sampling
    
    // Initialize metrics
    collector->current_metrics.instance_id = strdup(instance_id);
    collector->current_metrics.pod_name = strdup(pod_name);
    collector->current_metrics.namespace = strdup(namespace);
    collector->current_metrics.memory_limit_bytes = memory_limit;
    
    // Initialize history (keep last 60 samples = 10 minutes at 10s intervals)
    collector->history.max_snapshots = 60;
    collector->history.snapshots = calloc(60, sizeof(instance_metrics_t));
    collector->history.snapshot_count = 0;
    collector->history.index = 0;
    
    pthread_mutex_init(&collector->mutex, NULL);
    
    return collector;
}

/**
 * Free metrics collector
 */
void metrics_collector_free(metrics_collector_t *collector) {
    if (!collector) return;
    
    free(collector->instance_id);
    free(collector->pod_name);
    free(collector->namespace);
    free(collector->current_metrics.instance_id);
    free(collector->current_metrics.pod_name);
    free(collector->current_metrics.namespace);
    
    for (uint32_t i = 0; i < collector->history.snapshot_count; i++) {
        free(collector->history.snapshots[i].instance_id);
        free(collector->history.snapshots[i].pod_name);
        free(collector->history.snapshots[i].namespace);
    }
    free(collector->history.snapshots);
    
    pthread_mutex_destroy(&collector->mutex);
    free(collector);
}

/**
 * Read /proc stats for process
 */
static bool read_proc_stats(uint32_t pid, uint64_t *out_cpu_time, uint64_t *out_memory) {
    if (!out_cpu_time || !out_memory) return false;
    
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    
    FILE *f = fopen(path, "r");
    if (!f) return false;
    
    // Parse /proc/[pid]/stat
    // Format: pid (comm) state ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt utime stime ...
    uint64_t utime, stime;
    int result = fscanf(f, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu",
                       &utime, &stime);
    fclose(f);
    
    if (result != 2) return false;
    
    // Convert to milliseconds (assuming 100 Hz clock)
    *out_cpu_time = (utime + stime) * 10;  // 10ms per tick at 100Hz
    
    // Read memory from /proc/[pid]/status
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    f = fopen(path, "r");
    if (!f) return false;
    
    char line[256];
    *out_memory = 0;
    while (fgets(line, sizeof(line), f)) {
        uint64_t memory_kb;
        if (sscanf(line, "VmRSS: %lu", &memory_kb) == 1) {
            *out_memory = memory_kb * 1024;  // Convert to bytes
            break;
        }
    }
    fclose(f);
    
    return true;
}

/**
 * Collect current metrics
 */
bool metrics_collector_collect(metrics_collector_t *collector) {
    if (!collector) return false;
    
    pthread_mutex_lock(&collector->mutex);
    
    time_t now = time(NULL);
    
    // Check if enough time has passed since last collection
    if (collector->last_collection != 0 &&
        (now - collector->last_collection) < collector->sample_interval_seconds) {
        pthread_mutex_unlock(&collector->mutex);
        return false;  // Not time to collect yet
    }
    
    // Read CPU and memory stats
    uint64_t cpu_time_ms = 0;
    uint64_t memory_bytes = 0;
    
    if (!read_proc_stats(collector->pid, &cpu_time_ms, &memory_bytes)) {
        pthread_mutex_unlock(&collector->mutex);
        return false;
    }
    
    // Calculate CPU usage percentage
    uint32_t cpu_usage_percent = 0;
    if (collector->last_collection != 0) {
        uint64_t time_delta_ms = (now - collector->last_collection) * 1000;
        uint64_t cpu_delta = cpu_time_ms - collector->prev_cpu_time_ms;
        
        if (time_delta_ms > 0) {
            // cpu_usage_percent = (cpu_delta / time_delta) * 100
            cpu_usage_percent = (uint32_t)((cpu_delta * 100) / time_delta_ms);
            if (cpu_usage_percent > 100) cpu_usage_percent = 100;
        }
    }
    
    // Calculate memory usage percentage
    uint32_t memory_percent = 0;
    if (collector->memory_limit_bytes > 0) {
        memory_percent = (uint32_t)((memory_bytes * 100) / collector->memory_limit_bytes);
        if (memory_percent > 100) memory_percent = 100;
    }
    
    // Update current metrics
    collector->current_metrics.cpu_usage_percent = cpu_usage_percent;
    collector->current_metrics.cpu_time_ms = cpu_time_ms;
    collector->current_metrics.memory_usage_bytes = memory_bytes;
    collector->current_metrics.memory_usage_percent = memory_percent;
    collector->current_metrics.is_running = (access("/proc/" STRINGIFY(collector->pid) "/stat", F_OK) == 0);
    collector->current_metrics.collection_time = now;
    
    if (collector->last_collection == 0) {
        collector->current_metrics.uptime_seconds = 0;
    } else {
        collector->current_metrics.uptime_seconds += (now - collector->last_collection);
    }
    
    // Add to history (circular buffer)
    uint32_t idx = collector->history.index;
    if (collector->history.snapshots[idx].instance_id) {
        free(collector->history.snapshots[idx].instance_id);
        free(collector->history.snapshots[idx].pod_name);
        free(collector->history.snapshots[idx].namespace);
    }
    
    memcpy(&collector->history.snapshots[idx], &collector->current_metrics, 
           sizeof(instance_metrics_t));
    collector->history.snapshots[idx].instance_id = strdup(collector->current_metrics.instance_id);
    collector->history.snapshots[idx].pod_name = strdup(collector->current_metrics.pod_name);
    collector->history.snapshots[idx].namespace = strdup(collector->current_metrics.namespace);
    
    collector->history.index = (idx + 1) % collector->history.max_snapshots;
    if (collector->history.snapshot_count < collector->history.max_snapshots) {
        collector->history.snapshot_count++;
    }
    
    // Update previous values for next delta calculation
    collector->prev_cpu_time_ms = cpu_time_ms;
    collector->last_collection = now;
    
    pthread_mutex_unlock(&collector->mutex);
    return true;
}

/**
 * Get current metrics snapshot
 */
bool metrics_collector_get_current(metrics_collector_t *collector,
                                    instance_metrics_t *out_metrics) {
    if (!collector || !out_metrics) return false;
    
    pthread_mutex_lock(&collector->mutex);
    memcpy(out_metrics, &collector->current_metrics, sizeof(instance_metrics_t));
    pthread_mutex_unlock(&collector->mutex);
    
    return true;
}

/**
 * Get metrics history
 */
bool metrics_collector_get_history(metrics_collector_t *collector,
                                    instance_metrics_t **out_history,
                                    uint32_t *out_count) {
    if (!collector || !out_history || !out_count) return false;
    
    pthread_mutex_lock(&collector->mutex);
    
    *out_count = collector->history.snapshot_count;
    if (*out_count == 0) {
        *out_history = NULL;
        pthread_mutex_unlock(&collector->mutex);
        return false;
    }
    
    *out_history = malloc((*out_count) * sizeof(instance_metrics_t));
    if (!*out_history) {
        pthread_mutex_unlock(&collector->mutex);
        return false;
    }
    
    // Copy history in chronological order
    for (uint32_t i = 0; i < *out_count; i++) {
        uint32_t idx = (collector->history.index + i) % collector->history.max_snapshots;
        memcpy(&(*out_history)[i], &collector->history.snapshots[idx], 
               sizeof(instance_metrics_t));
    }
    
    pthread_mutex_unlock(&collector->mutex);
    return true;
}

/**
 * Get average metrics over time window
 */
bool metrics_collector_get_average(metrics_collector_t *collector,
                                    uint32_t time_window_seconds,
                                    instance_metrics_t *out_average) {
    if (!collector || !out_average) return false;
    
    pthread_mutex_lock(&collector->mutex);
    
    if (collector->history.snapshot_count == 0) {
        pthread_mutex_unlock(&collector->mutex);
        return false;
    }
    
    memset(out_average, 0, sizeof(instance_metrics_t));
    
    time_t cutoff_time = time(NULL) - time_window_seconds;
    uint32_t sample_count = 0;
    
    for (uint32_t i = 0; i < collector->history.snapshot_count; i++) {
        uint32_t idx = (collector->history.index + i) % collector->history.max_snapshots;
        instance_metrics_t *snapshot = &collector->history.snapshots[idx];
        
        if (snapshot->collection_time >= cutoff_time) {
            out_average->cpu_usage_percent += snapshot->cpu_usage_percent;
            out_average->memory_usage_bytes += snapshot->memory_usage_bytes;
            out_average->memory_usage_percent += snapshot->memory_usage_percent;
            sample_count++;
        }
    }
    
    if (sample_count > 0) {
        out_average->cpu_usage_percent /= sample_count;
        out_average->memory_usage_bytes /= sample_count;
        out_average->memory_usage_percent /= sample_count;
    }
    
    out_average->instance_id = collector->instance_id;
    out_average->pod_name = collector->pod_name;
    out_average->namespace = collector->namespace;
    
    pthread_mutex_unlock(&collector->mutex);
    return sample_count > 0;
}

/**
 * Check if memory exceeds limit
 */
bool metrics_collector_exceeds_memory(metrics_collector_t *collector) {
    if (!collector) return false;
    
    pthread_mutex_lock(&collector->mutex);
    bool exceeds = collector->current_metrics.memory_usage_percent >= 90;
    pthread_mutex_unlock(&collector->mutex);
    
    return exceeds;
}

/**
 * Check if CPU exceeds limit
 */
bool metrics_collector_exceeds_cpu(metrics_collector_t *collector) {
    if (!collector) return false;
    
    pthread_mutex_lock(&collector->mutex);
    bool exceeds = collector->current_metrics.cpu_usage_percent >= 80;
    pthread_mutex_unlock(&collector->mutex);
    
    return exceeds;
}

/**
 * Check if network exceeds threshold
 */
bool metrics_collector_exceeds_network(metrics_collector_t *collector, uint64_t threshold_bytes) {
    if (!collector) return false;
    
    pthread_mutex_lock(&collector->mutex);
    bool exceeds = (collector->current_metrics.network_rx_bytes + 
                   collector->current_metrics.network_tx_bytes) >= threshold_bytes;
    pthread_mutex_unlock(&collector->mutex);
    
    return exceeds;
}

/**
 * Free metrics snapshot
 */
void metrics_collector_free_metrics(instance_metrics_t *metrics) {
    if (!metrics) return;
    free(metrics->instance_id);
    free(metrics->pod_name);
    free(metrics->namespace);
    free(metrics);
}

#define STRINGIFY(x) #x
