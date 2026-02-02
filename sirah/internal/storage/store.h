#ifndef SIRAH_STORAGE_STORE_H
#define SIRAH_STORAGE_STORE_H

#include "../../pkg/types/pod.h"

// Forward declare etcd client
typedef struct etcd_client etcd_client_t;

// Pod store structure definition
typedef struct {
    k8s_pod_t* pods[1000];
    int count;
} pod_store_t;

// External pod store for apiserver and controller access
extern pod_store_t pod_store;

// Global etcd client for persistent storage
extern etcd_client_t* g_etcd_client;

typedef struct store {
    void* backend;
    int (*put)(void* backend, const char* key, const char* value);
    int (*get)(void* backend, const char* key, char** value);
    int (*delete)(void* backend, const char* key);
} store_t;

int store_put_pod(store_t* s, k8s_pod_t* pod);
k8s_pod_t* store_get_pod(store_t* s, const char* namespace, const char* name);
int store_delete_pod(store_t* s, const char* namespace, const char* name);

// Initialize etcd-backed storage
int store_init_etcd(const char* etcd_addr);

// Shutdown etcd connection
void store_shutdown_etcd(void);

#endif // SIRAH_STORAGE_STORE_H
