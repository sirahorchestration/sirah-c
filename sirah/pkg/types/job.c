#include <types/job.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <json-c/json.h>

// ============ Helper Functions ============

static int add_string_to_array(char*** array, int* count, const char* value) {
    if (!array || !count || !value) return -1;
    
    char** new_array = realloc(*array, (*count + 1) * sizeof(char*));
    if (!new_array) return -1;
    
    new_array[*count] = malloc(strlen(value) + 1);
    if (!new_array[*count]) {
        free(new_array);
        return -1;
    }
    
    strcpy(new_array[*count], value);
    *array = new_array;
    (*count)++;
    return 0;
}

// ============ Job Specification ============

k8s_job_spec_t* k8s_job_spec_new() {
    k8s_job_spec_t* spec = calloc(1, sizeof(k8s_job_spec_t));
    if (!spec) return NULL;
    
    spec->parallelism = 1;
    spec->completions = 1;
    spec->backoff_limit = 6;
    spec->ttl_seconds_after_finished = 3600;  // 1 hour default
    spec->restart_policy = K8S_JOB_RESTART_ON_FAILURE;
    
    return spec;
}

void k8s_job_spec_free(k8s_job_spec_t* spec) {
    if (!spec) return;
    free(spec->pod_template_json);
    free(spec);
}

// ============ Job Status ============

k8s_job_status_t* k8s_job_status_new() {
    k8s_job_status_t* status = calloc(1, sizeof(k8s_job_status_t));
    if (!status) return NULL;
    
    status->active = 0;
    status->succeeded = 0;
    status->failed = 0;
    status->start_time = 0;
    status->completion_time = 0;
    status->num_conditions = 0;
    
    return status;
}

void k8s_job_status_free(k8s_job_status_t* status) {
    if (!status) return;
    
    for (int i = 0; i < status->num_conditions; i++) {
        if (status->conditions[i]) {
            free(status->conditions[i]->type);
            free(status->conditions[i]->status);
            free(status->conditions[i]->reason);
            free(status->conditions[i]);
        }
    }
    free(status->conditions);
    free(status);
}

int k8s_job_add_condition(k8s_job_status_t* status, const char* type, const char* status_str) {
    if (!status || !type || !status_str) return -1;
    
    k8s_job_condition_t* cond = calloc(1, sizeof(k8s_job_condition_t));
    if (!cond) return -1;
    
    cond->type = malloc(strlen(type) + 1);
    cond->status = malloc(strlen(status_str) + 1);
    
    if (!cond->type || !cond->status) {
        free(cond->type);
        free(cond->status);
        free(cond);
        return -1;
    }
    
    strcpy(cond->type, type);
    strcpy(cond->status, status_str);
    cond->last_update = time(NULL);
    
    k8s_job_condition_t** new_conditions = realloc(status->conditions,
                                                    (status->num_conditions + 1) * sizeof(k8s_job_condition_t*));
    if (!new_conditions) {
        free(cond->type);
        free(cond->status);
        free(cond);
        return -1;
    }
    
    new_conditions[status->num_conditions] = cond;
    status->conditions = new_conditions;
    status->num_conditions++;
    
    return 0;
}

// ============ Job Lifecycle ============

k8s_job_t* k8s_job_new(const char* name, const char* namespace) {
    if (!name || !namespace) return NULL;
    
    k8s_job_t* job = calloc(1, sizeof(k8s_job_t));
    if (!job) return NULL;
    
    job->metadata = k8s_metadata_new(name, namespace);
    if (!job->metadata) {
        free(job);
        return NULL;
    }
    
    job->spec = k8s_job_spec_new();
    if (!job->spec) {
        k8s_metadata_free(job->metadata);
        free(job);
        return NULL;
    }
    
    job->status = k8s_job_status_new();
    if (!job->status) {
        k8s_job_spec_free(job->spec);
        k8s_metadata_free(job->metadata);
        free(job);
        return NULL;
    }
    
    return job;
}

