/*
 * Job Manager Implementation - Kubernetes v1.28
 *
 * Complete implementation of Job resource management with pod parallelism,
 * completion tracking, exponential backoff retry, and TTL support.
 *
 * Thread-safe singleton manager with mutex protection.
 */

#include "job.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/* Global Job manager */
static job_manager_t job_manager = {0};

/* ============================================================================
 * Helper Functions
 * ========================================================================== */

/**
 * Find Job index by namespace and name
 * @return Index on success, -1 if not found
 */
static int find_job_index(const char *namespace, const char *name) {
    if (!namespace || !name) return -1;
    
    for (int i = 0; i < job_manager.count; i++) {
        if (strcmp(job_manager.jobs[i].spec.namespace, namespace) == 0 &&
            strcmp(job_manager.jobs[i].spec.name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* ============================================================================
 * Lifecycle Management
 * ========================================================================== */

int job_manager_init(void) {
    if (job_manager.initialized) return 0;
    
    pthread_mutex_init(&job_manager.lock, NULL);
    job_manager.count = 0;
    memset(job_manager.jobs, 0, sizeof(job_manager.jobs));
    job_manager.initialized = 1;
    
    return 0;
}

int job_manager_shutdown(void) {
    if (!job_manager.initialized) return 0;
    
    pthread_mutex_lock(&job_manager.lock);
    job_manager.count = 0;
    memset(job_manager.jobs, 0, sizeof(job_manager.jobs));
    job_manager.initialized = 0;
    pthread_mutex_unlock(&job_manager.lock);
    
    pthread_mutex_destroy(&job_manager.lock);
    return 0;
}

/* ============================================================================
 * CRUD Operations
 * ========================================================================== */

int job_create(const char *namespace, const char *name, const job_spec_t *spec) {
    if (!namespace || !name || !spec) return -1;
    if (job_manager.count >= 1000) return -1;
    
    pthread_mutex_lock(&job_manager.lock);
    
    /* Check for duplicate */
    if (find_job_index(namespace, name) >= 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return -1;
    }
    
    /* Validate spec */
    char reason[256] = {0};
    if (!job_validate_spec(spec, reason)) {
        pthread_mutex_unlock(&job_manager.lock);
        return -2;
    }
    
    /* Create new Job */
    int idx = job_manager.count;
    job_t *job = &job_manager.jobs[idx];
    
    memcpy(&job->spec, spec, sizeof(job_spec_t));
    strncpy(job->spec.namespace, namespace, 127);
    strncpy(job->spec.name, name, 255);
    
    job->spec.created_at = time(NULL);
    job->spec.updated_at = time(NULL);
    job->spec.generation = 1;
    
    /* Initialize status */
    job->status.active = 0;
    job->status.succeeded = 0;
    job->status.failed = 0;
    job->status.terminating = 0;
    job->status.start_time = time(NULL);
    job->status.completion_time = 0;
    job->status.pod_count = 0;
    job->status.failure_count = 0;
    
    job_manager.count++;
    
    pthread_mutex_unlock(&job_manager.lock);
    return 0;
}

int job_get(const char *namespace, const char *name, job_t *out_job) {
    if (!namespace || !name || !out_job) return -1;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return -1;
    }
    
    memcpy(out_job, &job_manager.jobs[idx], sizeof(job_t));
    
    pthread_mutex_unlock(&job_manager.lock);
    return 0;
}

int job_update(const char *namespace, const char *name, const job_spec_t *spec) {
    if (!namespace || !name || !spec) return -1;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return -1;
    }
    
    job_t *job = &job_manager.jobs[idx];
    
    /* Validate new spec */
    char reason[256] = {0};
    if (!job_validate_spec(spec, reason)) {
        pthread_mutex_unlock(&job_manager.lock);
        return -2;
    }
    
    /* Update spec, preserving identity fields */
    time_t created_at = job->spec.created_at;
    memcpy(&job->spec, spec, sizeof(job_spec_t));
    strncpy(job->spec.namespace, namespace, 127);
    strncpy(job->spec.name, name, 255);
    job->spec.created_at = created_at;
    job->spec.updated_at = time(NULL);
    job->spec.generation++;
    
    pthread_mutex_unlock(&job_manager.lock);
    return 0;
}

int job_delete(const char *namespace, const char *name, int propagation_policy) {
    if (!namespace || !name) return -1;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return -1;
    }
    
    /* Shift array elements */
    for (int i = idx; i < job_manager.count - 1; i++) {
        memcpy(&job_manager.jobs[i], &job_manager.jobs[i + 1], sizeof(job_t));
    }
    
    memset(&job_manager.jobs[job_manager.count - 1], 0, sizeof(job_t));
    job_manager.count--;
    
    pthread_mutex_unlock(&job_manager.lock);
    return 0;
}

int job_list(const char *namespace, job_t *out_jobs) {
    if (!out_jobs) return -1;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int count = 0;
    for (int i = 0; i < job_manager.count; i++) {
        if (!namespace || strcmp(job_manager.jobs[i].spec.namespace, namespace) == 0) {
            memcpy(&out_jobs[count], &job_manager.jobs[i], sizeof(job_t));
            count++;
        }
    }
    
    pthread_mutex_unlock(&job_manager.lock);
    return count;
}

int job_list_count(const char *namespace) {
    pthread_mutex_lock(&job_manager.lock);
    
    int count = 0;
    for (int i = 0; i < job_manager.count; i++) {
        if (!namespace || strcmp(job_manager.jobs[i].spec.namespace, namespace) == 0) {
            count++;
        }
    }
    
    pthread_mutex_unlock(&job_manager.lock);
    return count;
}

/* ============================================================================
 * Job Pod Creation & Management
 * ========================================================================== */

int job_generate_pod_name(const char *job_name, int32_t pod_index, char *out_pod_name) {
    if (!job_name || !out_pod_name || pod_index < 0) return -1;
    
    snprintf(out_pod_name, 256, "%s-%d-%d", job_name, pod_index, (int)(time(NULL) % 1000));
    return 0;
}

int job_create_pod(const char *namespace, const char *job_name, int32_t pod_index,
                   char *out_pod_name) {
    if (!namespace || !job_name || !out_pod_name || pod_index < 0) return -1;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return -1;
    }
    
    job_t *job = &job_manager.jobs[idx];
    
    /* Generate pod name */
    job_generate_pod_name(job_name, pod_index, out_pod_name);
    
    /* Track pod in job status */
    if (job->status.pod_count < 1000) {
        strncpy(job->status.pod_names[job->status.pod_count], out_pod_name, 255);
        job->status.pod_count++;
    }
    
    job->status.active++;
    
    pthread_mutex_unlock(&job_manager.lock);
    return 0;
}

