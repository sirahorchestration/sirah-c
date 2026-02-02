# Sirah Week 3-5 Implementation Plan

**Status**: Planning Phase  
**Target Duration**: 3 weeks  
**Target Code**: 3500-4000 additional lines  
**Overall Progress Target**: 75-80% of MVP

---

## Executive Summary

Week 3-5 builds on the solid foundation of Week 1-2, expanding from a basic control plane into a production-capable Kubernetes implementation. This phase focuses on:

1. **Complete Pod Lifecycle Management** - Full pod status phases, quality-of-service, preemption
2. **Advanced Scheduling** - Node affinity, pod affinity, taints/tolerations
3. **All Core Controllers** - DaemonSet, StatefulSet, Job, CronJob controllers
4. **Networking Stack** - CNI integration, DNS, service networking
5. **Storage Integration** - PersistentVolumes, claims, mounting
6. **Observability** - Pod events, resource metrics, logging integration

---

## Architecture Overview

### Current State (End of Week 2)

```
API Server (fully functional)
  ├── CRUD for Pods, Nodes, Deployments
  ├── etcd persistence
  └── HTTP REST API (6443)

Scheduler (functional)
  ├── Pod placement (round-robin)
  ├── Watch pending pods
  └── Bind to nodes

Controllers (partial)
  ├── Deployment Controller (working)
  ├── ReplicaSet Controller (working)
  ├── Others (stubs)
  └── Reconciliation loops active

Node Status
  ├── Heartbeat tracking
  ├── Capacity tracking
  └── Status enum (Ready, NotReady, Unknown)

Pod Status
  ├── Basic phases (Pending, Running, Succeeded, Failed)
  └── Limited container status
```

### Target State (End of Week 5)

```
API Server (production-ready)
  ├── Full CRUD for all resource types
  ├── Watch/List with filtering
  ├── etcd persistence + caching
  ├── Validation + admission webhooks
  ├── Resource versioning
  └── Error handling + proper HTTP codes

Scheduler (advanced)
  ├── Predicate filters (10+)
  ├── Priority scoring plugins (8+)
  ├── Preemption logic
  ├── Node affinity/anti-affinity
  ├── Pod affinity/anti-affinity
  ├── Taints and tolerations
  └── Queue management

Controllers (complete)
  ├── Deployment Controller
  ├── ReplicaSet Controller
  ├── DaemonSet Controller
  ├── StatefulSet Controller
  ├── Job Controller
  ├── CronJob Controller (basic)
  ├── Service Controller
  └── Endpoint Controller

Node Management
  ├── Kubelet agent (basic)
  ├── Container status reporting
  ├── Health checks
  └── Resource metrics

Pod Lifecycle
  ├── All 5 phases (Pending, Running, Succeeded, Failed, Unknown)
  ├── Container status (Waiting, Running, Terminated)
  ├── Quality of Service (Guaranteed, Burstable, BestEffort)
  ├── Preemption & priority
  └── Init containers

Networking
  ├── CNI plugin system
  ├── Pod networking setup
  ├── Service load balancing
  ├── DNS integration (CoreDNS)
  └── Network policies (basic)

Storage
  ├── PersistentVolume abstraction
  ├── PersistentVolumeClaim binding
  ├── Volume mounting
  └── Storage classes (basic)

Observability
  ├── Event system
  ├── Resource metrics endpoint
  ├── Pod logging integration
  └── Audit logging (basic)
```

---

## Week 3: Advanced Pod Lifecycle & Networking (4-5 days)

### Goals
- Complete pod lifecycle phases and statuses
- Implement kubelet agent (basic)
- Add networking integration
- Implement events system

### Detailed Tasks

#### 3.1: Pod Lifecycle - Complete Implementation (1 day)

**What to Add**:
- Pod phase transitions (Pending → Running → Succeeded/Failed)
- Container status tracking (Waiting → Running → Terminated)
- Restart policies (Always, OnFailure, Never)
- Init containers
- Ready conditions

**Files to Create/Update**:
- `pkg/types/pod.c` - Add phase tracking, conditions
- `internal/controller/pod_lifecycle.c/h` - NEW: Lifecycle controller
- `internal/apiserver/endpoints.c` - Update pod status endpoint

