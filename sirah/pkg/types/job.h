#ifndef K8S_JOB_H
#define K8S_JOB_H

#include <types/common.h>
#include <stdbool.h>
#include <time.h>

// Job restart policy
typedef enum {
    K8S_JOB_RESTART_NEVER = 0,
    K8S_JOB_RESTART_ON_FAILURE = 1,
    K8S_JOB_RESTART_ALWAYS = 2
} k8s_job_restart_policy_t;

// CronJob concurrency policy
typedef enum {
    K8S_CRON_ALLOW = 0,
    K8S_CRON_FORBID = 1,
    K8S_CRON_REPLACE = 2
} k8s_cron_concurrency_policy_t;

// Job specification
typedef struct {
    int parallelism;                // Number of parallel pods (default 1)
    int completions;                // Number of required completions
    int backoff_limit;              // Retry limit (default 6)
    int ttl_seconds_after_finished; // TTL after completion
    k8s_job_restart_policy_t restart_policy;
    char* pod_template_json;        // Pod template specification
    time_t active_deadline_seconds; // Timeout for job
} k8s_job_spec_t;

// Job condition
typedef struct {
    char* type;        // "Active", "Complete", "Failed"
    char* status;      // "True", "False", "Unknown"
    time_t last_update;
    char* reason;
} k8s_job_condition_t;

// Job status
typedef struct {
    int active;        // Number of running pods
    int succeeded;     // Number of completed pods
    int failed;        // Number of failed pods
    time_t start_time;
    time_t completion_time;
    k8s_job_condition_t** conditions;
    int num_conditions;
} k8s_job_status_t;

// Job object
typedef struct {
    k8s_metadata_t* metadata;
    k8s_job_spec_t* spec;
    k8s_job_status_t* status;
} k8s_job_t;

// CronJob specification
typedef struct {
    char* schedule;                          // Cron expression
    char* timezone;                          // Optional timezone
    bool suspend;                            // Suspend scheduling
    k8s_cron_concurrency_policy_t concurrency_policy;
    int success_history_limit;               // Keep N successful jobs
    int failure_history_limit;               // Keep N failed jobs
    char* job_template_json;                 // Job template
} k8s_cronjob_spec_t;

// CronJob status
typedef struct {
    char* last_schedule_time_str;
    char* last_successful_time_str;
    char** active_job_names;
    int num_active_jobs;
} k8s_cronjob_status_t;

// CronJob object
typedef struct {
    k8s_metadata_t* metadata;
    k8s_cronjob_spec_t* spec;
    k8s_cronjob_status_t* status;
} k8s_cronjob_t;

// ============ Job Functions ============

k8s_job_t* k8s_job_new(const char* name, const char* namespace);
void k8s_job_free(k8s_job_t* job);

k8s_job_spec_t* k8s_job_spec_new();
void k8s_job_spec_free(k8s_job_spec_t* spec);

k8s_job_status_t* k8s_job_status_new();
void k8s_job_status_free(k8s_job_status_t* status);

int k8s_job_set_parallelism(k8s_job_t* job, int parallelism);
int k8s_job_set_completions(k8s_job_t* job, int completions);
int k8s_job_set_backoff_limit(k8s_job_t* job, int limit);
int k8s_job_set_restart_policy(k8s_job_t* job, k8s_job_restart_policy_t policy);
int k8s_job_set_pod_template(k8s_job_t* job, const char* template_json);

int k8s_job_add_condition(k8s_job_status_t* status, const char* type, const char* status_str);
void k8s_job_mark_started(k8s_job_t* job);
void k8s_job_mark_completed(k8s_job_t* job);
void k8s_job_increment_active(k8s_job_status_t* status, int count);
void k8s_job_increment_succeeded(k8s_job_status_t* status, int count);
void k8s_job_increment_failed(k8s_job_status_t* status, int count);

char* k8s_job_to_json(k8s_job_t* job);
k8s_job_t* k8s_job_from_json(const char* json);

// ============ CronJob Functions ============

k8s_cronjob_t* k8s_cronjob_new(const char* name, const char* namespace);
void k8s_cronjob_free(k8s_cronjob_t* cronjob);

k8s_cronjob_spec_t* k8s_cronjob_spec_new();
void k8s_cronjob_spec_free(k8s_cronjob_spec_t* spec);

k8s_cronjob_status_t* k8s_cronjob_status_new();
void k8s_cronjob_status_free(k8s_cronjob_status_t* status);

int k8s_cronjob_set_schedule(k8s_cronjob_t* cronjob, const char* schedule);
int k8s_cronjob_set_job_template(k8s_cronjob_t* cronjob, const char* template_json);
int k8s_cronjob_set_concurrency_policy(k8s_cronjob_t* cronjob, k8s_cron_concurrency_policy_t policy);
int k8s_cronjob_add_active_job(k8s_cronjob_status_t* status, const char* job_name);
int k8s_cronjob_remove_active_job(k8s_cronjob_status_t* status, const char* job_name);

// CronJob schedule validation and next execution time
bool k8s_cronjob_is_valid_schedule(const char* schedule);
time_t k8s_cronjob_next_execution_time(const char* schedule, time_t from_time);

char* k8s_cronjob_to_json(k8s_cronjob_t* cronjob);
k8s_cronjob_t* k8s_cronjob_from_json(const char* json);

#endif