int job_delete_pod(const char *namespace, const char *job_name, const char *pod_name) {
    if (!namespace || !job_name || !pod_name) return -1;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return -1;
    }
    
    job_t *job = &job_manager.jobs[idx];
    
    /* Remove from pod tracking */
    for (int i = 0; i < job->status.pod_count; i++) {
        if (strcmp(job->status.pod_names[i], pod_name) == 0) {
            /* Shift array */
            for (int j = i; j < job->status.pod_count - 1; j++) {
                strcpy(job->status.pod_names[j], job->status.pod_names[j + 1]);
            }
            memset(job->status.pod_names[job->status.pod_count - 1], 0, 256);
            job->status.pod_count--;
            break;
        }
    }
    
    if (job->status.active > 0) {
        job->status.active--;
    }
    
    pthread_mutex_unlock(&job_manager.lock);
    return 0;
}

int job_get_pods(const char *namespace, const char *job_name, char **out_pod_names, int max_pods) {
    if (!namespace || !job_name || !out_pod_names) return -1;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return -1;
    }
    
    job_t *job = &job_manager.jobs[idx];
    int count = (job->status.pod_count < max_pods) ? job->status.pod_count : max_pods;
    
    for (int i = 0; i < count; i++) {
        out_pod_names[i] = job->status.pod_names[i];
    }
    
    pthread_mutex_unlock(&job_manager.lock);
    return count;
}

