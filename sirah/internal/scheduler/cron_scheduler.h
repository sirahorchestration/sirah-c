#ifndef K8S_CRON_SCHEDULER_H
#define K8S_CRON_SCHEDULER_H

#include <types/job.h>
#include <stdbool.h>
#include <time.h>

// CronJob scheduler
typedef struct {
    k8s_cronjob_t** cronjobs;
    int num_cronjobs;
    int max_cronjobs;
    bool enabled;
} k8s_cron_scheduler_t;

// ============ Scheduler Lifecycle ============

k8s_cron_scheduler_t* k8s_cron_scheduler_new();
void k8s_cron_scheduler_free(k8s_cron_scheduler_t* scheduler);

// ============ CronJob Management ============

int k8s_cron_scheduler_register(k8s_cron_scheduler_t* scheduler, k8s_cronjob_t* cronjob);
int k8s_cron_scheduler_unregister(k8s_cron_scheduler_t* scheduler, const char* name, const char* namespace);
int k8s_cron_scheduler_get(k8s_cron_scheduler_t* scheduler, const char* name, const char* namespace, k8s_cronjob_t** cronjob);
int k8s_cron_scheduler_update(k8s_cron_scheduler_t* scheduler, k8s_cronjob_t* cronjob);
int k8s_cron_scheduler_list(k8s_cron_scheduler_t* scheduler, const char* namespace, k8s_cronjob_t*** cronjobs, int* count);

// ============ Schedule Evaluation ============

// Run scheduling logic, return jobs to create
int k8s_cron_scheduler_evaluate_schedules(k8s_cron_scheduler_t* scheduler,
                                           k8s_job_t*** jobs_to_create,
                                           int* num_jobs);

// Check if a specific cronjob should trigger
bool k8s_cron_scheduler_should_trigger(k8s_cronjob_t* cronjob, time_t now);

// Get next execution time for a cronjob
time_t k8s_cron_scheduler_next_execution(k8s_cronjob_t* cronjob, time_t from_time);

// ============ Concurrency Handling ============

// Check concurrency policy and decide if new job can run
bool k8s_cron_scheduler_check_concurrency(k8s_cronjob_t* cronjob);

// Kill active jobs (for Replace policy)
int k8s_cron_scheduler_kill_active_jobs(k8s_cron_scheduler_t* scheduler, k8s_cronjob_t* cronjob);

// ============ Cleanup ============

// Remove completed jobs from history
int k8s_cron_scheduler_cleanup_history(k8s_cron_scheduler_t* scheduler, k8s_cronjob_t* cronjob);

// ============ Control ============

void k8s_cron_scheduler_set_enabled(k8s_cron_scheduler_t* scheduler, bool enabled);
bool k8s_cron_scheduler_is_enabled(k8s_cron_scheduler_t* scheduler);

// ============ Global Scheduler ============

k8s_cron_scheduler_t* k8s_cron_scheduler_global();

#endif
