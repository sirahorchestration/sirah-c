# Week 8 Integration & Usage Guide

## Quick Start

### Compile All Week 8 Code
```bash
cd c:/projects/k8s_unikernels/sirah
make clean
make all
```

### Initialize Week 8 Components

```c
// In your main.c or initialization code

// 1. Initialize Job Controller
k8s_job_controller_t* job_ctrl = k8s_job_controller_global();

// 2. Initialize CronJob Scheduler
k8s_cron_scheduler_t* cron_sched = k8s_cron_scheduler_global();

// 3. Initialize Affinity Scheduler
k8s_affinity_scheduler_t* aff_sched = k8s_affinity_scheduler_global();

// 4. Initialize Taint Controller
k8s_taint_controller_t* taint_ctrl = k8s_taint_controller_global();

// 5. Initialize HPA Controller
k8s_hpa_controller_t* hpa_ctrl = k8s_hpa_controller_global();

// 6. Initialize Quota Controller
k8s_quota_controller_t* quota_ctrl = k8s_quota_controller_global();

// 7. Initialize Health System
k8s_system_health_t* health = k8s_system_health_global();

// 8. Initialize Cluster Status
k8s_cluster_status_t* status = k8s_cluster_status_global();
```

## Phase 8.1: Jobs & CronJobs

### Create a Job
```c
// Create Job object
k8s_job_t* job = k8s_job_new("my-job", "default");

// Configure Job
k8s_job_set_parallelism(job, 4);
k8s_job_set_completions(job, 10);
k8s_job_set_backoff_limit(job, 3);

// Set pod template (in JSON format)
const char* pod_template = "{\"spec\":{\"containers\":[{\"name\":\"worker\",\"image\":\"my-worker:1.0\"}]}}";
k8s_job_set_pod_template(job, pod_template);

// Register with controller
k8s_job_controller_create(job_ctrl, job);

// Start the job (creates pods)
k8s_job_controller_start_job(job_ctrl, job);
```

### Create a CronJob
```c
// Create CronJob object
k8s_cronjob_t* cj = k8s_cronjob_new("backup-job", "default");

// Configure schedule (cron format)
k8s_cronjob_set_schedule(cj, "0 2 * * *");  // 2 AM daily

// Set job template
const char* job_template = "{\"spec\":{\"template\":{...}}}";
k8s_cronjob_set_job_template(cj, job_template);

// Set concurrency policy
k8s_cronjob_set_concurrency_policy(cj, K8S_CRON_FORBID);

// Register with scheduler
k8s_cron_scheduler_register(cron_sched, cj);

// Periodically evaluate schedules
k8s_job_t** jobs_to_create = NULL;
int num_jobs = 0;
k8s_cron_scheduler_evaluate_schedules(cron_sched, &jobs_to_create, &num_jobs);
for (int i = 0; i < num_jobs; i++) {
    k8s_job_controller_create(job_ctrl, jobs_to_create[i]);
}
```

### Check Job Status
```c
// Get job
k8s_job_t* job = k8s_job_controller_get(job_ctrl, "my-job", "default");

// Check completion
int succeeded = job->status->succeeded;
int total_completions = job->spec->completions;
if (succeeded >= total_completions) {
    printf("Job completed!\n");
}

// Check for failures
int failed = job->status->failed;
int backoff_limit = job->spec->backoff_limit;
if (failed >= backoff_limit) {
    printf("Job failed due to excessive retries\n");
}
```

## Phase 8.2: Affinity & Taints

### Configure Node Affinity
```c
// Create affinity object
k8s_affinity_t* affinity = k8s_affinity_new();

// Create node affinity
k8s_node_affinity_t* node_aff = k8s_node_affinity_new();

// Add required term (pod must be on nodes with label gpu=true)
k8s_node_selector_term_t* term = malloc(sizeof(k8s_node_selector_term_t));
term->match_expressions = (char**)malloc(1 * sizeof(char*));
term->match_expressions[0] = "gpu=true";
term->num_expressions = 1;

k8s_node_affinity_add_required_term(node_aff, term);
affinity->node_affinity = node_aff;

// Apply to pod
pod->spec->affinity = affinity;
```

