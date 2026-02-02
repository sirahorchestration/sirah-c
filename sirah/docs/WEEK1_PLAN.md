# Sirah Week 1: Core Kubernetes Platform Implementation

## Goal

Implement the foundation of a complete Kubernetes platform in C that:
- **API Server** listens on port 6443 and handles Kubernetes REST API
- **Storage** uses etcd for object persistence
- **Scheduler** places pods on nodes
- **Controllers** manage object state (Deployments, Services)
- **kubectl compatibility** - `kubectl get nodes/pods` works without modification

## Week 1 Deliverables

- [ ] API Server binary listening on 6443
- [ ] etcd storage integration
- [ ] Kubernetes object types (Pod, Node, Deployment, Service)
- [ ] REST API endpoints (`/api/v1/pods`, `/api/v1/nodes`)
- [ ] Basic scheduler that places pods
- [ ] Deployment controller that creates pods
- [ ] kubectl integration (get nodes, get pods, describe, apply)
- [ ] Comprehensive test suite
- [ ] Documentation and examples

---

## Week 1 Tasks

### Task 1.1: Project Setup & API Server Foundation (Day 1)

**Objective**: Establish build system and HTTP server foundation

**Files to Create**:
```
sirah/
├── Makefile                      # Build system
├── .gitignore                   # Git ignore
├── pkg/types/
│   ├── pod.h                    # Pod type definition
│   ├── node.h                   # Node type definition
│   ├── deployment.h             # Deployment type definition
│   └── common.h                 # Common types & helpers
│
└── cmd/apiserver/
    └── main.c                   # API Server entry point
```

**Code Skeleton**:

```c
// pkg/types/common.h
#ifndef SIRAH_TYPES_H
#define SIRAH_TYPES_H

#include <time.h>
#include <uuid/uuid.h>

// Kubernetes object metadata
typedef struct {
    char* name;
    char* namespace;
    char* uid;
    char* resource_version;
    time_t creation_timestamp;
} k8s_metadata_t;

// Object status constants
typedef enum {
    PHASE_PENDING,
    PHASE_RUNNING,
    PHASE_SUCCEEDED,
    PHASE_FAILED,
    PHASE_UNKNOWN
} k8s_phase_t;

#endif
```

```c
// pkg/types/pod.h
#ifndef SIRAH_POD_H
#define SIRAH_POD_H

#include "common.h"

typedef struct {
    char* name;
    char* image;
    int cpu_millicores;
    int memory_mb;
} container_t;

typedef struct {
    k8s_metadata_t metadata;
    
    struct {
        container_t* containers;
        int num_containers;
        char* restart_policy;
    } spec;
    
    struct {
        k8s_phase_t phase;
        char* host_ip;
        char* pod_ip;
    } status;
} k8s_pod_t;

#endif
```

```c
// cmd/apiserver/main.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

int api_server_init(int port);
int api_server_run(void);

int main(int argc, char** argv) {
    int port = 6443;
    const char* etcd_addr = "localhost:2379";
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        }
        if (strcmp(argv[i], "--etcd") == 0 && i + 1 < argc) {
            etcd_addr = argv[++i];
        }
    }
    
    printf("Sirah API Server starting...\n");
    printf("  Listen: 0.0.0.0:%d\n", port);
    printf("  etcd: %s\n", etcd_addr);
    
    if (api_server_init(port) != 0) {
        fprintf(stderr, "Failed to initialize API server\n");
        return 1;
    }
    
    if (api_server_run() != 0) {
        fprintf(stderr, "API server error\n");
        return 1;
    }
    
    return 0;
}
```

**Makefile**:
```makefile
CC = gcc
CFLAGS = -Wall -O2 -fPIC -Iinternal -Ipkg
LDFLAGS = -lm -pthread -lcurl -ljson-c -lopenssl -lz

BINS = bin/sirah-apiserver bin/sirah-scheduler bin/sirah-controller-manager

all: $(BINS)

bin/sirah-apiserver: cmd/apiserver/main.o internal/apiserver/server.o \
                     internal/storage/etcd.o pkg/types/pod.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

bin/sirah-scheduler: cmd/scheduler/main.o internal/scheduler/scheduler.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

bin/sirah-controller-manager: cmd/controller/main.o internal/controller/*.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test:
	./tests/run-tests.sh

clean:
	find . -name '*.o' -delete
	rm -f $(BINS)

.PHONY: all test clean
```

