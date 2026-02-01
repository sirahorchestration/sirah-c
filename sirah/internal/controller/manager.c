// internal/controller/manager.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include "manager.h"
#include "pod_controller.h"
#include "../runtime/runtime.h"

static const char* api_server_url = NULL;
static pthread_t deployment_tid = 0;
static pthread_t replicaset_tid = 0;
static pthread_t pod_tid = 0;

int deployment_controller_init(void);
int deployment_controller_run(void);
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

int controller_manager_init(const char* apiserver_url) {
    api_server_url = apiserver_url;
    printf("Controller Manager initialized with API Server: %s\n", apiserver_url);
    
    // Initialize runtime (QEMU)
    runtime_init(RUNTIME_QEMU);
    
    // Initialize all controllers
    deployment_controller_init();
    replicaset_controller_init();
    pod_controller_init(apiserver_url);
    
    return 0;
}

int controller_manager_run(void) {
    printf("Controller Manager running...\n");

    // Start controller threads
    pthread_create(&deployment_tid, NULL, deployment_thread, NULL);
    pthread_create(&replicaset_tid, NULL, replicaset_thread, NULL);
    pthread_create(&pod_tid, NULL, pod_thread, NULL);

    // Wait for threads
    pthread_join(deployment_tid, NULL);
    pthread_join(replicaset_tid, NULL);
    pthread_join(pod_tid, NULL);

    return 0;
}

void controller_manager_shutdown(void) {
    printf("Controller Manager shutdown\n");
    pod_controller_shutdown();
    runtime_cleanup();
}