void k8s_job_free(k8s_job_t* job) {
    if (!job) return;
    
    if (job->metadata) k8s_metadata_free(job->metadata);
    if (job->spec) k8s_job_spec_free(job->spec);
    if (job->status) k8s_job_status_free(job->status);
    
    free(job);
}

// ============ Job Configuration ============

int k8s_job_set_parallelism(k8s_job_t* job, int parallelism) {
    if (!job || !job->spec || parallelism <= 0) return -1;
    job->spec->parallelism = parallelism;
    return 0;
}

int k8s_job_set_completions(k8s_job_t* job, int completions) {
    if (!job || !job->spec || completions <= 0) return -1;
    job->spec->completions = completions;
    return 0;
}

int k8s_job_set_backoff_limit(k8s_job_t* job, int limit) {
    if (!job || !job->spec || limit < 0) return -1;
    job->spec->backoff_limit = limit;
    return 0;
}

int k8s_job_set_restart_policy(k8s_job_t* job, k8s_job_restart_policy_t policy) {
    if (!job || !job->spec) return -1;
    job->spec->restart_policy = policy;
    return 0;
}

int k8s_job_set_pod_template(k8s_job_t* job, const char* template_json) {
    if (!job || !job->spec || !template_json) return -1;
    
    if (job->spec->pod_template_json) free(job->spec->pod_template_json);
    
    job->spec->pod_template_json = malloc(strlen(template_json) + 1);
    if (!job->spec->pod_template_json) return -1;
    
    strcpy(job->spec->pod_template_json, template_json);
    return 0;
}

void k8s_job_mark_started(k8s_job_t* job) {
    if (job && job->status) {
        job->status->start_time = time(NULL);
        k8s_job_add_condition(job->status, "Active", "True");
    }
}

void k8s_job_mark_completed(k8s_job_t* job) {
    if (job && job->status) {
        job->status->completion_time = time(NULL);
        k8s_job_add_condition(job->status, "Complete", "True");
    }
}

void k8s_job_increment_active(k8s_job_status_t* status, int count) {
    if (status && count > 0) {
        status->active += count;
    }
}

void k8s_job_increment_succeeded(k8s_job_status_t* status, int count) {
    if (status && count > 0) {
        status->succeeded += count;
        status->active = status->active > count ? status->active - count : 0;
    }
}

void k8s_job_increment_failed(k8s_job_status_t* status, int count) {
    if (status && count > 0) {
        status->failed += count;
        status->active = status->active > count ? status->active - count : 0;
    }
}

// ============ Job Serialization ============

char* k8s_job_to_json(k8s_job_t* job) {
    if (!job || !job->metadata) return NULL;
    
    json_object* obj = json_object_new_object();
    
    json_object_object_add(obj, "apiVersion", json_object_new_string("batch/v1"));
    json_object_object_add(obj, "kind", json_object_new_string("Job"));
    
    // Metadata
    json_object* meta = json_object_new_object();
    json_object_object_add(meta, "name", json_object_new_string(job->metadata->name));
    json_object_object_add(meta, "namespace", json_object_new_string(job->metadata->namespace));
    if (job->metadata->uid) {
        json_object_object_add(meta, "uid", json_object_new_string(job->metadata->uid));
    }
    json_object_object_add(obj, "metadata", meta);
    
    // Spec
    if (job->spec) {
        json_object* spec = json_object_new_object();
        json_object_object_add(spec, "parallelism", json_object_new_int(job->spec->parallelism));
        json_object_object_add(spec, "completions", json_object_new_int(job->spec->completions));
        json_object_object_add(spec, "backoffLimit", json_object_new_int(job->spec->backoff_limit));
        json_object_object_add(obj, "spec", spec);
    }
    
    // Status
    if (job->status) {
        json_object* status = json_object_new_object();
        json_object_object_add(status, "active", json_object_new_int(job->status->active));
        json_object_object_add(status, "succeeded", json_object_new_int(job->status->succeeded));
        json_object_object_add(status, "failed", json_object_new_int(job->status->failed));
        json_object_object_add(obj, "status", status);
    }
    
    const char* json_str = json_object_to_json_string(obj);
    char* result = malloc(strlen(json_str) + 1);
    if (result) strcpy(result, json_str);
    
    json_object_put(obj);
    return result;
}