**Code Skeleton** (~150 lines):
```c
// Pod phase transitions
typedef enum {
    POD_PHASE_PENDING = 0,
    POD_PHASE_RUNNING = 1,
    POD_PHASE_SUCCEEDED = 2,
    POD_PHASE_FAILED = 3,
    POD_PHASE_UNKNOWN = 4
} pod_phase_t;

typedef struct {
    char* type;              // "Ready", "Initialized", "PodScheduled"
    bool status;             // True/False
    time_t last_transition;
    char* reason;
    char* message;
} pod_condition_t;

// Update pod status
int update_pod_status(k8s_pod_t* pod) {
    // Transition phases based on container status
    // Update conditions array
    // Track timing for readiness probes
    return 0;
}

// Reconcile actual vs desired state
int reconcile_pod(k8s_pod_t* pod) {
    // Check container status
    // Update pod phase
    // Handle restart policies
    // Trigger events
    return 0;
}
```

**Acceptance Criteria**:
- [ ] Pod phases transitioning correctly
- [ ] Container status tracked
- [ ] Restart policies enforced
- [ ] Conditions updated
- [ ] Events generated

---

#### 3.2: Kubelet Agent - Basic Implementation (1.5 days)

**What to Add**:
- Basic kubelet server
- Pod synchronization loop
- Container status reporting
- Health probes (liveness/readiness)

**Files to Create**:
- `cmd/node-agent/main.c` - Entry point
- `internal/kubelet/kubelet.c/h` - NEW: Main kubelet loop
- `internal/kubelet/container.c/h` - NEW: Container runtime
- `internal/kubelet/pod.c/h` - NEW: Pod execution
- `internal/kubelet/probes.c/h` - NEW: Liveness/readiness checks

**Code Skeleton** (~300 lines):
```c
// Kubelet main loop
int kubelet_run(const char* node_name, const char* api_server) {
    while (running) {
        // 1. Get list of pods assigned to this node
        k8s_pod_t** pods = api_get_pods_for_node(node_name);
        
        // 2. Sync desired state (create/update/delete containers)
        for (int i = 0; pods[i]; i++) {
            sync_pod(pods[i]);
        }
        
        // 3. Update pod status
        for (int i = 0; pods[i]; i++) {
            update_pod_status(pods[i]);
        }
        
        // 4. Send heartbeat
        send_node_heartbeat(node_name);
        
        sleep(10);  // Sync every 10 seconds
    }
    return 0;
}

// Sync a pod (create containers, mount volumes, etc)
int sync_pod(k8s_pod_t* pod) {
    // For each container spec:
    //   - Pull image
    //   - Create container
    //   - Mount volumes
    //   - Start container
    return 0;
}

// Run health probes
int check_pod_health(k8s_pod_t* pod) {
    for (int i = 0; i < pod->container_count; i++) {
        // Run liveness probe
        if (should_restart_container(&pod->containers[i])) {
            restart_container(&pod->containers[i]);
        }
        
        // Run readiness probe
        pod->containers[i].ready = run_readiness_probe(&pod->containers[i]);
    }
    return 0;
}
```

**Acceptance Criteria**:
- [ ] Kubelet server starts on 10250
- [ ] Syncs pods from API server
- [ ] Reports container status
- [ ] Restarts failed containers
- [ ] Responds to healthz queries

---

#### 3.3: Networking Integration (1.5 days)

**What to Add**:
- CNI plugin system
- Pod network assignment
- Service DNS names
- Basic service load balancing

**Files to Create**:
- `pkg/networking/cni.c/h` - NEW: CNI interface
- `pkg/networking/flannel.c/h` - NEW: Flannel plugin
- `pkg/networking/service_proxy.c/h` - NEW: Service routing
- `internal/apiserver/service_endpoints.c` - Update: Service endpoint controller

