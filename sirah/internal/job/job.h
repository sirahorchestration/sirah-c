/*
 * Job API Header - Kubernetes v1.28
 *
 * Manages Job resources for one-off and batch workloads with
 * parallelism control, completion tracking, failure handling,
 * and backoff policies.
 *
 * Key Features:
 * - Pod creation for parallel/sequential execution
 * - Completion tracking with completion count
 * - Automatic retries with exponential backoff
 * - Backoff limits (max failures before job failure)
 * - TTL after finish (auto-cleanup after completion)
 * - Suspend/resume functionality
 * - Status tracking (active, succeeded, failed pods)
 *
 * Thread Safety: All operations protected by mutex lock
 * Max Resources: 1000 Jobs per cluster
 */

#ifndef SIRAH_JOB_H
#define SIRAH_JOB_H

#include <pthread.h>
#include <time.h>
#include <stdint.h>

/* Job Status Condition Types */
typedef enum {
    JOB_CONDITION_SUSPENDED,
    JOB_CONDITION_COMPLETE,
    JOB_CONDITION_FAILED,
    JOB_CONDITION_FAILED_INDEX,
    JOB_CONDITION_TERMINATING
} job_condition_type_t;

/* Job Completion Mode */
typedef enum {
    JOB_COMPLETION_MODE_NON_INDEXED,    /* Standard job (any pod completion) */
    JOB_COMPLETION_MODE_INDEXED         /* Indexed job (pod index 0 to N-1) */
} job_completion_mode_t;

/* Pod Failure Policy Action */
typedef enum {
    POD_FAILURE_POLICY_IGNORE,          /* Ignore pod failure, continue */
    POD_FAILURE_POLICY_FAIL_JOB,        /* Mark entire job as failed */
    POD_FAILURE_POLICY_COUNT_FAILURE,   /* Count toward backoff limit */
    POD_FAILURE_POLICY_UNCONTROLLABLE_ERROR  /* Unrecoverable error */
} pod_failure_policy_action_t;

/* Pod Failure Policy Reason */
typedef enum {
    POD_FAILURE_REASON_EXIT_CODE,
    POD_FAILURE_REASON_TIMEOUT,
    POD_FAILURE_REASON_UNKNOWN
} pod_failure_policy_reason_t;

/* Job Specification */
typedef struct {
    char name[256];                     /* Job name */
    char namespace[128];                /* Namespace */
    int32_t completions;                /* Desired completion count (default 1) */
    int32_t parallelism;                /* Max parallel pods (default 1) */
    int32_t backoff_limit;              /* Max retries (default 6) */
    int32_t ttl_seconds_after_finished; /* Auto-delete after completion (0=keep) */
    int suspend;                        /* 1=suspend job, 0=active */
    job_completion_mode_t completion_mode;  /* Non-indexed or indexed */
    
    char pod_template_name[256];        /* Pod template for job pods */
    char container_image[512];          /* Container image */
    char command[1024];                 /* Container command */
    int32_t container_port;             /* Container port (if any) */
    
    /* Restart policy for pod spec */
    int restart_policy;                 /* 0=Never, 1=OnFailure, 2=Always */
    
    /* Timestamps */
    time_t created_at;
    time_t updated_at;
    uint32_t generation;                /* Kubernetes generation counter */
} job_spec_t;

/* Job Condition */
typedef struct {
    job_condition_type_t type;
    int status;                         /* 1=True, 0=False */
    time_t last_probe_time;
    time_t last_transition_time;
    char reason[128];
    char message[256];
} job_condition_t;

/* Job Status */
typedef struct {
    int32_t active;                     /* Current running pods */
    int32_t succeeded;                  /* Completed pods */
    int32_t failed;                     /* Failed pods */
    int32_t terminating;                /* Pods being terminated */
    
    time_t start_time;                  /* When job started */
    time_t completion_time;             /* When job completed */
    
    int succeeded_ready;                /* Whether succeeded count is ready */
    int failed_ready;                   /* Whether failed count is ready */
    
    /* Conditions */
    job_condition_t conditions[10];
    int condition_count;
    
    /* Pod tracking */
    char pod_names[1000][256];          /* Names of pods created */
    int pod_count;
    
    /* Backoff tracking */
    int32_t failure_count;              /* Current consecutive failures */
    time_t last_failure_time;
} job_status_t;

/* Job Object */
typedef struct {
    job_spec_t spec;
    job_status_t status;
} job_t;

/* Job Manager */
typedef struct {
    pthread_mutex_t lock;
    int initialized;
    job_t jobs[1000];                   /* Max 1000 Jobs */
    int count;
} job_manager_t;

/* ============================================================================
 * Lifecycle Management
 * ========================================================================== */

/**
 * Initialize the Job manager
 * @return 0 on success, -1 on error
 */