/* ============================================================================
 * Job Status & Completion Tracking
 * ========================================================================== */

int job_mark_pod_succeeded(const char *namespace, const char *job_name, const char *pod_name) {
    if (!namespace || !job_name || !pod_name) return -1;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return -1;
    }
    
    job_t *job = &job_manager.jobs[idx];
    job->status.succeeded++;
    
    if (job->status.active > 0) {
        job->status.active--;
    }
    
    /* Reset failure count on success */
    job->status.failure_count = 0;
    
    pthread_mutex_unlock(&job_manager.lock);
    return 0;
}

int job_mark_pod_failed(const char *namespace, const char *job_name, const char *pod_name, int retry) {
    if (!namespace || !job_name || !pod_name) return -1;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return -1;
    }
    
    job_t *job = &job_manager.jobs[idx];
    
    if (retry) {
        /* Retry: just count the failure for backoff */
        job->status.failure_count++;
    } else {
        /* Permanent failure: count as failed pod */
        job->status.failed++;
    }
    
    if (job->status.active > 0) {
        job->status.active--;
    }
    
    job->status.last_failure_time = time(NULL);
    
    pthread_mutex_unlock(&job_manager.lock);
    return 0;
}

int job_is_complete(const char *namespace, const char *job_name) {
    if (!namespace || !job_name) return 0;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return 0;
    }
    
    job_t *job = &job_manager.jobs[idx];
    int complete = (job->status.succeeded >= job->spec.completions);
    
    pthread_mutex_unlock(&job_manager.lock);
    return complete;
}

int job_is_failed(const char *namespace, const char *job_name) {
    if (!namespace || !job_name) return 0;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return 0;
    }
    
    job_t *job = &job_manager.jobs[idx];
    int failed = (job->status.failure_count > job->spec.backoff_limit);
    
    pthread_mutex_unlock(&job_manager.lock);
    return failed;
}

int job_get_status(const char *namespace, const char *job_name, job_status_t *out_status) {
    if (!namespace || !job_name || !out_status) return -1;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return -1;
    }
    
    memcpy(out_status, &job_manager.jobs[idx].status, sizeof(job_status_t));
    
    pthread_mutex_unlock(&job_manager.lock);
    return 0;
}

int job_update_status(const char *namespace, const char *job_name,
                      int32_t active, int32_t succeeded, int32_t failed) {
    if (!namespace || !job_name) return -1;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return -1;
    }
    
    job_t *job = &job_manager.jobs[idx];
    job->status.active = active;
    job->status.succeeded = succeeded;
    job->status.failed = failed;
    
    pthread_mutex_unlock(&job_manager.lock);
    return 0;
}

int job_get_status_json(const char *namespace, const char *job_name, char *out_json) {
    if (!namespace || !job_name || !out_json) return -1;
    
    job_status_t status = {0};
    if (job_get_status(namespace, job_name, &status) < 0) return -1;
    
    snprintf(out_json, 2048,
        "{"
        "  \"active\": %d,"
        "  \"succeeded\": %d,"
        "  \"failed\": %d,"
        "  \"terminating\": %d,"
        "  \"pod_count\": %d,"
        "  \"failure_count\": %d"
        "}",
        status.active, status.succeeded, status.failed, status.terminating,
        status.pod_count, status.failure_count);
    
    return 0;
}

/* ============================================================================
 * Backoff & Retry Management
 * ========================================================================== */

int job_record_pod_failure(const char *namespace, const char *job_name) {
    if (!namespace || !job_name) return -1;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return -1;
    }
    
    job_manager.jobs[idx].status.failure_count++;
    job_manager.jobs[idx].status.last_failure_time = time(NULL);
    
    int count = job_manager.jobs[idx].status.failure_count;
    
    pthread_mutex_unlock(&job_manager.lock);
    return count;
}