**Code Skeleton** (~200 lines):
```c
// CNI plugin interface
typedef struct {
    char* name;              // "flannel", "weave", etc
    int (*setup_pod_network)(k8s_pod_t* pod);
    int (*teardown_pod_network)(k8s_pod_t* pod);
} cni_plugin_t;

// Setup pod network
int setup_pod_network(k8s_pod_t* pod) {
    // Call CNI ADD
    // Assign IP from IPAM
    // Configure routes
    // Save network config to pod status
    return 0;
}

// Service load balancing
typedef struct {
    char* service_name;
    char* service_ip;
    int* backend_ports;
    k8s_endpoint_t* endpoints;  // List of healthy pods
} service_proxy_t;

int route_service_traffic(const char* service_name) {
    // Get service object
    k8s_service_t* svc = api_get_service(service_name);
    
    // Get all endpoints for service
    k8s_endpoint_t* endpoints = api_get_endpoints(service_name);
    
    // Setup routing/proxying rules
    // Forward traffic to random healthy endpoint
    return 0;
}
```

**Acceptance Criteria**:
- [ ] Pod gets IP from CNI
- [ ] Pod network connectivity works
- [ ] Service DNS names resolve
- [ ] Service traffic routes to pods
- [ ] Endpoint controller syncs endpoints

---

#### 3.4: Events System (0.5 days)

**What to Add**:
- Event object type
- Event creation for pod lifecycle events
- Event retention/cleanup
- API endpoint for querying events

**Files to Create/Update**:
- `pkg/types/event.c/h` - NEW: Event type
- `internal/controller/events.c/h` - NEW: Event system
- `internal/apiserver/endpoints.c` - Add /api/v1/events

**Code Skeleton** (~100 lines):
```c
typedef struct {
    k8s_object_meta_t metadata;
    char* reason;                 // "SuccessfulCreate", "FailedScheduling"
    char* message;
    char* involved_object_name;   // Pod name, Deployment name, etc
    char* type;                   // "Normal", "Warning"
    time_t first_timestamp;
    time_t last_timestamp;
    int count;
} k8s_event_t;

// Create event
int create_event(const char* reason, const char* message, 
                 const char* involved_object_type, const char* involved_object_name) {
    k8s_event_t* event = malloc(sizeof(*event));
    event->reason = strdup(reason);
    event->message = strdup(message);
    event->type = "Normal";  // or "Warning"
    event->count = 1;
    
    // Store in etcd
    store_put_event(event);
    return 0;
}
```

**Acceptance Criteria**:
- [ ] Events created for pod lifecycle changes
- [ ] Events queryable via API
- [ ] Old events cleaned up
- [ ] kubectl describe shows events

---

### Testing for Week 3

```bash
# Test pod lifecycle
kubectl apply -f test-pod.yaml
kubectl get pod <name> -w           # Watch status changes
kubectl describe pod <name>         # See events

# Test kubelet
./bin/node-agent --node-name=node1 --api-server=http://localhost:6443

# Test networking
kubectl run -it test --image=busybox -- /bin/sh
# Inside pod: ping <other-pod-ip>

# Test service
kubectl expose pod test --port=8080
kubectl get service test
```

---

## Week 4: Advanced Controllers & Scheduling (5 days)

### Goals
- Complete all controller implementations
- Add advanced scheduling features
- Implement quality of service

### Detailed Tasks

#### 4.1: DaemonSet, StatefulSet, Job Controllers (1.5 days)

**DaemonSet Controller** (~150 lines):
```c
// Ensure pod runs on every node
int daemonset_controller_loop() {
    while (running) {
        // 1. Get all DaemonSet objects
        k8s_daemonset_t** daemonsets = api_get_daemonsets();
        
        for (int i = 0; daemonsets[i]; i++) {
            k8s_daemonset_t* ds = daemonsets[i];
            
            // 2. Get all nodes
            k8s_node_t** nodes = api_get_nodes();
            
            // 3. For each node, ensure one pod exists
            for (int j = 0; nodes[j]; j++) {
                if (node_matches_selector(nodes[j], ds->node_selector)) {
                    ensure_pod_on_node(ds->pod_template, nodes[j]);
                }
            }
        }
        
        sleep(5);
    }
    return 0;
}
```

**StatefulSet Controller** (~150 lines):
```c
// Manage stateful applications
int statefulset_controller_loop() {
    // Similar to deployment but:
    // - Pods have stable identities (pod-0, pod-1, pod-2)
    // - Pods are created/destroyed in order
    // - Each pod gets persistent volumes
    // - DNS name: pod-0.service-name, pod-1.service-name, etc
    return 0;
}
```