### Apply Taints to Node
```c
// Create taint
k8s_taint_t* taint = k8s_taint_new("gpu", "true", "NoSchedule");

// Register node with taint controller
k8s_taint_controller_register_node(taint_ctrl, "gpu-node-1");

// Add taint to node
k8s_taint_controller_add_taint(taint_ctrl, "gpu-node-1", taint);
```

### Add Toleration to Pod
```c
// Create toleration
k8s_toleration_t* toleration = k8s_toleration_new("gpu", "Equal", "true", "NoSchedule");

// Add to pod
if (!pod->spec->tolerations) {
    pod->spec->tolerations = (k8s_toleration_t**)malloc(10 * sizeof(k8s_toleration_t*));
    pod->spec->num_tolerations = 0;
}
pod->spec->tolerations[pod->spec->num_tolerations++] = toleration;
```

### Schedule Pod with Affinity
```c
// Get available nodes
k8s_node_t* available[10];
int num_available = 0;
// ... populate available nodes ...

// Select best node based on affinity
k8s_node_t* selected = k8s_affinity_scheduler_select_node(aff_sched, pod, available, num_available);

if (selected) {
    printf("Pod scheduled on node: %s\n", selected->metadata.name);
}
```

## Phase 8.3: Horizontal Pod Autoscaling

### Create HPA
```c
// Create HPA object
k8s_hpa_t* hpa = k8s_hpa_new("web-scaler", "default");

// Set scaling target
k8s_hpa_set_scale_target(hpa, "Deployment/web-app");

// Set replica limits
k8s_hpa_set_replicas(hpa, 2, 10);

// Set CPU target
k8s_hpa_set_target_cpu(hpa, 80);  // Scale at 80% CPU

// Set behavior
k8s_hpa_set_upscale_behavior(hpa, 60, 50);    // Scale up 50% every 60s
k8s_hpa_set_downscale_behavior(hpa, 300, 50); // Scale down 50% every 300s

// Register with HPA controller
k8s_hpa_controller_create(hpa_ctrl, hpa);
```

### Trigger Scaling Decision
```c
// Update current metrics for target
k8s_hpa_controller_update_metrics(hpa_ctrl, "Deployment/web-app", 85, 60);

// Evaluate all HPAs
char** targets = NULL;
int* desired_replicas = NULL;
int num_decisions = 0;

k8s_hpa_controller_evaluate_all(hpa_ctrl, &targets, &desired_replicas, &num_decisions);

// Apply scaling decisions
for (int i = 0; i < num_decisions; i++) {
    printf("Scaling %s to %d replicas\n", targets[i], desired_replicas[i]);
    k8s_hpa_controller_scale_target(hpa_ctrl, targets[i], desired_replicas[i]);
}
```

## Phase 8.4: Resource Quotas & Limits

### Create ResourceQuota
```c
// Create quota object
k8s_resource_quota_t* quota = k8s_resource_quota_new("default-quota", "production");

// Set hard limits
k8s_resource_quota_set_hard_limit(quota, "cpu", 100000);        // 100 cores
k8s_resource_quota_set_hard_limit(quota, "memory", 200000000000); // 200Gi
k8s_resource_quota_set_hard_limit(quota, "pods", 100);

// Set soft limits (warnings at 80%)
k8s_resource_quota_set_soft_limit(quota, "cpu", 80000);
k8s_resource_quota_set_soft_limit(quota, "memory", 160000000000);

// Register with quota controller
k8s_quota_controller_create_quota(quota_ctrl, quota);
```