k8s_job_t* k8s_job_from_json(const char* json) {
    if (!json) return NULL;
    
    // TODO: Implement full JSON parsing
    return NULL;  // Stub implementation
}

// ============ CronJob Specification ============

k8s_cronjob_spec_t* k8s_cronjob_spec_new() {
    k8s_cronjob_spec_t* spec = calloc(1, sizeof(k8s_cronjob_spec_t));
    if (!spec) return NULL;
    
    spec->suspend = false;
    spec->concurrency_policy = K8S_CRON_ALLOW;
    spec->success_history_limit = 3;
    spec->failure_history_limit = 1;
    
    return spec;
}

void k8s_cronjob_spec_free(k8s_cronjob_spec_t* spec) {
    if (!spec) return;
    free(spec->schedule);
    free(spec->timezone);
    free(spec->job_template_json);
    free(spec);
}

// ============ CronJob Status ============

k8s_cronjob_status_t* k8s_cronjob_status_new() {
    k8s_cronjob_status_t* status = calloc(1, sizeof(k8s_cronjob_status_t));
    if (!status) return NULL;
    
    status->num_active_jobs = 0;
    return status;
}

void k8s_cronjob_status_free(k8s_cronjob_status_t* status) {
    if (!status) return;
    
    free(status->last_schedule_time_str);
    free(status->last_successful_time_str);
    
    for (int i = 0; i < status->num_active_jobs; i++) {
        free(status->active_job_names[i]);
    }
    free(status->active_job_names);
    
    free(status);
}

int k8s_cronjob_add_active_job(k8s_cronjob_status_t* status, const char* job_name) {
    if (!status || !job_name) return -1;
    
    return add_string_to_array(&status->active_job_names, &status->num_active_jobs, job_name);
}

int k8s_cronjob_remove_active_job(k8s_cronjob_status_t* status, const char* job_name) {
    if (!status || !job_name) return -1;
    
    for (int i = 0; i < status->num_active_jobs; i++) {
        if (strcmp(status->active_job_names[i], job_name) == 0) {
            free(status->active_job_names[i]);
            
            for (int j = i; j < status->num_active_jobs - 1; j++) {
                status->active_job_names[j] = status->active_job_names[j + 1];
            }
            status->num_active_jobs--;
            return 0;
        }
    }
    
    return -1;
}

// ============ CronJob Lifecycle ============

k8s_cronjob_t* k8s_cronjob_new(const char* name, const char* namespace) {
    if (!name || !namespace) return NULL;
    
    k8s_cronjob_t* cronjob = calloc(1, sizeof(k8s_cronjob_t));
    if (!cronjob) return NULL;
    
    cronjob->metadata = k8s_metadata_new(name, namespace);
    if (!cronjob->metadata) {
        free(cronjob);
        return NULL;
    }
    
    cronjob->spec = k8s_cronjob_spec_new();
    if (!cronjob->spec) {
        k8s_metadata_free(cronjob->metadata);
        free(cronjob);
        return NULL;
    }
    
    cronjob->status = k8s_cronjob_status_new();
    if (!cronjob->status) {
        k8s_cronjob_spec_free(cronjob->spec);
        k8s_metadata_free(cronjob->metadata);
        free(cronjob);
        return NULL;
    }
    
    return cronjob;
}

void k8s_cronjob_free(k8s_cronjob_t* cronjob) {
    if (!cronjob) return;
    
    if (cronjob->metadata) k8s_metadata_free(cronjob->metadata);
    if (cronjob->spec) k8s_cronjob_spec_free(cronjob->spec);
    if (cronjob->status) k8s_cronjob_status_free(cronjob->status);
    
    free(cronjob);
}