**Completion Criteria**:
- Build system works and produces binaries
- API Server entry point compiles
- Type definitions for Pod, Node, Deployment defined

---

### Task 1.2: Storage Abstraction & etcd Integration (Day 2)

**Objective**: Implement data storage layer using etcd

**Files to Create**:
```
internal/storage/
├── store.h                  # Storage interface
├── store.c                  # Generic store implementation
├── etcd.c                   # etcd client wrapper
└── schema.c                 # Object marshaling
```

**Code**:

```c
// internal/storage/store.h
#ifndef SIRAH_STORE_H
#define SIRAH_STORE_H

typedef struct store_t store_t;

// Storage interface
typedef struct {
    int (*init)(store_t* s, const char* addr);
    int (*put)(store_t* s, const char* key, const char* value);
    char* (*get)(store_t* s, const char* key);
    int (*delete)(store_t* s, const char* key);
    int (*watch)(store_t* s, const char* key_prefix);
} store_ops_t;

struct store_t {
    store_ops_t ops;
    void* context;  // etcd client handle
};

int store_put_pod(store_t* s, k8s_pod_t* pod);
k8s_pod_t* store_get_pod(store_t* s, const char* namespace, const char* name);
int store_delete_pod(store_t* s, const char* namespace, const char* name);

#endif
```

```c
// internal/storage/etcd.c
#include "store.h"
#include <curl/curl.h>
#include <json-c/json.h>

int etcd_init(store_t* s, const char* addr) {
    // Initialize etcd client
    CURL* curl = curl_easy_init();
    if (!curl) return -1;
    
    s->context = curl;
    s->ops.put = etcd_put;
    s->ops.get = etcd_get;
    s->ops.delete = etcd_delete;
    
    return 0;
}

int etcd_put(store_t* s, const char* key, const char* value) {
    // PUT to /v3/kv/put endpoint
    CURL* curl = (CURL*)s->context;
    
    struct json_object* req = json_object_new_object();
    json_object_object_add(req, "key", 
        json_object_new_string(key));
    json_object_object_add(req, "value",
        json_object_new_string(value));
    
    // Execute request
    // ...
    return 0;
}

int etcd_get(store_t* s, const char* key, char* output_buffer) {
    // GET from /v3/kv/range endpoint
    // Return value in output_buffer
    return 0;
}
```

**Completion Criteria**:
- etcd client connects and authenticates
- Put/Get/Delete operations work
- Object serialization (JSON) works
- Can store and retrieve Pod objects

---

### Task 1.3: Kubernetes Object Types & Validation (Day 2-3)

**Objective**: Define core Kubernetes types and validation

**Files to Create**:
```
pkg/types/
├── deployment.h
├── service.h
├── node.h
└── validation.c           # Type validation
```

**Code**:

```c
// pkg/types/deployment.h
#ifndef SIRAH_DEPLOYMENT_H
#define SIRAH_DEPLOYMENT_H

#include "common.h"
#include "pod.h"

typedef struct {
    k8s_metadata_t metadata;
    
    struct {
        int replicas;
        char* selector;        // Label selector (JSON)
        pod_spec_t pod_template;
    } spec;
    
    struct {
        int replicas;
        int updated_replicas;
        int ready_replicas;
    } status;
} k8s_deployment_t;

int deployment_create(k8s_deployment_t* dep);
int deployment_delete(const char* namespace, const char* name);
int deployment_update_replicas(const char* namespace, const char* name, int count);

#endif
```

**Completion Criteria**:
- All core types defined (Pod, Deployment, Service, Node)
- Validation functions for each type
- Type serialization/deserialization working

---

### Task 1.4: API Server REST Endpoints (Day 3-4)

**Objective**: Implement Kubernetes REST API endpoints

**Files to Create**:
```
internal/apiserver/
├── server.c              # HTTP server setup
├── handler.c             # Request routing
├── endpoints.c           # Individual endpoints
└── auth.c                # Authentication/authorization
```

**Key Endpoints**:
- `GET /api/v1/pods` - List pods
- `GET /api/v1/pods/{name}` - Get pod
- `POST /api/v1/pods` - Create pod
- `DELETE /api/v1/pods/{name}` - Delete pod
- `GET /api/v1/nodes` - List nodes
- `GET /api/v1/deployments` - List deployments

**Code**:

