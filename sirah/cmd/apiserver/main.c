// cmd/apiserver/main.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "etcd/etcd_manager.h"
#include "../internal/apiserver/scheduler_integration.h"

int api_server_init(int port);
int api_server_run(void);
int store_init_etcd(const char* etcd_addr);
void store_shutdown_etcd(void);
int store_restore_pods_from_etcd(void);

int main(int argc, char** argv) {
    int port = 6443;
    const char* etcd_addr = "http://localhost:2379";
    
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
    
    // Initialize etcd_manager singleton
    printf("\n=== Initializing etcd Manager ===\n");
    etcd_manager_init(etcd_addr, NULL);
    
    // Wait for etcd manager to be ready (up to 60 seconds)
    printf("Waiting for etcd manager to be ready...\n");
    if (etcd_manager_wait_for_ready(60) != 0) {
        fprintf(stderr, "Warning: etcd manager not ready after 60 seconds, continuing with limited functionality\n");
    } else {
        printf("✓ etcd manager is ready\n");
    }
    
    // Initialize persistent storage with etcd
    printf("\n=== Initializing Persistent Storage ===\n");
    if (store_init_etcd(etcd_addr) != 0) {
        fprintf(stderr, "Warning: Failed to connect to etcd, continuing with in-memory storage\n");
        // Continue anyway - system can work with in-memory storage
    } else {
        // Restore all pods from etcd
        printf("\n=== Restoring Data from etcd ===\n");
        int restored = store_restore_pods_from_etcd();
        printf("Restored %d pods from etcd\n\n", restored);
    }
    
    // Initialize scheduler integration
    // TEMPORARILY DISABLED: Scheduler integration is causing segfaults
    // printf("\n=== Initializing Scheduler ===\n");
    // if (scheduler_integration_init() != 0) {
    //     fprintf(stderr, "Warning: Failed to initialize scheduler\n");
    // } else {
    //     printf("✓ Scheduler integration initialized\n");
    // }
    
    if (api_server_init(port) != 0) {
        fprintf(stderr, "Failed to initialize API server\n");
        // scheduler_integration_stop();
        etcd_manager_shutdown();
        store_shutdown_etcd();
        return 1;
    }
    
    // Start scheduler control loop
    // TEMPORARILY DISABLED: Scheduler integration is causing segfaults
    // printf("\n=== Starting Scheduler Control Loop ===\n");
    // if (scheduler_integration_start() != 0) {
    //     fprintf(stderr, "Warning: Failed to start scheduler control loop\n");
    // } else {
    //     printf("✓ Scheduler control loop started\n");
    // }
    
    if (api_server_run() != 0) {
        fprintf(stderr, "API server error\n");
        // scheduler_integration_stop();
        etcd_manager_shutdown();
        store_shutdown_etcd();
        return 1;
    }

    scheduler_integration_stop();
    etcd_manager_shutdown();
    store_shutdown_etcd();
    return 0;
}