int job_manager_init(void);

/**
 * Shutdown the Job manager
 * @return 0 on success, -1 on error
 */
int job_manager_shutdown(void);

/* ============================================================================
 * CRUD Operations (Namespace-Scoped)
 * ========================================================================== */

/**
 * Create a new Job
 * @param namespace Job namespace
 * @param name Job name
 * @param spec Job specification
 * @return 0 on success, -1 on duplicate/error, -2 if invalid spec
 */
int job_create(const char *namespace, const char *name, const job_spec_t *spec);

/**
 * Get a Job by name
 * @param namespace Job namespace
 * @param name Job name
 * @param out_job Pointer to store Job data
 * @return 0 on success, -1 if not found
 */
int job_get(const char *namespace, const char *name, job_t *out_job);

/**
 * Update a Job
 * @param namespace Job namespace
 * @param name Job name
 * @param spec New specification
 * @return 0 on success, -1 if not found, -2 if invalid
 */
int job_update(const char *namespace, const char *name, const job_spec_t *spec);

/**
 * Delete a Job
 * @param namespace Job namespace
 * @param name Job name
 * @param propagation_policy DeletePropagation (0=Foreground, 1=Background, 2=Orphan)
 * @return 0 on success, -1 if not found
 */
int job_delete(const char *namespace, const char *name, int propagation_policy);

/**
 * List all Jobs in namespace
 * @param namespace Namespace filter (NULL for all)
 * @param out_jobs Output array (caller allocated, size >= 1000)
 * @return Number of Jobs listed, -1 on error
 */
int job_list(const char *namespace, job_t *out_jobs);

/**
 * List count of Jobs
 * @param namespace Namespace filter (NULL for all)
 * @return Count of Jobs
 */
int job_list_count(const char *namespace);

/* ============================================================================
 * Job Pod Creation & Management
 * ========================================================================== */

/**
 * Generate pod name for job
 * @param job_name Job name
 * @param pod_index Pod index (for parallelism tracking)
 * @param out_pod_name Output pod name (caller allocated, >= 256)
 * @return 0 on success, -1 on error
 */
int job_generate_pod_name(const char *job_name, int32_t pod_index, char *out_pod_name);

/**
 * Create pod for job execution
 * @param namespace Job namespace
 * @param job_name Job name
 * @param pod_index Index for this pod (0 to parallelism-1)
 * @param out_pod_name Output pod name (caller allocated, >= 256)
 * @return 0 on success, -1 if job not found or error
 */
int job_create_pod(const char *namespace, const char *job_name, int32_t pod_index,
                   char *out_pod_name);

/**
 * Delete pod for job
 * @param namespace Job namespace
 * @param job_name Job name
 * @param pod_name Pod name to delete
 * @return 0 on success, -1 if not found
 */
int job_delete_pod(const char *namespace, const char *job_name, const char *pod_name);

/**
 * Get pods created for job
 * @param namespace Job namespace
 * @param job_name Job name
 * @param out_pod_names Output array of pod names (caller allocated)
 * @param max_pods Maximum pods to return
 * @return Number of pods returned, -1 on error
 */
int job_get_pods(const char *namespace, const char *job_name, char **out_pod_names, int max_pods);

/* ============================================================================
 * Job Status & Completion Tracking
 * ========================================================================== */

/**
 * Update pod completion (pod succeeded)
 * @param namespace Job namespace
 * @param job_name Job name
 * @param pod_name Pod name that succeeded
 * @return 0 on success, -1 if not found
 */
int job_mark_pod_succeeded(const char *namespace, const char *job_name, const char *pod_name);

/**
 * Update pod failure (pod failed)
 * @param namespace Job namespace
 * @param job_name Job name
 * @param pod_name Pod name that failed
 * @param retry 1 if should retry, 0 if permanent failure
 * @return 0 on success, -1 if not found
 */
int job_mark_pod_failed(const char *namespace, const char *job_name, const char *pod_name, int retry);

/**
 * Check if job has completed successfully
 * @param namespace Job namespace
 * @param job_name Job name
 * @return 1 if complete, 0 if not
 */
int job_is_complete(const char *namespace, const char *job_name);

/**
 * Check if job has failed (exceeded backoff limit)
 * @param namespace Job namespace
 * @param job_name Job name
 * @return 1 if failed, 0 if not
 */
int job_is_failed(const char *namespace, const char *job_name);

/**
 * Get job status
 * @param namespace Job namespace
 * @param job_name Job name
 * @param out_status Pointer to store status
 * @return 0 on success, -1 if not found
 */
int job_get_status(const char *namespace, const char *job_name, job_status_t *out_status);

/**
 * Update job status counts
 * @param namespace Job namespace
 * @param job_name Job name
 * @param active Current active pods
 * @param succeeded Succeeded pods
 * @param failed Failed pods
 * @return 0 on success, -1 if not found
 */