**Job Controller** (~150 lines):
```c
// Run work to completion
int job_controller_loop() {
    // Watch jobs
    // Create pods for spec.parallelism
    // Track completion
    // Handle retries with backoff
    // Delete finished pods
    return 0;
}
```

**Files to Create**:
- `internal/controller/daemonset.c/h`
- `internal/controller/statefulset.c/h`
- `internal/controller/job.c/h`
- `pkg/types/daemonset.c/h`
- `pkg/types/statefulset.c/h`
- `pkg/types/job.c/h`

**Acceptance Criteria**:
- [ ] DaemonSet pods on all eligible nodes
- [ ] StatefulSet pods with stable names and storage
- [ ] Job runs pods to completion
- [ ] Failed pods retried correctly
- [ ] Scale down deletes correct number of pods

---

#### 4.2: Advanced Scheduling (1.5 days)

**Predicate Filters** (~200 lines):
```c
// Filter nodes that can fit pod
typedef int (*predicate_fn_t)(k8s_pod_t* pod, k8s_node_t* node);

predicate_fn_t predicates[] = {
    predicate_resource_fit,      // CPU, memory available?
    predicate_node_selector,     // Node matches nodeSelector?
    predicate_node_affinity,     // Node affinity rules satisfied?
    predicate_pod_affinity,      // Pod affinity rules satisfied?
    predicate_tolerations,       // Pod tolerates node taints?
    predicate_port_binding,      // Port available?
    predicate_volume_zones,      // Zone requirements met?
    NULL
};

// Priority scoring plugins
typedef int (*priority_fn_t)(k8s_pod_t* pod, k8s_node_t* node);

priority_fn_t priorities[] = {
    priority_least_used,         // Prefer least-used nodes
    priority_most_used,          // Balance workload
    priority_affinity,           // Prefer nodes matching affinity
    priority_image_local,        // Prefer nodes with image cached
    priority_zone_spreading,     // Spread across zones
    NULL
};
```

**Taints and Tolerations** (~100 lines):
```c
typedef struct {
    char* key;
    char* value;
    char* effect;  // "NoSchedule", "NoExecute", "PreferNoSchedule"
} k8s_taint_t;

typedef struct {
    char* key;
    char* operator;   // "Equal", "Exists"
    char* value;
    int toleration_seconds;  // For NoExecute
} k8s_toleration_t;

bool toleration_matches_taint(k8s_toleration_t* tol, k8s_taint_t* taint) {
    if (strcmp(tol->operator, "Equal") == 0) {
        return strcmp(tol->key, taint->key) == 0 &&
               strcmp(tol->value, taint->value) == 0;
    } else {  // "Exists"
        return strcmp(tol->key, taint->key) == 0;
    }
}
```

**Files to Create/Update**:
- `internal/scheduler/predicates.c/h` - NEW: Predicate filters
- `internal/scheduler/priorities.c/h` - NEW: Priority plugins
- `pkg/types/affinity.c/h` - NEW: Affinity types
- `pkg/types/taint.c/h` - NEW: Taint/toleration types

**Acceptance Criteria**:
- [ ] Nodes filtered by predicates
- [ ] Nodes scored by priorities
- [ ] Preemption removes lower-priority pods
- [ ] Node affinity respected
- [ ] Pod affinity spreading works
- [ ] Taints and tolerations enforced

---

#### 4.3: Quality of Service (0.5 days)

**Code** (~80 lines):
```c
typedef enum {
    QOS_GUARANTEED = 0,    // Requests == Limits
    QOS_BURSTABLE = 1,     // Requests < Limits
    QOS_BEST_EFFORT = 2    // No requests/limits
} qos_class_t;

qos_class_t calculate_qos(k8s_pod_t* pod) {
    bool has_requests = false;
    bool has_limits = false;
    bool limits_eq_requests = true;
    
    for (int i = 0; i < pod->container_count; i++) {
        if (pod->containers[i].resources.requests.cpu) has_requests = true;
        if (pod->containers[i].resources.limits.cpu) has_limits = true;
        if (pod->containers[i].resources.requests.cpu != 
            pod->containers[i].resources.limits.cpu) limits_eq_requests = false;
    }
    
    if (has_limits && limits_eq_requests) return QOS_GUARANTEED;
    if (has_requests) return QOS_BURSTABLE;
    return QOS_BEST_EFFORT;
}
```

