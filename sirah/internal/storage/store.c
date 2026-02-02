// internal/storage/store.c
#include "store.h"
#include "etcd_client.h"
#include "../../pkg/types/pod.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Global pod store (in-memory for MVP)
// Used by apiserver to store pods and by controller to update pod status
pod_store_t pod_store = {0};

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
int store_init_etcd(const char* etcd_addr) {
    if (!etcd_addr) {
        fprintf(stderr, "etcd address required\n");
        return -1;
    }

    // Connect to etcd
    g_etcd_client = etcd_connect(etcd_addr);
    if (!g_etcd_client) {
        fprintf(stderr, "Failed to connect to etcd at %s\n", etcd_addr);
        return -1;
    }

    printf("Persistent storage initialized with etcd at %s\n", etcd_addr);
    return 0;
}

// Shutdown etcd connection
void store_shutdown_etcd(void) {
    if (g_etcd_client) {
        etcd_disconnect(g_etcd_client);
        g_etcd_client = NULL;
    }
}

// Save pod to etcd
int store_save_pod_to_etcd(k8s_pod_t* pod) {
    if (!g_etcd_client || !pod) {
        return -1;
    }
    return etcd_save_pod(g_etcd_client, pod);
}

// Restore all pods from etcd
int store_restore_pods_from_etcd(void) {
    if (!g_etcd_client) {
        fprintf(stderr, "etcd client not initialized\n");
        return 0;
    }
    return etcd_restore_all_pods(g_etcd_client);
}

// Delete pod from etcd
int store_delete_pod_from_etcd(const char* namespace, const char* name) {
    if (!g_etcd_client) {
        return -1;
    }
    return etcd_delete_pod(g_etcd_client, namespace, name);
}
