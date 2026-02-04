// internal/storage/store.c
#include "store.h"
#include "../../pkg/types/pod.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// In-memory pod_store removed - all storage now goes through etcd

// Global etcd client for persistent storage
etcd_client_t* g_etcd_client = NULL;

int store_put_pod(store_t* s, k8s_pod_t* pod) {
    // TODO: Implement pod storage
    return 0;
}

k8s_pod_t* store_get_pod(store_t* s, const char* namespace, const char* name) {
    // TODO: Implement pod retrieval
    return NULL;
}

int store_delete_pod(store_t* s, const char* namespace, const char* name) {
    // TODO: Implement pod deletion
    return 0;
}

// Initialize etcd-backed persistent storage
// Now handled by etcd_manager singleton
int store_init_etcd(const char* etcd_addr) {
    if (!etcd_addr) {
        fprintf(stderr, "etcd address required\n");
        return -1;
    }

    // etcd_manager handles connection - just return success
    printf("Persistent storage initialized (via etcd_manager) at %s\n", etcd_addr);
    return 0;
}

// Shutdown etcd connection
// Now handled by etcd_manager singleton
void store_shutdown_etcd(void) {
    // etcd_manager handles shutdown
}

// Save pod to etcd
// Now handled by endpoints_etcd_integration
int store_save_pod_to_etcd(k8s_pod_t* pod) {
    if (!pod) {
        return -1;
    }
    // Pods are now saved directly by endpoint_create_pod_etcd()
    return 0;
}

// Restore all pods from etcd
// Now handled by endpoints via etcd_manager
int store_restore_pods_from_etcd(void) {
    // Pods are now loaded via etcd_manager queries
    return 0;
}

// Delete pod from etcd
// Now handled by endpoints_etcd_integration
int store_delete_pod_from_etcd(const char* namespace, const char* name) {
    if (!namespace || !name) {
        return -1;
    }
    // Pods are now deleted directly by endpoint_delete_pod_etcd()
    return 0;
}