int job_update_status(const char *namespace, const char *job_name,
                      int32_t active, int32_t succeeded, int32_t failed);

/**
 * Get job status as JSON
 * @param namespace Job namespace
 * @param job_name Job name
 * @param out_json Output JSON string (caller allocated, >= 2048)
 * @return 0 on success, -1 if not found
 */
int job_get_status_json(const char *namespace, const char *job_name, char *out_json);

/* ============================================================================
 * Backoff & Retry Management
 * ========================================================================== */

/**
 * Record pod failure for backoff calculation
 * @param namespace Job namespace
 * @param job_name Job name
 * @return Current failure count
 */
int job_record_pod_failure(const char *namespace, const char *job_name);

/**
 * Reset failure count on successful pod
 * @param namespace Job namespace
 * @param job_name Job name
 * @return 0 on success, -1 if not found
 */
int job_reset_failure_count(const char *namespace, const char *job_name);

/**
 * Get backoff delay in seconds
 * @param failure_count Current consecutive failure count
 * @return Backoff delay in seconds (1, 2, 4, 8, 16, 32 seconds max)
 */
int job_get_backoff_delay(int32_t failure_count);

/**
 * Check if backoff limit exceeded
 * @param namespace Job namespace
 * @param job_name Job name
 * @return 1 if limit exceeded, 0 if not
 */
int job_has_exceeded_backoff_limit(const char *namespace, const char *job_name);

/* ============================================================================
 * Job Suspend & Resume
 * ========================================================================== */

/**
 * Suspend a job (stop creating new pods)
 * @param namespace Job namespace
 * @param job_name Job name
 * @return 0 on success, -1 if not found
 */
int job_suspend(const char *namespace, const char *job_name);

/**
 * Resume a job (resume pod creation)
 * @param namespace Job namespace
 * @param job_name Job name
 * @return 0 on success, -1 if not found
 */
int job_resume(const char *namespace, const char *job_name);

/**
 * Check if job is suspended
 * @param namespace Job namespace
 * @param job_name Job name
 * @return 1 if suspended, 0 if active
 */
int job_is_suspended(const char *namespace, const char *job_name);

/* ============================================================================
 * Parallelism & Completion Management
 * ========================================================================== */

/**
 * Get number of pods that should be running
 * Considers parallelism limit and remaining completions
 * @param namespace Job namespace
 * @param job_name Job name
 * @return Desired pod count
 */
int32_t job_get_desired_pod_count(const char *namespace, const char *job_name);

/**
 * Get number of pods still needed for completion
 * @param namespace Job namespace
 * @param job_name Job name
 * @return Remaining pods to complete
 */
int32_t job_get_remaining_completions(const char *namespace, const char *job_name);

/**
 * Get current pod count for job
 * @param namespace Job namespace
 * @param job_name Job name
 * @return Current pod count
 */
int32_t job_get_current_pod_count(const char *namespace, const char *job_name);

/**
 * Check if job needs more pods created
 * @param namespace Job namespace
 * @param job_name Job name
 * @return 1 if should create more pods, 0 if not
 */
int job_should_create_more_pods(const char *namespace, const char *job_name);

/**
 * Check if all pods should be terminated
 * @param namespace Job namespace
 * @param job_name Job name
 * @return 1 if should terminate all pods, 0 if not
 */
int job_should_terminate_all_pods(const char *namespace, const char *job_name);

/* ============================================================================
 * TTL (Time-To-Live) Management
 * ========================================================================== */

/**
 * Check if job should be auto-deleted (TTL expired)
 * @param namespace Job namespace
 * @param job_name Job name
 * @return 1 if TTL expired and should delete, 0 if not
 */
int job_should_be_deleted_by_ttl(const char *namespace, const char *job_name);

/**
 * Get time until TTL cleanup (in seconds)
 * @param namespace Job namespace
 * @param job_name Job name
 * @return Seconds until cleanup (-1 if no TTL set)
 */
int32_t job_get_ttl_seconds_remaining(const char *namespace, const char *job_name);

/* ============================================================================
 * Validation & Admission Control
 * ========================================================================== */

/**
 * Validate Job specification
 * @param spec Job specification to validate
 * @param out_reason Output reason if invalid (caller allocated, >= 256)
 * @return 1 if valid, 0 if invalid
 */
int job_validate_spec(const job_spec_t *spec, char *out_reason);

/**
 * Check if Job can be created
 * @param namespace Job namespace
 * @param spec Job specification
 * @param out_reason Output reason if cannot create (caller allocated, >= 256)
 * @return 1 if can create, 0 if cannot
 */
int job_can_create(const char *namespace, const job_spec_t *spec, char *out_reason);

#endif /* SIRAH_JOB_H */