### Apply LimitRange Defaults
```c
// Create LimitRange object
k8s_limit_range_t* lr = k8s_limit_range_new("default-limits", "production");

// Set container limits
k8s_resource_quantity_t* min = k8s_resource_quantity_new();
min->cpu_millicores = 100;
min->memory_bytes = 128 * 1024 * 1024;

k8s_resource_quantity_t* max = k8s_resource_quantity_new();
max->cpu_millicores = 4000;
max->memory_bytes = 4LL * 1024 * 1024 * 1024;

k8s_limit_range_set_container_limits(lr, min, max);

// Set container default requests
k8s_resource_quantity_t* request = k8s_resource_quantity_new();
request->cpu_millicores = 500;
request->memory_bytes = 512 * 1024 * 1024;

k8s_resource_quantity_t* limit = k8s_resource_quantity_new();
limit->cpu_millicores = 1000;
limit->memory_bytes = 1024 * 1024 * 1024;

k8s_limit_range_set_container_defaults(lr, request, limit);

// Register with quota controller
k8s_quota_controller_create_limit_range(quota_ctrl, lr);
```

### Check Quota Before Pod Creation
```c
// Prepare requested resources
k8s_resource_quantity_t* requested = k8s_resource_quantity_new();
requested->cpu_millicores = 500;
requested->memory_bytes = 512 * 1024 * 1024;
requested->pods_count = 1;

// Check if namespace quota allows
if (k8s_quota_controller_can_admit_resource(quota_ctrl, "production", requested)) {
    // Create pod
    k8s_pod_t* pod = k8s_pod_new("app-pod", "production");
    // ... configure pod ...
    
    // Record usage
    k8s_quota_controller_record_resource_usage(quota_ctrl, "production", requested);
} else {
    printf("Quota exceeded, cannot create pod\n");
}
```

## Phase 8.5: Health & Diagnostics

### Get System Health
```c
// Get health system
k8s_system_health_t* health = k8s_system_health_global();

// Check /healthz endpoint
char* healthz_response = k8s_api_healthz_handler(health);
printf("Health response: %s\n", healthz_response);

// Check /readyz endpoint
char* readyz_response = k8s_api_readyz_handler(health);
printf("Ready response: %s\n", readyz_response);
```

### Report Component Health
```c
// Update component status
k8s_system_health_check_component(health, "etcd", K8S_COMPONENT_HEALTHY, "etcd backend operational");
k8s_system_health_check_component(health, "scheduler", K8S_COMPONENT_HEALTHY, "scheduler running");

// Get full health JSON
char* health_json = k8s_system_health_to_json(health);
printf("Full health: %s\n", health_json);
```

### Get Cluster Status
```c
// Get cluster status
k8s_cluster_status_t* status = k8s_cluster_status_global();

// Set capacity
k8s_cluster_capacity_t cap;
cap.total_cpu_millicores = 32000;      // 32 cores
cap.total_memory_bytes = 128000000000;  // 128Gi
cap.allocatable_cpu_millicores = 30000;
cap.allocatable_memory_bytes = 120000000000;
k8s_cluster_status_set_capacity(status, &cap);

// Set usage
k8s_cluster_usage_t usage;
usage.used_cpu_millicores = 12000;  // 50% utilized
usage.used_memory_bytes = 60000000000;
usage.num_pods = 150;
usage.num_nodes = 5;
k8s_cluster_status_update_usage(status, &usage);

// Set node/pod status
k8s_cluster_status_set_node_status(status, 5, 5, 0, 0);  // 5 ready
k8s_cluster_status_set_pod_status(status, 150, 140, 5, 3, 2, 0);

// Get cluster status JSON
char* status_json = k8s_cluster_status_to_json(status);
printf("Cluster status: %s\n", status_json);

// Get health score
int health_score = k8s_cluster_status_calculate_health_score(status);
printf("Cluster health: %d%%\n", health_score);
```

## API Server Integration

