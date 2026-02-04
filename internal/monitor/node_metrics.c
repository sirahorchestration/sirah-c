/*
 * node_metrics.c
 *
 * Node metrics collection implementation
 */

#include "node_metrics.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * Create node metrics collector
 */
node_metrics_collector_t* node_metrics_create(const char *node_name) {
    if (!node_name) return NULL;
    
    node_metrics_collector_t *collector = malloc(sizeof(node_metrics_collector_t));
    if (!collector) return NULL;
    
    memset(collector, 0, sizeof(node_metrics_collector_t));
    
    collector->node_name = strdup(node_name);
    if (!collector->node_name) {
        free(collector);
        return NULL;
    }
    
    collector->current_metrics.node_name = strdup(node_name);
    collector->sample_interval_seconds = 10;
    
    pthread_mutex_init(&collector->mutex, NULL);
    
    return collector;
}

/**
 * Free node metrics collector
 */
void node_metrics_free(node_metrics_collector_t *collector) {
    if (!collector) return;
    
    free(collector->node_name);
    free(collector->current_metrics.node_name);
    pthread_mutex_destroy(&collector->mutex);
    free(collector);
}

/**
 * Read CPU stats from /proc/stat
 */
static bool read_cpu_stats(uint32_t *out_cpu_percent, uint64_t *out_cores) {
    if (!out_cpu_percent || !out_cores) return false;
    
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return false;
    
    // Count CPU cores
    *out_cores = 0;
    uint64_t user, nice, system, idle, iowait;
    char cpu_label[16];
    
    while (fscanf(f, "%s", cpu_label) == 1) {
        if (strncmp(cpu_label, "cpu", 3) == 0 && strlen(cpu_label) > 3) {
            (*out_cores)++;
            // Skip the rest of this line
            while (fgetc(f) != '\n' && !feof(f));
        } else if (strcmp(cpu_label, "cpu") == 0) {
            // This is the aggregate CPU line
            fscanf(f, "%lu %lu %lu %lu %lu", &user, &nice, &system, &idle, &iowait);
            break;
        } else {
            // Skip other lines
            while (fgetc(f) != '\n' && !feof(f));
        }
    }
    
    fclose(f);
    
    // Calculate CPU usage percentage
    uint64_t total = user + nice + system + idle + iowait;
    uint64_t busy = user + nice + system;
    *out_cpu_percent = (total > 0) ? (uint32_t)((busy * 100) / total) : 0;
    
    return true;
}

/**
 * Read memory stats from /proc/meminfo
 */
static bool read_memory_stats(uint64_t *out_total, uint64_t *out_used, uint64_t *out_available) {
    if (!out_total || !out_used || !out_available) return false;
    
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return false;
    
    char line[256];
    uint64_t mem_total_kb = 0, mem_available_kb = 0, mem_cached_kb = 0, mem_buffers_kb = 0;
    
    while (fgets(line, sizeof(line), f)) {
        uint64_t kb;
        if (sscanf(line, "MemTotal: %lu kB", &kb) == 1) {
            mem_total_kb = kb;
        } else if (sscanf(line, "MemAvailable: %lu kB", &kb) == 1) {
            mem_available_kb = kb;
        } else if (sscanf(line, "Cached: %lu kB", &kb) == 1) {
            mem_cached_kb = kb;
        } else if (sscanf(line, "Buffers: %lu kB", &kb) == 1) {
            mem_buffers_kb = kb;
        }
    }
    
    fclose(f);
    
    *out_total = mem_total_kb * 1024;  // Convert to bytes
    *out_available = mem_available_kb * 1024;
    *out_used = *out_total - *out_available;
    
    return mem_total_kb > 0;
}

/**
 * Collect current node metrics
 */
bool node_metrics_collect(node_metrics_collector_t *collector) {
    if (!collector) return false;
    
    pthread_mutex_lock(&collector->mutex);
    
    time_t now = time(NULL);
    
    // Check if enough time has passed
    if (collector->last_collection != 0 &&
        (now - collector->last_collection) < collector->sample_interval_seconds) {
        pthread_mutex_unlock(&collector->mutex);
        return false;
    }
    
    uint32_t cpu_percent = 0;
    uint64_t cpu_cores = 0;
    if (!read_cpu_stats(&cpu_percent, &cpu_cores)) {
        pthread_mutex_unlock(&collector->mutex);
        return false;
    }
    
    uint64_t mem_total = 0, mem_used = 0, mem_available = 0;
    if (!read_memory_stats(&mem_total, &mem_used, &mem_available)) {
        pthread_mutex_unlock(&collector->mutex);
        return false;
    }
    
    // Update metrics
    collector->current_metrics.total_cpu_percent = cpu_percent;
    collector->current_metrics.available_cpu_percent = (cpu_percent > 100) ? 0 : (100 - cpu_percent);
    collector->current_metrics.cpu_cores = cpu_cores;
    
    collector->current_metrics.total_memory_bytes = mem_total;
    collector->current_metrics.used_memory_bytes = mem_used;
    collector->current_metrics.available_memory_bytes = mem_available;
    collector->current_metrics.memory_usage_percent = 
        (mem_total > 0) ? (uint32_t)((mem_used * 100) / mem_total) : 0;
    
    collector->current_metrics.collection_time = now;
    collector->last_collection = now;
    
    pthread_mutex_unlock(&collector->mutex);
    return true;
}

/**
 * Get current node metrics
 */
bool node_metrics_get_current(node_metrics_collector_t *collector,
                               node_metrics_t *out_metrics) {
    if (!collector || !out_metrics) return false;
    
    pthread_mutex_lock(&collector->mutex);
    memcpy(out_metrics, &collector->current_metrics, sizeof(node_metrics_t));
    pthread_mutex_unlock(&collector->mutex);
    
    return true;
}

/**
 * Update pod count on node
 */
bool node_metrics_update_pod_count(node_metrics_collector_t *collector,
                                    uint32_t running_count,
                                    uint32_t failed_count) {
    if (!collector) return false;
    
    pthread_mutex_lock(&collector->mutex);
    
    collector->current_metrics.running_pods = running_count;
    collector->current_metrics.failed_pods = failed_count;
    collector->current_metrics.pod_count = running_count + failed_count;
    
    pthread_mutex_unlock(&collector->mutex);
    return true;
}
