#include "cron_scheduler.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Global scheduler instance
static k8s_cron_scheduler_t* g_cron_scheduler = NULL;

// ============ Initialization ============

k8s_cron_scheduler_t* k8s_cron_scheduler_new() {
    k8s_cron_scheduler_t* scheduler = calloc(1, sizeof(k8s_cron_scheduler_t));
    if (!scheduler) return NULL;
    
    scheduler->max_cronjobs = 5000;
    scheduler->num_cronjobs = 0;
    scheduler->enabled = true;
    
    return scheduler;
}

void k8s_cron_scheduler_free(k8s_cron_scheduler_t* scheduler) {
    if (!scheduler) return;
    
    for (int i = 0; i < scheduler->num_cronjobs; i++) {
        if (scheduler->cronjobs[i]) {
            k8s_cronjob_free(scheduler->cronjobs[i]);
        }
    }
    free(scheduler->cronjobs);
    free(scheduler);
}

void k8s_cron_scheduler_set_enabled(k8s_cron_scheduler_t* scheduler, bool enabled) {
    if (scheduler) {
        scheduler->enabled = enabled;
    }
}

bool k8s_cron_scheduler_is_enabled(k8s_cron_scheduler_t* scheduler) {
    if (!scheduler) return false;
    return scheduler->enabled;
}

// ============ CronJob Management ============

int k8s_cron_scheduler_register(k8s_cron_scheduler_t* scheduler, k8s_cronjob_t* cronjob) {
    if (!scheduler || !cronjob || !cronjob->metadata) return -1;
    
    // Check if already registered
    k8s_cronjob_t* existing = NULL;
    if (k8s_cron_scheduler_get(scheduler, cronjob->metadata->name, cronjob->metadata->namespace, &existing) == 0) {
        return -1;  // Duplicate
    }
    
    // Check quota
    if (scheduler->num_cronjobs >= scheduler->max_cronjobs) {
        return -1;  // Quota exceeded
    }
    
    // Set UID if not set
    if (!cronjob->metadata->uid) {
        char uid[37];
        snprintf(uid, sizeof(uid), "cron-%d-%ld", scheduler->num_cronjobs, (long)time(NULL));
        cronjob->metadata->uid = malloc(strlen(uid) + 1);
        if (cronjob->metadata->uid) strcpy(cronjob->metadata->uid, uid);
    }
    
    // Add to scheduler
    k8s_cronjob_t** new_cronjobs = realloc(scheduler->cronjobs, (scheduler->num_cronjobs + 1) * sizeof(k8s_cronjob_t*));
    if (!new_cronjobs) return -1;
    
    new_cronjobs[scheduler->num_cronjobs] = cronjob;
    scheduler->cronjobs = new_cronjobs;
    scheduler->num_cronjobs++;
    
    return 0;
}

int k8s_cron_scheduler_unregister(k8s_cron_scheduler_t* scheduler, const char* name, const char* namespace) {
    if (!scheduler || !name || !namespace) return -1;
    
    for (int i = 0; i < scheduler->num_cronjobs; i++) {
        k8s_cronjob_t* cronjob = scheduler->cronjobs[i];
        if (cronjob && cronjob->metadata &&
            strcmp(cronjob->metadata->name, name) == 0 &&
            strcmp(cronjob->metadata->namespace, namespace) == 0) {
            
            k8s_cronjob_free(cronjob);
            
            for (int j = i; j < scheduler->num_cronjobs - 1; j++) {
                scheduler->cronjobs[j] = scheduler->cronjobs[j + 1];
            }
            scheduler->num_cronjobs--;
            return 0;
        }
    }
    
    return -1;  // Not found
}

int k8s_cron_scheduler_get(k8s_cron_scheduler_t* scheduler, const char* name, const char* namespace, k8s_cronjob_t** cronjob) {
    if (!scheduler || !name || !namespace || !cronjob) return -1;
    
    for (int i = 0; i < scheduler->num_cronjobs; i++) {
        k8s_cronjob_t* cj = scheduler->cronjobs[i];
        if (cj && cj->metadata &&
            strcmp(cj->metadata->name, name) == 0 &&
            strcmp(cj->metadata->namespace, namespace) == 0) {
            *cronjob = cj;
            return 0;
        }
    }
    
    return -1;  // Not found
}

int k8s_cron_scheduler_update(k8s_cron_scheduler_t* scheduler, k8s_cronjob_t* cronjob) {
    if (!scheduler || !cronjob || !cronjob->metadata) return -1;
    
    for (int i = 0; i < scheduler->num_cronjobs; i++) {
        k8s_cronjob_t* existing = scheduler->cronjobs[i];
        if (existing && existing->metadata &&
            strcmp(existing->metadata->name, cronjob->metadata->name) == 0 &&
            strcmp(existing->metadata->namespace, cronjob->metadata->namespace) == 0) {
            
            k8s_cronjob_free(existing);
            scheduler->cronjobs[i] = cronjob;
            return 0;
        }
    }
    
    return -1;  // Not found
}