// ============ CronJob Configuration ============

int k8s_cronjob_set_schedule(k8s_cronjob_t* cronjob, const char* schedule) {
    if (!cronjob || !cronjob->spec || !schedule) return -1;
    
    if (!k8s_cronjob_is_valid_schedule(schedule)) return -1;
    
    if (cronjob->spec->schedule) free(cronjob->spec->schedule);
    
    cronjob->spec->schedule = malloc(strlen(schedule) + 1);
    if (!cronjob->spec->schedule) return -1;
    
    strcpy(cronjob->spec->schedule, schedule);
    return 0;
}

int k8s_cronjob_set_job_template(k8s_cronjob_t* cronjob, const char* template_json) {
    if (!cronjob || !cronjob->spec || !template_json) return -1;
    
    if (cronjob->spec->job_template_json) free(cronjob->spec->job_template_json);
    
    cronjob->spec->job_template_json = malloc(strlen(template_json) + 1);
    if (!cronjob->spec->job_template_json) return -1;
    
    strcpy(cronjob->spec->job_template_json, template_json);
    return 0;
}

int k8s_cronjob_set_concurrency_policy(k8s_cronjob_t* cronjob, k8s_cron_concurrency_policy_t policy) {
    if (!cronjob || !cronjob->spec) return -1;
    cronjob->spec->concurrency_policy = policy;
    return 0;
}

// ============ CronJob Schedule Parsing ============

bool k8s_cronjob_is_valid_schedule(const char* schedule) {
    if (!schedule) return false;
    
    // Basic cron format validation: minute hour day month weekday
    int fields = 0;
    int i = 0;
    
    while (schedule[i] != '\0') {
        if (schedule[i] == ' ') {
            fields++;
        }
        i++;
    }
    
    // Valid if we have exactly 5 space-separated fields
    return fields == 4;  // 5 fields = 4 spaces
}

time_t k8s_cronjob_next_execution_time(const char* schedule, time_t from_time) {
    if (!schedule) return 0;
    
    // TODO: Implement proper cron schedule calculation
    // For now, return next hour
    return from_time + 3600;
}

// ============ CronJob Serialization ============

char* k8s_cronjob_to_json(k8s_cronjob_t* cronjob) {
    if (!cronjob || !cronjob->metadata) return NULL;
    
    json_object* obj = json_object_new_object();
    
    json_object_object_add(obj, "apiVersion", json_object_new_string("batch/v1"));
    json_object_object_add(obj, "kind", json_object_new_string("CronJob"));
    
    // Metadata
    json_object* meta = json_object_new_object();
    json_object_object_add(meta, "name", json_object_new_string(cronjob->metadata->name));
    json_object_object_add(meta, "namespace", json_object_new_string(cronjob->metadata->namespace));
    json_object_object_add(obj, "metadata", meta);
    
    // Spec
    if (cronjob->spec) {
        json_object* spec = json_object_new_object();
        if (cronjob->spec->schedule) {
            json_object_object_add(spec, "schedule", json_object_new_string(cronjob->spec->schedule));
        }
        json_object_object_add(spec, "suspend", json_object_new_boolean(cronjob->spec->suspend));
        json_object_object_add(obj, "spec", spec);
    }
    
    // Status
    if (cronjob->status) {
        json_object* status = json_object_new_object();
        json_object_object_add(status, "activeJobs", json_object_new_int(cronjob->status->num_active_jobs));
        json_object_object_add(obj, "status", status);
    }
    
    const char* json_str = json_object_to_json_string(obj);
    char* result = malloc(strlen(json_str) + 1);
    if (result) strcpy(result, json_str);
    
    json_object_put(obj);
    return result;
}

k8s_cronjob_t* k8s_cronjob_from_json(const char* json) {
    if (!json) return NULL;
    
    // TODO: Implement full JSON parsing
    return NULL;  // Stub implementation
}