**Acceptance Criteria**:
- [ ] QoS classes calculated correctly
- [ ] Displayed in kubectl describe
- [ ] Used for eviction decisions

---

#### 4.4: Preemption & Priority (1.5 days)

**Code** (~200 lines):
```c
// Pod priority
typedef struct {
    int priority_value;          // Higher = more important
    char* priority_class_name;   // References PriorityClass
} k8s_priority_t;

// Preemption algorithm
int find_preemption_targets(k8s_pod_t* pending_pod, 
                            k8s_node_t** candidates,
                            k8s_pod_t*** target_pods) {
    // For each candidate node:
    // 1. If pod fits without removing any: consider it
    // 2. If pod fits by removing lower-priority pods: mark those for removal
    // 3. Choose node that requires removing fewest pods
    // 4. Return list of pods to remove
    return 0;
}

// Execute preemption
int preempt_for_pod(k8s_pod_t* pending_pod, k8s_pod_t** victims) {
    // Delete victim pods with grace period
    for (int i = 0; victims[i]; i++) {
        delete_pod(victims[i], 30);  // 30 second grace period
    }
    return 0;
}
```

**Acceptance Criteria**:
- [ ] High-priority pods scheduled before low-priority
- [ ] Low-priority pods preempted if needed
- [ ] Preemption respects grace period
- [ ] Preemption events logged

---

### Testing for Week 4

```bash
# Test DaemonSet
kubectl apply -f daemonset.yaml
kubectl get daemonsets
kubectl get pods -l daemon=true

# Test StatefulSet
kubectl apply -f statefulset.yaml
kubectl get statefulsets
kubectl describe statefulset mysql

# Test Job
kubectl apply -f job.yaml
kubectl get jobs
kubectl logs <job-pod>

# Test advanced scheduling
kubectl apply -f pod-with-affinity.yaml
kubectl apply -f pod-with-taints.yaml

# Test preemption
kubectl apply -f high-priority-pod.yaml  # Should displace low-priority
```

---

## Week 5: Storage, Webhooks & Polish (4-5 days)

### Goals
- Storage integration (PersistentVolumes, PersistentVolumeClaims)
- Webhook system (mutation, validation)
- Resource quota and limits
- Finishing touches

### Detailed Tasks

#### 5.1: Storage System (1.5 days)

**PersistentVolume Controller** (~150 lines):
```c
typedef struct {
    k8s_object_meta_t metadata;
    struct {
        int capacity_bytes;
        char** access_modes;  // "ReadWriteOnce", "ReadOnlyMany", etc
        char* storage_class;
        char* volume_type;    // "hostPath", "nfs", "local"
        union {
            struct { char* path; } host_path;
            struct { char* server; char* path; } nfs;
        } source;
    } spec;
    struct {
        char* phase;          // "Available", "Bound", "Released", "Failed"
        char* claim_ref;      // Bound to which PVC?
    } status;
} k8s_persistent_volume_t;

int pv_controller_loop() {
    while (running) {
        k8s_persistent_volume_t** pvs = api_get_persistent_volumes();
        k8s_persistent_volume_claim_t** pvcs = api_get_claims();
        
        // 1. Match unbound PVCs with available PVs
        for (int i = 0; pvcs[i]; i++) {
            if (!pvcs[i]->status.bound_pv) {
                for (int j = 0; pvs[j]; j++) {
                    if (can_bind(pvcs[i], pvs[j])) {
                        bind_pv_to_pvc(pvs[j], pvcs[i]);
                        break;
                    }
                }
            }
        }
        
        sleep(5);
    }
    return 0;
}
```

**Storage Management** (~150 lines):
```c
// Mount volume in pod
int mount_volume(k8s_pod_t* pod, const char* volume_name, 
                 const char* mount_path) {
    // 1. Get volume spec
    // 2. If PVC, get the bound PV
    // 3. Mount filesystem at mount_path
    // 4. Update pod status
    return 0;
}

// Unmount volume (on pod deletion)
int unmount_volume(k8s_pod_t* pod, const char* volume_name) {
    // Unmount filesystem
    // Cleanup local data if hostPath
    // Update pod status
    return 0;
}
```

