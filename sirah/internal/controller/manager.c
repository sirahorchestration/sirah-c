// internal/controller/manager.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include "manager.h"
#include "pod_controller.h"
#include "service.h"
#include "network_controller.h"
#include "hpa_controller.h"
#include "node_lifecycle_controller.h"
#include "gc_controller.h"
#include "pod_eviction_controller.h"
#include "namespace_controller.h"
#include "config_propagation_controller.h"
#include "../runtime/runtime.h"

static const char* api_server_url = NULL;
static pthread_t deployment_tid = 0;
static pthread_t replicaset_tid = 0;
static pthread_t pod_tid = 0;
static pthread_t service_tid = 0;
static pthread_t network_tid = 0;
static pthread_t hpa_tid = 0;
static pthread_t node_lifecycle_tid = 0;
static pthread_t gc_tid = 0;
static pthread_t pod_eviction_tid = 0;
static pthread_t namespace_tid = 0;
static pthread_t config_prop_tid = 0;

// Controller functions
int deployment_controller_init(void);
int deployment_controller_run(void);
int deployment_controller_shutdown(void);
int replicaset_controller_init(void);
int replicaset_controller_run(void);

void* deployment_thread(void* arg) {
    (void)arg;
    int result = deployment_controller_run();
    return (result == 0) ? NULL : (void*)(intptr_t)-1;
}

void* replicaset_thread(void* arg) {
    (void)arg;
    int result = replicaset_controller_run();
    return (result == 0) ? NULL : (void*)(intptr_t)-1;
}

void* pod_thread(void* arg) {
    (void)arg;
    int result = pod_controller_run();
    return (result == 0) ? NULL : (void*)(intptr_t)-1;
}

void* service_thread(void* arg) {
    (void)arg;
    service_controller_t* controller = (service_controller_t*)arg;
    int result = service_controller_run(controller);
    return (result == 0) ? NULL : (void*)(intptr_t)-1;
}

void* network_thread(void* arg) {
    (void)arg;
    network_controller_t* controller = (network_controller_t*)arg;
    int result = network_controller_run(controller);
    return (result == 0) ? NULL : (void*)(intptr_t)-1;
}

// Phase 5 controller threads disabled - use API server controllers instead
/*
void* hpa_thread(void* arg) {
    (void)arg;
    int result = hpa_controller_run();
    return (result == 0) ? NULL : (void*)(intptr_t)-1;
}

void* node_lifecycle_thread(void* arg) {
    (void)arg;
    int result = node_lifecycle_controller_run();
    return (result == 0) ? NULL : (void*)(intptr_t)-1;
}

void* gc_thread(void* arg) {
    (void)arg;
    int result = gc_controller_run();
    return (result == 0) ? NULL : (void*)(intptr_t)-1;
}

void* pod_eviction_thread(void* arg) {
    (void)arg;
    int result = pod_eviction_controller_run();
    return (result == 0) ? NULL : (void*)(intptr_t)-1;
}

void* namespace_thread(void* arg) {
    (void)arg;
    int result = namespace_controller_run();
    return (result == 0) ? NULL : (void*)(intptr_t)-1;
}

void* config_propagation_thread(void* arg) {
    (void)arg;
    int result = config_propagation_controller_run();
    return (result == 0) ? NULL : (void*)(intptr_t)-1;
}
*/

int controller_manager_init(const char* apiserver_url) {
    api_server_url = apiserver_url;
    printf("Controller Manager initialized with API Server: %s\n", apiserver_url);
    
    // Initialize runtime (QEMU)
    runtime_init(RUNTIME_QEMU);
    
    // Initialize all Phase 1-4 controllers
    deployment_controller_init();
    replicaset_controller_init();
    pod_controller_init(apiserver_url);
    
    // Note: Phase 5 controllers deferred - use API server controllers.c instead
    // The API server now includes the Phase 5 controller coordinator (controllers.c)
    // which manages StatefulSets, Jobs, and other advanced workload types.
    // Controller manager focus: core workload and Pod lifecycle management
    
    printf("All controllers initialized\n");
    return 0;
}

int controller_manager_run(void) {
    printf("Controller Manager running...\n");

    // Start core controllers
    pthread_create(&deployment_tid, NULL, deployment_thread, NULL);
    printf("  Deployment controller started\n");
    
    pthread_create(&replicaset_tid, NULL, replicaset_thread, NULL);
    printf("  ReplicaSet controller started\n");
    
    pthread_create(&pod_tid, NULL, pod_thread, NULL);
    printf("  Pod controller started\n");
    
    // Create and start service controller
    service_controller_t* svc_controller = service_controller_new(api_server_url);
    if (svc_controller) {
        if (service_controller_init(svc_controller) == 0) {
            pthread_create(&service_tid, NULL, service_thread, (void*)svc_controller);
            printf("  Service controller started\n");
        } else {
            service_controller_free(svc_controller);
            printf("  Service controller failed to initialize\n");
        }
    }

    // Create and start network controller
    network_controller_t* net_controller = network_controller_new(api_server_url);
    if (net_controller) {
        if (network_controller_init(net_controller) == 0) {
            pthread_create(&network_tid, NULL, network_thread, (void*)net_controller);
            printf("  Network controller started\n");
        } else {
            network_controller_free(net_controller);
            printf("  Network controller failed to initialize\n");
        }
    }

    // Phase 5 controllers now managed by API server (controllers.c)
    // Do not start them here to avoid conflicts

    // Wait for threads (these typically run forever)
    pthread_join(deployment_tid, NULL);
    pthread_join(replicaset_tid, NULL);
    pthread_join(pod_tid, NULL);
    if (service_tid > 0) {
        pthread_join(service_tid, NULL);
    }
    if (network_tid > 0) {
        pthread_join(network_tid, NULL);
    }

    return 0;
}

void controller_manager_shutdown(void) {
    printf("Controller Manager shutdown\n");
    deployment_controller_shutdown();
    pod_controller_shutdown();
    runtime_cleanup();
}