int job_reset_failure_count(const char *namespace, const char *job_name) {
    if (!namespace || !job_name) return -1;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return -1;
    }
    
    job_manager.jobs[idx].status.failure_count = 0;
    
    pthread_mutex_unlock(&job_manager.lock);
    return 0;
}

int job_get_backoff_delay(int32_t failure_count) {
    if (failure_count < 0) failure_count = 0;
    if (failure_count > 6) failure_count = 6;
    
    /* Exponential backoff: 1, 2, 4, 8, 16, 32, 32 seconds max */
    int delays[] = {1, 2, 4, 8, 16, 32, 32};
    return delays[failure_count];
}

int job_has_exceeded_backoff_limit(const char *namespace, const char *job_name) {
    if (!namespace || !job_name) return 0;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return 0;
    }
    
    job_t *job = &job_manager.jobs[idx];
    int exceeded = (job->status.failure_count > job->spec.backoff_limit);
    
    pthread_mutex_unlock(&job_manager.lock);
    return exceeded;
}

/* ============================================================================
 * Job Suspend & Resume
 * ========================================================================== */

int job_suspend(const char *namespace, const char *job_name) {
    if (!namespace || !job_name) return -1;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return -1;
    }
    
    job_manager.jobs[idx].spec.suspend = 1;
    
    pthread_mutex_unlock(&job_manager.lock);
    return 0;
}

int job_resume(const char *namespace, const char *job_name) {
    if (!namespace || !job_name) return -1;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return -1;
    }
    
    job_manager.jobs[idx].spec.suspend = 0;
    
    pthread_mutex_unlock(&job_manager.lock);
    return 0;
}

int job_is_suspended(const char *namespace, const char *job_name) {
    if (!namespace || !job_name) return 0;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return 0;
    }
    
    int suspended = job_manager.jobs[idx].spec.suspend;
    
    pthread_mutex_unlock(&job_manager.lock);
    return suspended;
}

/* ============================================================================
 * Parallelism & Completion Management
 * ========================================================================== */

int32_t job_get_desired_pod_count(const char *namespace, const char *job_name) {
    if (!namespace || !job_name) return 0;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return 0;
    }
    
    job_t *job = &job_manager.jobs[idx];
    
    /* Calculate desired count based on completions and parallelism */
    int32_t remaining = job->spec.completions - job->status.succeeded;
    if (remaining < 0) remaining = 0;
    
    int32_t desired = (remaining < job->spec.parallelism) ? remaining : job->spec.parallelism;
    
    pthread_mutex_unlock(&job_manager.lock);
    return desired;
}

int32_t job_get_remaining_completions(const char *namespace, const char *job_name) {
    if (!namespace || !job_name) return 0;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return 0;
    }
    
    job_t *job = &job_manager.jobs[idx];
    int32_t remaining = job->spec.completions - job->status.succeeded;
    
    if (remaining < 0) remaining = 0;
    
    pthread_mutex_unlock(&job_manager.lock);
    return remaining;
}

int32_t job_get_current_pod_count(const char *namespace, const char *job_name) {
    if (!namespace || !job_name) return 0;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return 0;
    }
    
    int32_t count = job_manager.jobs[idx].status.pod_count;
    
    pthread_mutex_unlock(&job_manager.lock);
    return count;
}

int job_should_create_more_pods(const char *namespace, const char *job_name) {
    if (!namespace || !job_name) return 0;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return 0;
    }
    
    job_t *job = &job_manager.jobs[idx];
    
    /* Don't create if suspended or complete or failed */
    if (job->spec.suspend || job->status.succeeded >= job->spec.completions ||
        job->status.failure_count > job->spec.backoff_limit) {
        pthread_mutex_unlock(&job_manager.lock);
        return 0;
    }
    
    /* Create if below parallelism limit and more completions needed */
    int32_t remaining = job->spec.completions - job->status.succeeded;
    int should_create = (job->status.pod_count < job->spec.parallelism && remaining > 0);
    
    pthread_mutex_unlock(&job_manager.lock);
    return should_create;
}

