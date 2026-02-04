// internal/apiserver/scheduler_integration.h
// Scheduler integration into API Server control plane
// Automatically assigns pending pods to nodes

#ifndef SIRAH_SCHEDULER_INTEGRATION_H
#define SIRAH_SCHEDULER_INTEGRATION_H

/**
 * Initialize scheduler integration
 * Must be called after API server is initialized
 * 
 * @return 0 on success, -1 on failure
 */
int scheduler_integration_init(void);

/**
 * Start scheduler control loop
 * Runs in background thread, continuously:
 * - Discovers nodes
 * - Finds pending pods
 * - Assigns pods to best-fit nodes
 * 
 * @return 0 on success, -1 on failure
 */
int scheduler_integration_start(void);

/**
 * Stop scheduler control loop
 * Gracefully shuts down the scheduler thread
 */
void scheduler_integration_stop(void);

/**
 * Manually schedule a single pod
 * Used for testing and direct scheduling requests
 * 
 * @param namespace - Pod namespace
 * @param pod_name - Pod name
 * @return 0 if scheduled successfully, -1 on error
 */
int scheduler_schedule_pod(const char* namespace, const char* pod_name);

/**
 * Get scheduler statistics
 * Returns metrics about scheduling decisions
 */
typedef struct {
    int total_pods_scheduled;
    int total_scheduling_errors;
    int active_nodes;
    int active_pods;
    int last_sync_time;
} scheduler_stats_t;

scheduler_stats_t scheduler_get_stats(void);

#endif // SIRAH_SCHEDULER_INTEGRATION_H
