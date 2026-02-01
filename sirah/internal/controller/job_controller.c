#include "job_controller.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Global controller instance
static k8s_job_controller_t* g_job_controller = NULL;

// ============ Initialization ============

k8s_job_controller_t* k8s_job_controller_new() {
    k8s_job_controller_t* controller = calloc(1, sizeof(k8s_job_controller_t));
    if (!controller) return NULL;
    
    controller->max_jobs = 10000;
    controller->num_jobs = 0;
    controller->enabled = true;
    
    return controller;
}

void k8s_job_controller_free(k8s_job_controller_t* controller) {
    if (!controller) return;
    
    for (int i = 0; i < controller->num_jobs; i++) {
        if (controller->jobs[i]) {
            k8s_job_free(controller->jobs[i]);
        }
    }
    free(controller->jobs);
    free(controller);
}

void k8s_job_controller_set_enabled(k8s_job_controller_t* controller, bool enabled) {
    if (controller) {
        controller->enabled = enabled;
    }
}

bool k8s_job_controller_is_enabled(k8s_job_controller_t* controller) {
    if (!controller) return false;
    return controller->enabled;
}

// ============ Job Management ============

int k8s_job_controller_create(k8s_job_controller_t* controller, k8s_job_t* job) {
    if (!controller || !job || !job->metadata) return -1;
    
    // Check if job already exists
    k8s_job_t* existing = NULL;
    if (k8s_job_controller_get(controller, job->metadata->name, job->metadata->namespace, &existing) == 0) {
        return -1;  // Duplicate
    }
    
    // Check quota
    if (controller->num_jobs >= controller->max_jobs) {
        return -1;  // Quota exceeded
    }
    
    // Set UID if not set
    if (!job->metadata->uid) {
        char uid[37];
        snprintf(uid, sizeof(uid), "job-%d-%ld", controller->num_jobs, (long)time(NULL));
        job->metadata->uid = malloc(strlen(uid) + 1);
        if (job->metadata->uid) strcpy(job->metadata->uid, uid);
    }
    
    // Add to controller
    k8s_job_t** new_jobs = realloc(controller->jobs, (controller->num_jobs + 1) * sizeof(k8s_job_t*));
    if (!new_jobs) return -1;
    
    new_jobs[controller->num_jobs] = job;
    controller->jobs = new_jobs;
    controller->num_jobs++;
    
    // TODO: Sync to etcd storage
    
    return 0;
}

int k8s_job_controller_get(k8s_job_controller_t* controller, const char* name, const char* namespace, k8s_job_t** job) {
    if (!controller || !name || !namespace || !job) return -1;
    
    for (int i = 0; i < controller->num_jobs; i++) {
        k8s_job_t* j = controller->jobs[i];
        if (j && j->metadata &&
            strcmp(j->metadata->name, name) == 0 &&
            strcmp(j->metadata->namespace, namespace) == 0) {
            *job = j;
            return 0;
        }
    }
    
    return -1;  // Not found
}

int k8s_job_controller_update(k8s_job_controller_t* controller, k8s_job_t* job) {
    if (!controller || !job || !job->metadata) return -1;
    
    // Find and update job
    for (int i = 0; i < controller->num_jobs; i++) {
        k8s_job_t* existing = controller->jobs[i];
        if (existing && existing->metadata &&
            strcmp(existing->metadata->name, job->metadata->name) == 0 &&
            strcmp(existing->metadata->namespace, job->metadata->namespace) == 0) {
            
            k8s_job_free(existing);
            controller->jobs[i] = job;
            
            // TODO: Sync to etcd storage
            
            return 0;
        }
    }
    
    return -1;  // Not found
}

int k8s_job_controller_delete(k8s_job_controller_t* controller, const char* name, const char* namespace) {
    if (!controller || !name || !namespace) return -1;
    
    for (int i = 0; i < controller->num_jobs; i++) {
        k8s_job_t* job = controller->jobs[i];
        if (job && job->metadata &&
            strcmp(job->metadata->name, name) == 0 &&
            strcmp(job->metadata->namespace, namespace) == 0) {
            
            k8s_job_free(job);
            
            // Remove from array
            for (int j = i; j < controller->num_jobs - 1; j++) {
                controller->jobs[j] = controller->jobs[j + 1];
            }
            controller->num_jobs--;
            
            // TODO: Sync to etcd storage
            
            return 0;
        }
    }
    
    return -1;  // Not found
}

int k8s_job_controller_list(k8s_job_controller_t* controller, const char* namespace, k8s_job_t*** jobs, int* count) {
    if (!controller || !jobs || !count) return -1;
    
    int matched = 0;
    k8s_job_t** result = NULL;
    
    for (int i = 0; i < controller->num_jobs; i++) {
        k8s_job_t* job = controller->jobs[i];
        if (job && job->metadata) {
            if (namespace && strcmp(job->metadata->namespace, namespace) != 0) {
                continue;
            }
            
            k8s_job_t** new_result = realloc(result, (matched + 1) * sizeof(k8s_job_t*));
            if (!new_result) {
                free(result);
                return -1;
            }
            
            new_result[matched] = job;
            result = new_result;
            matched++;
        }
    }
    
    *jobs = result;
    *count = matched;
    return 0;
}

// ============ Job Execution ============

int k8s_job_controller_start_job(k8s_job_controller_t* controller, k8s_job_t* job) {
    if (!controller || !job) return -1;
    
    // Mark job as started
    k8s_job_mark_started(job);
    
    // TODO: Create pods according to parallelism setting
    // For now, just mark as started
    
    return 0;
}

int k8s_job_controller_update_job_status(k8s_job_controller_t* controller, const char* job_name, const char* namespace) {
    if (!controller || !job_name || !namespace) return -1;
    
    k8s_job_t* job = NULL;
    if (k8s_job_controller_get(controller, job_name, namespace, &job) != 0) {
        return -1;
    }
    
    // TODO: Query pod status and update job status
    // Update active, succeeded, and failed counts
    
    return 0;
}

bool k8s_job_controller_is_complete(k8s_job_t* job) {
    if (!job || !job->spec || !job->status) return false;
    
    // Job is complete if succeeded >= completions
    return job->status->succeeded >= job->spec->completions;
}

bool k8s_job_controller_should_retry(k8s_job_t* job) {
    if (!job || !job->spec || !job->status) return false;
    
    // Retry if failed < backoff_limit and not completed
    return job->status->failed < job->spec->backoff_limit && !k8s_job_controller_is_complete(job);
}

// ============ Cleanup ============

int k8s_job_controller_cleanup_completed(k8s_job_controller_t* controller) {
    if (!controller) return -1;
    
    time_t now = time(NULL);
    int removed = 0;
    
    for (int i = 0; i < controller->num_jobs; i++) {
        k8s_job_t* job = controller->jobs[i];
        if (!job) continue;
        
        // Check if job is completed and past TTL
        if (k8s_job_controller_is_complete(job) && job->status && job->status->completion_time > 0) {
            if (job->spec && (now - job->status->completion_time) > job->spec->ttl_seconds_after_finished) {
                k8s_job_free(job);
                
                for (int j = i; j < controller->num_jobs - 1; j++) {
                    controller->jobs[j] = controller->jobs[j + 1];
                }
                controller->num_jobs--;
                i--;
                removed++;
            }
        }
    }
    
    return removed;
}

// ============ Global Controller ============

k8s_job_controller_t* k8s_job_controller_global() {
    if (!g_job_controller) {
        g_job_controller = k8s_job_controller_new();
    }
    return g_job_controller;
}