```c
// internal/apiserver/server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <microhttpd.h>
#include "../../pkg/types/common.h"

struct MHD_Daemon *daemon;

int api_handler(void* cls,
                struct MHD_Connection* connection,
                const char* url,
                const char* method,
                const char* version,
                const char* upload_data,
                size_t* upload_data_size,
                void** con_cls) {
    
    // Route requests based on method and URL
    if (strcmp(method, "GET") == 0) {
        if (strncmp(url, "/api/v1/pods", 12) == 0) {
            return handle_list_pods(connection, url);
        }
        if (strncmp(url, "/api/v1/nodes", 13) == 0) {
            return handle_list_nodes(connection, url);
        }
    }
    
    if (strcmp(method, "POST") == 0) {
        if (strncmp(url, "/api/v1/pods", 12) == 0) {
            return handle_create_pod(connection, upload_data);
        }
    }
    
    return MHD_HTTP_NOT_FOUND;
}

int api_server_init(int port) {
    daemon = MHD_start_daemon(
        MHD_USE_THREAD_PER_CONNECTION,
        port,
        NULL, NULL,
        &api_handler, NULL,
        MHD_OPTION_END);
    
    return daemon ? 0 : -1;
}

int api_server_run(void) {
    while (1) {
        sleep(1);
    }
    return 0;
}
```

**Completion Criteria**:
- HTTP server listening on 6443
- All key endpoints returning proper Kubernetes responses
- JSON serialization of objects in responses
- Request validation

---

### Task 1.5: Scheduler Foundation (Day 4)

**Objective**: Implement basic scheduler

**Files to Create**:
```
internal/scheduler/
├── scheduler.c           # Main scheduler loop
├── plugins.c             # Filter & scoring plugins
└── queue.c               # Pod queue management
```

**Code**:

```c
// internal/scheduler/scheduler.c
#include "../../pkg/types/common.h"
#include "../../pkg/types/pod.h"

typedef struct {
    k8s_pod_t* pending_pods[1000];
    int num_pending;
} scheduler_queue_t;

int scheduler_run(store_t* storage, const char* apiserver_url) {
    scheduler_queue_t queue = {0};
    
    while (1) {
        // 1. Fetch all pending pods from etcd
        // 2. For each pending pod:
        //    a. Get list of nodes
        //    b. Filter nodes (running, resources available)
        //    c. Score remaining nodes
        //    d. Bind pod to best scoring node
        //    e. Update pod status in etcd
        
        sleep(1);
    }
    
    return 0;
}

int filter_nodes(node_t* nodes, int num_nodes, pod_t* pod,
                 node_t* suitable[], int* num_suitable) {
    // Filter nodes that can run this pod
    // Check: resources, taints, node selectors
    return 0;
}

int score_nodes(node_t* suitable[], int num_suitable, pod_t* pod,
                int* scores) {
    // Score nodes for pod placement
    // Criteria: resource utilization, pod count, affinity rules
    return 0;
}

int bind_pod_to_node(store_t* storage, pod_t* pod, node_t* node) {
    // Update pod.spec.nodeName and status.hostIP
    // Save to storage
    return 0;
}
```

**Completion Criteria**:
- Scheduler binary compiles and runs
- Reads pending pods from etcd
- Filters and scores nodes
- Successfully binds pods to nodes

---

### Task 1.6: Deployment Controller (Day 5)

**Objective**: Implement deployment controller

**Files to Create**:
```
internal/controller/
├── deployment.c          # Deployment controller
├── replicaset.c          # ReplicaSet controller
└── manager.c             # Controller manager main loop
```

**Code**:

```c
// internal/controller/deployment.c
#include "../../pkg/types/deployment.h"
#include "../../internal/storage/store.h"

int deployment_controller_run(store_t* storage) {
    while (1) {
        // 1. Fetch all deployments from etcd
        // 2. For each deployment:
        //    a. Check desired vs actual replica count
        //    b. Create/delete pods as needed
        //    c. Update deployment status in etcd
        
        sleep(5);
    }
    return 0;
}

int reconcile_deployment(store_t* storage, k8s_deployment_t* dep) {
    // Get current pods matching selector
    int current_count = get_pod_count_for_deployment(storage, dep);
    
    if (current_count < dep->spec.replicas) {
        // Create missing pods
        int to_create = dep->spec.replicas - current_count;
        for (int i = 0; i < to_create; i++) {
            k8s_pod_t* pod = create_pod_from_template(dep);
            store_put_pod(storage, pod);
        }
    } else if (current_count > dep->spec.replicas) {
        // Delete excess pods
        // ...
    }
    
    // Update deployment status
    dep->status.replicas = dep->spec.replicas;
    store_put_deployment(storage, dep);
    
    return 0;
}
```