### Add Health Endpoints
```c
// In your HTTP handler code (server.c or handler.c)

// Handle /healthz
if (strcmp(request->path, "/healthz") == 0) {
    k8s_system_health_t* health = k8s_system_health_global();
    char* response = k8s_api_healthz_handler(health);
    
    if (health->overall_status == K8S_COMPONENT_UNHEALTHY) {
        send_response(client, 503, response);  // Service Unavailable
    } else {
        send_response(client, 200, response);  // OK
    }
    free(response);
}

// Handle /readyz
if (strcmp(request->path, "/readyz") == 0) {
    k8s_system_health_t* health = k8s_system_health_global();
    char* response = k8s_api_readyz_handler(health);
    
    if (health->overall_status == K8S_COMPONENT_HEALTHY) {
        send_response(client, 200, response);  // Ready
    } else {
        send_response(client, 503, response);  // Not Ready
    }
    free(response);
}

// Handle /api/v1/clusterStatus
if (strcmp(request->path, "/api/v1/clusterStatus") == 0) {
    k8s_cluster_status_t* status = k8s_cluster_status_global();
    char* response = k8s_cluster_status_to_json(status);
    
    send_response(client, 200, response);
    free(response);
}
```

## Common Patterns

### Periodic Background Tasks
```c
// In main event loop
void background_tasks() {
    // Evaluate CronJobs every minute
    static time_t last_cron_eval = 0;
    time_t now = time(NULL);
    
    if (now - last_cron_eval >= 60) {
        k8s_cron_scheduler_t* cron_sched = k8s_cron_scheduler_global();
        k8s_job_t** jobs_to_create = NULL;
        int num_jobs = 0;
        
        k8s_cron_scheduler_evaluate_schedules(cron_sched, &jobs_to_create, &num_jobs);
        // ... create jobs ...
        
        last_cron_eval = now;
    }
    
    // Evaluate HPA every 15 seconds
    static time_t last_hpa_eval = 0;
    if (now - last_hpa_eval >= 15) {
        k8s_hpa_controller_t* hpa_ctrl = k8s_hpa_controller_global();
        
        char** targets = NULL;
        int* desired_replicas = NULL;
        int num_decisions = 0;
        
        k8s_hpa_controller_evaluate_all(hpa_ctrl, &targets, &desired_replicas, &num_decisions);
        // ... apply scaling ...
        
        last_hpa_eval = now;
    }
    
    // Check completed jobs every 30 seconds
    static time_t last_job_cleanup = 0;
    if (now - last_job_cleanup >= 30) {
        k8s_job_controller_t* job_ctrl = k8s_job_controller_global();
        k8s_job_controller_cleanup_completed(job_ctrl);
        
        last_job_cleanup = now;
    }
}
```

## Memory Cleanup

### Proper Shutdown
```c
void cleanup_week8_systems() {
    // These are global singletons, so cleanup happens at exit
    // But if you need to explicitly free:
    
    // Note: In production, these typically persist for the lifetime
    // of the application. Only free if completely shutting down.
    
    k8s_job_controller_t* job_ctrl = k8s_job_controller_global();
    if (job_ctrl) {
        k8s_job_controller_free(job_ctrl);
    }
    
    // Similar for other controllers...
}
```

## Monitoring & Debugging

### Track Resource Usage
```c
// Query quota usage
int usage_percent = 0;
k8s_quota_controller_get_usage_percent(quota_ctrl, "production", "cpu", &usage_percent);
printf("CPU usage: %d%%\n", usage_percent);

// Get HPA recommendations
int recommended_replicas = 0;
k8s_hpa_controller_get_recommendations(hpa_ctrl, "Deployment/web-app", &recommended_replicas);
printf("HPA recommends: %d replicas\n", recommended_replicas);

// Check job status
k8s_job_t* job = k8s_job_controller_get(job_ctrl, "batch-job", "default");
printf("Job: %d succeeded, %d failed out of %d completions\n",
       job->status->succeeded, job->status->failed, job->spec->completions);
```

---

**Week 8 is now complete and integrated!**

All production-ready features are implemented and ready for testing and deployment.