**Files to Create**:
- `pkg/types/persistent_volume.c/h`
- `pkg/types/persistent_volume_claim.c/h`
- `internal/controller/pv_controller.c/h`
- `internal/kubelet/volume.c/h`

**Acceptance Criteria**:
- [ ] PV objects created and listed
- [ ] PVC binds to matching PV
- [ ] Volume mounted in pod
- [ ] Data persists across pod recreation
- [ ] PV released when PVC deleted

---

#### 5.2: Admission Webhooks (1 day)

**Mutating Webhooks** (~150 lines):
```c
typedef struct {
    char* name;
    char* webhook_url;           // HTTP endpoint
    char* admissionReviewVersions[10];
    char** rules;                // Which resources to intercept
    bool fail_policy_ignore;     // Ignore webhook on error?
    int timeout_seconds;
} k8s_mutating_webhook_t;

// Call webhook for mutation
int call_mutating_webhook(const char* webhook_url, k8s_object_t* obj,
                          k8s_object_t** modified_obj) {
    // 1. Create AdmissionReview request
    // 2. POST to webhook URL
    // 3. If successful, apply patches from response
    // 4. Return modified object
    return 0;
}

// Example: Add default labels
int mutate_pod_add_labels(k8s_pod_t* pod) {
    // Add app=default label if not present
    // Add timestamp annotation
    return 0;
}
```

**Validating Webhooks** (~150 lines):
```c
typedef struct {
    char* name;
    char* webhook_url;
    char** rules;
    bool fail_policy_fail;       // Reject resource if webhook fails?
} k8s_validating_webhook_t;

// Call webhook for validation
int call_validating_webhook(const char* webhook_url, k8s_object_t* obj,
                            bool* allowed, char** error_msg) {
    // 1. Create AdmissionReview request
    // 2. POST to webhook URL
    // 3. Check 'allowed' field in response
    // 4. If not allowed, return error message
    return 0;
}

// Example: Require resource limits
int validate_pod_has_limits(k8s_pod_t* pod, char** error) {
    for (int i = 0; i < pod->container_count; i++) {
        if (!pod->containers[i].resources.limits.cpu) {
            *error = strdup("CPU limit required");
            return 1;  // Validation failed
        }
    }
    return 0;  // Valid
}
```

**Files to Create**:
- `pkg/types/webhook_config.c/h`
- `internal/apiserver/webhooks.c/h`

**Acceptance Criteria**:
- [ ] Mutating webhooks modify resources before storage
- [ ] Validating webhooks reject invalid resources
- [ ] Webhooks timeout correctly
- [ ] Webhook failures handled according to policy
- [ ] AdmissionReview requests/responses correct

---

#### 5.3: Resource Quota & Limits (1 day)

**ResourceQuota Controller** (~150 lines):
```c
typedef struct {
    k8s_object_meta_t metadata;
    struct {
        // Hard limits for namespace
        int requests_cpu;        // 1000m = 1 CPU
        int requests_memory;     // In MB
        int limits_cpu;
        int limits_memory;
        int pods;               // Max pod count
        int persistentvolumeclaims;
    } spec;
    struct {
        // Current usage
        int requests_cpu_used;
        int requests_memory_used;
    } status;
} k8s_resource_quota_t;

int quota_controller_loop() {
    while (running) {
        k8s_resource_quota_t** quotas = api_get_resource_quotas();
        
        for (int i = 0; quotas[i]; i++) {
            // 1. Calculate current usage
            // 2. Update status.used
            // 3. Check for violations
            // 4. Generate events if exceeded
        }
        
        sleep(10);
    }
    return 0;
}

// Check if pod creation violates quota
int check_quota_before_pod_creation(k8s_pod_t* pod, const char* namespace) {
    k8s_resource_quota_t* quota = api_get_quota(namespace);
    if (!quota) return 0;  // No quota
    
    int pod_cpu_request = calculate_cpu_request(pod);
    int pod_memory_request = calculate_memory_request(pod);
    
    if (quota->status.requests_cpu_used + pod_cpu_request > quota->spec.requests_cpu) {
        return -1;  // Would exceed CPU quota
    }
    if (quota->status.requests_memory_used + pod_memory_request > quota->spec.requests_memory) {
        return -1;  // Would exceed memory quota
    }
    
    return 0;  // OK
}
```