**Completion Criteria**:
- Controller watches deployments
- Creates pods when replicas < desired
- Deletes pods when replicas > desired
- Updates deployment status

---

### Task 1.7: Integration & Testing (Day 6-7)

**Objective**: Wire components together and test with kubectl

**Files to Create**:
```
tests/
├── test_api_server.c     # API server tests
├── test_scheduler.c      # Scheduler tests
├── test_controller.c     # Controller tests
└── integration_test.sh   # End-to-end tests
```

**Test Cases**:
```bash
# Test 1: API Server responds
kubectl --server=https://localhost:6443 get nodes
# Should list nodes

# Test 2: Create a pod via kubectl
kubectl apply -f - <<EOF
apiVersion: v1
kind: Pod
metadata:
  name: test-pod
  namespace: default
spec:
  containers:
  - name: busybox
    image: busybox
    command: ["sleep", "3600"]
EOF

# Test 3: Pod gets scheduled
kubectl get pods
# Should show test-pod in Running state

# Test 4: Create a deployment
kubectl apply -f - <<EOF
apiVersion: apps/v1
kind: Deployment
metadata:
  name: test-deployment
spec:
  replicas: 3
  selector:
    matchLabels:
      app: test
  template:
    metadata:
      labels:
        app: test
    spec:
      containers:
      - name: app
        image: busybox
EOF

# Test 5: Deployment creates pods
kubectl get pods -l app=test
# Should show 3 pods

# Test 6: Scale deployment
kubectl scale deployment test-deployment --replicas=5
kubectl get pods -l app=test
# Should show 5 pods
```

**Completion Criteria**:
- All components working together
- kubectl can connect without errors
- Pods can be created and scheduled
- Deployments create replica pods
- All tests passing

---

## Week 1 Architecture

```
┌─────────────────────────────────────────────────┐
│    Kubernetes CLI Tools (kubectl, Helm)         │
└────────────────┬────────────────────────────────┘
                 │ HTTPS/REST API
┌────────────────▼────────────────────────────────┐
│         API Server (6443)                       │
│  ├─ /api/v1/pods                               │
│  ├─ /api/v1/nodes                              │
│  ├─ /api/v1/deployments                        │
│  └─ /api/v1/services                           │
└────────────────┬────────────────────────────────┘
                 │
         ┌───────┴────────┬──────────────┐
         │                │              │
┌────────▼──────┐  ┌──────▼──────┐  ┌───▼─────────┐
│ Scheduler     │  │ Controllers │  │ etcd Store  │
│               │  │             │  │             │
│ - Pod queue   │  │ - Deployment│  │ - Objects   │
│ - Filtering   │  │ - ReplicaSet│  │ - Watch API │
│ - Scoring     │  │ - Service   │  │ - Revision  │
│ - Binding     │  │ - Node      │  │             │
└───────────────┘  └─────────────┘  └─────────────┘
```

## Success Metrics

- ✅ `kubectl --server=https://localhost:6443 get nodes` works
- ✅ `kubectl --server=https://localhost:6443 get pods` works
- ✅ `kubectl apply -f pod.yaml` creates pod successfully
- ✅ `kubectl apply -f deployment.yaml` creates replica pods
- ✅ Pods get scheduled to nodes automatically
- ✅ Scaling deployments works
- ✅ Logs and describe work with basic info
- ✅ All tests passing (100+ test cases)

## Code Metrics

- **Target Lines of Code**: ~5,000-7,000 lines of C
- **Binary Size**: <15MB per component
- **Memory Usage**: <100MB for API server + controllers
- **Response Time**: <100ms per API request

## Next Steps (Week 2+)

- **Multiple nodes** support
- **Service networking** (ClusterIP, NodePort, LoadBalancer)
- **ConfigMaps and Secrets**
- **Persistent volumes**
- **Stateful sets**
- **DaemonSets**
- **Jobs and CronJobs**
- **RBAC and admission control**
- **Operators and CRDs**

---

**Status**: Week 1 planning complete  
**Next**: Start Task 1.1 - Project Setup & API Server Foundation