int k8s_cron_scheduler_list(k8s_cron_scheduler_t* scheduler, const char* namespace, k8s_cronjob_t*** cronjobs, int* count) {
    if (!scheduler || !cronjobs || !count) return -1;
    
    int matched = 0;
    k8s_cronjob_t** result = NULL;
    
    for (int i = 0; i < scheduler->num_cronjobs; i++) {
        k8s_cronjob_t* cronjob = scheduler->cronjobs[i];
        if (cronjob && cronjob->metadata) {
            if (namespace && strcmp(cronjob->metadata->namespace, namespace) != 0) {
                continue;
            }
            
            k8s_cronjob_t** new_result = realloc(result, (matched + 1) * sizeof(k8s_cronjob_t*));
            if (!new_result) {
                free(result);
                return -1;
            }
            
            new_result[matched] = cronjob;
            result = new_result;
            matched++;
        }
    }
    
    *cronjobs = result;
    *count = matched;
    return 0;
}

// ============ Schedule Evaluation ============

bool k8s_cron_scheduler_should_trigger(k8s_cronjob_t* cronjob, time_t now) {
    if (!cronjob || !cronjob->spec || !cronjob->spec->schedule) return false;
    
    // Don't trigger if suspended
    if (cronjob->spec->suspend) return false;
    
    // TODO: Compare current time with cron schedule
    // For now, simple hourly check
    if (cronjob->status && cronjob->status->last_schedule_time_str) {
        // Parse last schedule time and check if enough time has passed
        // This is a stub implementation
        return false;
    }
    
    return false;
}

time_t k8s_cron_scheduler_next_execution(k8s_cronjob_t* cronjob, time_t from_time) {
    if (!cronjob) return 0;
    
    return k8s_cronjob_next_execution_time(cronjob->spec->schedule, from_time);
}

bool k8s_cron_scheduler_check_concurrency(k8s_cronjob_t* cronjob) {
    if (!cronjob || !cronjob->spec || !cronjob->status) return false;
    
    switch (cronjob->spec->concurrency_policy) {
        case K8S_CRON_FORBID:
            // Forbid if any active jobs
            return cronjob->status->num_active_jobs == 0;
        
        case K8S_CRON_ALLOW:
            // Always allow
            return true;
        
        case K8S_CRON_REPLACE:
            // Always allow, will replace
            return true;
        
        default:
            return false;
    }
}

int k8s_cron_scheduler_kill_active_jobs(k8s_cron_scheduler_t* scheduler, k8s_cronjob_t* cronjob) {
    if (!scheduler || !cronjob || !cronjob->status) return -1;
    
    // TODO: Delete all active jobs for this cronjob
    cronjob->status->num_active_jobs = 0;
    
    return 0;
}

int k8s_cron_scheduler_evaluate_schedules(k8s_cron_scheduler_t* scheduler,
                                           k8s_job_t*** jobs_to_create,
                                           int* num_jobs) {
    if (!scheduler || !jobs_to_create || !num_jobs) return -1;
    
    time_t now = time(NULL);
    int job_count = 0;
    k8s_job_t** jobs = NULL;
    
    // Evaluate each cronjob
    for (int i = 0; i < scheduler->num_cronjobs; i++) {
        k8s_cronjob_t* cronjob = scheduler->cronjobs[i];
        if (!cronjob) continue;
        
        // Check if should trigger
        if (!k8s_cron_scheduler_should_trigger(cronjob, now)) {
            continue;
        }
        
        // Check concurrency policy
        if (!k8s_cron_scheduler_check_concurrency(cronjob)) {
            continue;  // Skip this cronjob
        }
        
        // Replace policy: kill active jobs
        if (cronjob->spec && cronjob->spec->concurrency_policy == K8S_CRON_REPLACE) {
            k8s_cron_scheduler_kill_active_jobs(scheduler, cronjob);
        }
        
        // TODO: Create job from cronjob template
        // For now, just mark that we would create a job
    }
    
    *jobs_to_create = jobs;
    *num_jobs = job_count;
    
    return 0;
}

// ============ Cleanup ============

int k8s_cron_scheduler_cleanup_history(k8s_cron_scheduler_t* scheduler, k8s_cronjob_t* cronjob) {
    if (!scheduler || !cronjob || !cronjob->spec) return -1;
    
    // TODO: Clean up old job history based on success/failure limits
    
    return 0;
}

// ============ Global Scheduler ============

k8s_cron_scheduler_t* k8s_cron_scheduler_global() {
    if (!g_cron_scheduler) {
        g_cron_scheduler = k8s_cron_scheduler_new();
    }
    return g_cron_scheduler;
}