**Files to Create**:
- `pkg/types/resource_quota.c/h`
- `internal/controller/quota_controller.c/h`

**Acceptance Criteria**:
- [ ] Quota enforced on pod creation
- [ ] Usage tracked correctly
- [ ] Events generated when exceeded
- [ ] kubectl describe quota shows usage

---

#### 5.4: Polish & Documentation (0.5-1 days)

**What to Add**:
- Error handling improvements
- Logging/debugging
- Configuration management
- Documentation updates

**Files to Create/Update**:
- `pkg/utils/logging.c/h` - Structured logging
- `pkg/utils/config.c/h` - Configuration parsing
- `pkg/utils/errors.c/h` - Error handling
- Documentation updates

**Acceptance Criteria**:
- [ ] Consistent error messages
- [ ] Useful debug logging
- [ ] Configuration via files/env vars
- [ ] Clean code without TODOs

---

### Testing for Week 5

```bash
# Test persistent volumes
kubectl apply -f pvc.yaml
kubectl apply -f pod-with-pvc.yaml
kubectl exec <pod> -- touch /data/test.txt
kubectl delete pod <pod>
kubectl apply -f pod-with-pvc.yaml
kubectl exec <pod> -- ls /data/           # Should see test.txt

# Test webhooks
kubectl apply -f webhook-config.yaml
kubectl apply -f pod-that-needs-mutation.yaml  # Should be mutated
kubectl apply -f pod-that-needs-validation.yaml  # Should be rejected

# Test quota
kubectl apply -f quota.yaml
kubectl apply -f pod.yaml         # Should succeed
kubectl apply -f pod2.yaml        # Might fail if over quota
kubectl describe resourcequota    # Should show usage
```

---

## Summary: Week 3-5 Deliverables

### Code Delivered

| Component | Files | LOC | Status |
|-----------|-------|-----|--------|
| Pod Lifecycle | 2 | 150 | ✓ |
| Kubelet Agent | 4 | 300 | ✓ |
| Networking (CNI, Service) | 3 | 200 | ✓ |
| Events System | 2 | 100 | ✓ |
| DaemonSet, StatefulSet, Job | 6 | 450 | ✓ |
| Advanced Scheduling | 3 | 300 | ✓ |
| QoS & Preemption | 2 | 200 | ✓ |
| Storage (PV, PVC) | 4 | 300 | ✓ |
| Webhooks (Mutating, Validating) | 2 | 300 | ✓ |
| Resource Quota | 2 | 150 | ✓ |
| Polish & Utils | 3 | 150 | ✓ |
| **Total** | **33** | **2700** | ✓ |

### Binaries

```
bin/sirah-apiserver    (35KB) - Full API server with all endpoints
bin/sirah-scheduler    (32KB) - Advanced scheduler with plugins
bin/sirah-controller   (32KB) - All controllers + reconciliation
bin/node-agent         (18KB) - Kubelet agent
```

### Documentation

- `WEEK3-5-IMPLEMENTATION.md` - Detailed implementation notes
- `API.md` - Complete API documentation
- `NETWORKING.md` - Networking architecture
- `STORAGE.md` - Storage system design
- Updated `README.md` with all features

### Test Coverage

- 30+ integration tests
- End-to-end pod lifecycle test
- Network connectivity test
- Storage mounting test
- Webhook invocation test
- Quota enforcement test

---

## Success Metrics

By end of Week 5:
- [ ] All core controllers implemented
- [ ] Advanced scheduling working
- [ ] Pod lifecycle complete
- [ ] Networking operational
- [ ] Storage integration done
- [ ] Webhooks functioning
- [ ] ~80% of MVP complete

---

## Next Phase (Week 6+)

- RBAC (role-based access control)
- TLS/certificate management
- High-availability control plane
- Performance optimization
- Security hardening
- Full Kubernetes conformance testing

---

**Status**: Planning ready  
**Target Start**: After Week 2 completion  
**Estimated Effort**: 100-120 hours  
**Team Size**: 1-2 engineers  

