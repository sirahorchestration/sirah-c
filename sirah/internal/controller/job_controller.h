#ifndef K8S_JOB_CONTROLLER_H
#define K8S_JOB_CONTROLLER_H

#include <types/job.h>
#include <stdbool.h>

// Job controller
typedef struct {
    k8s_job_t** jobs;
    int num_jobs;
    int max_jobs;
    bool enabled;
} k8s_job_controller_t;

// ============ Controller Lifecycle ============

k8s_job_controller_t* k8s_job_controller_new();
void k8s_job_controller_free(k8s_job_controller_t* controller);

// ============ Job Management ============

int k8s_job_controller_create(k8s_job_controller_t* controller, k8s_job_t* job);
int k8s_job_controller_get(k8s_job_controller_t* controller, const char* name, const char* namespace, k8s_job_t** job);
int k8s_job_controller_update(k8s_job_controller_t* controller, k8s_job_t* job);
int k8s_job_controller_delete(k8s_job_controller_t* controller, const char* name, const char* namespace);
int k8s_job_controller_list(k8s_job_controller_t* controller, const char* namespace, k8s_job_t*** jobs, int* count);

// ============ Job Execution ============

// Mark job as started and create pods
int k8s_job_controller_start_job(k8s_job_controller_t* controller, k8s_job_t* job);

// Update job status based on pod completion
int k8s_job_controller_update_job_status(k8s_job_controller_t* controller, const char* job_name, const char* namespace);

// Check if job is complete
bool k8s_job_controller_is_complete(k8s_job_t* job);

// Check if job should be retried
bool k8s_job_controller_should_retry(k8s_job_t* job);

// ============ Cleanup ============

// Delete completed jobs after TTL
int k8s_job_controller_cleanup_completed(k8s_job_controller_t* controller);

// ============ Control ============

void k8s_job_controller_set_enabled(k8s_job_controller_t* controller, bool enabled);
bool k8s_job_controller_is_enabled(k8s_job_controller_t* controller);

// ============ Global Controller ============

k8s_job_controller_t* k8s_job_controller_global();

#endif