int job_should_terminate_all_pods(const char *namespace, const char *job_name) {
    if (!namespace || !job_name) return 0;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return 0;
    }
    
    job_t *job = &job_manager.jobs[idx];
    
    /* Terminate all if suspended or exceeded backoff limit */
    int should_terminate = (job->spec.suspend || job->status.failure_count > job->spec.backoff_limit);
    
    pthread_mutex_unlock(&job_manager.lock);
    return should_terminate;
}

/* ============================================================================
 * TTL (Time-To-Live) Management
 * ========================================================================== */

int job_should_be_deleted_by_ttl(const char *namespace, const char *job_name) {
    if (!namespace || !job_name) return 0;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return 0;
    }
    
    job_t *job = &job_manager.jobs[idx];
    
    /* No TTL set */
    if (job->spec.ttl_seconds_after_finished <= 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return 0;
    }
    
    /* Job not complete or failed */
    if (job->status.completion_time == 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return 0;
    }
    
    /* Check if TTL expired */
    time_t now = time(NULL);
    int expired = (now >= job->status.completion_time + job->spec.ttl_seconds_after_finished);
    
    pthread_mutex_unlock(&job_manager.lock);
    return expired;
}

int32_t job_get_ttl_seconds_remaining(const char *namespace, const char *job_name) {
    if (!namespace || !job_name) return -1;
    
    pthread_mutex_lock(&job_manager.lock);
    
    int idx = find_job_index(namespace, job_name);
    if (idx < 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return -1;
    }
    
    job_t *job = &job_manager.jobs[idx];
    
    /* No TTL set */
    if (job->spec.ttl_seconds_after_finished <= 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return -1;
    }
    
    /* Job not complete */
    if (job->status.completion_time == 0) {
        pthread_mutex_unlock(&job_manager.lock);
        return job->spec.ttl_seconds_after_finished;
    }
    
    time_t now = time(NULL);
    int32_t remaining = job->spec.ttl_seconds_after_finished - 
                        (int32_t)(now - job->status.completion_time);
    
    if (remaining < 0) remaining = 0;
    
    pthread_mutex_unlock(&job_manager.lock);
    return remaining;
}

/* ============================================================================
 * Validation & Admission Control
 * ========================================================================== */

int job_validate_spec(const job_spec_t *spec, char *out_reason) {
    if (!spec) return 0;
    
    /* Validate name */
    if (strlen(spec->name) == 0 || strlen(spec->name) > 253) {
        if (out_reason) snprintf(out_reason, 256, "Invalid name: must be 1-253 characters");
        return 0;
    }
    
    /* Validate completions */
    if (spec->completions <= 0 || spec->completions > 10000) {
        if (out_reason) snprintf(out_reason, 256, "Invalid completions: must be 1-10000");
        return 0;
    }
    
    /* Validate parallelism */
    if (spec->parallelism <= 0 || spec->parallelism > 1000) {
        if (out_reason) snprintf(out_reason, 256, "Invalid parallelism: must be 1-1000");
        return 0;
    }
    
    /* Validate backoff limit */
    if (spec->backoff_limit < 0 || spec->backoff_limit > 100) {
        if (out_reason) snprintf(out_reason, 256, "Invalid backoff limit: 0-100");
        return 0;
    }
    
    /* Validate restart policy */
    if (spec->restart_policy < 0 || spec->restart_policy > 2) {
        if (out_reason) snprintf(out_reason, 256, "Invalid restart policy");
        return 0;
    }
    
    return 1;
}

int job_can_create(const char *namespace, const job_spec_t *spec, char *out_reason) {
    if (!namespace || !spec) return 0;
    
    /* Check namespace length */
    if (strlen(namespace) == 0 || strlen(namespace) > 253) {
        if (out_reason) snprintf(out_reason, 256, "Invalid namespace");
        return 0;
    }
    
    return job_validate_spec(spec, out_reason);
}
