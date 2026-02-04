// internal/storage/etcd_client.h
#ifndef SIRAH_STORAGE_ETCD_CLIENT_H
#define SIRAH_STORAGE_ETCD_CLIENT_H

#include "../../pkg/types/pod.h"

// etcd connection handle
typedef struct etcd_client etcd_client_t;

// Initialize etcd connection
// Returns NULL on failure, pointer to client on success
etcd_client_t* etcd_connect(const char* addr);

// Disconnect from etcd
void etcd_disconnect(etcd_client_t* client);

// Check if connected
int etcd_is_connected(etcd_client_t* client);

// Put key-value pair in etcd
// Returns 0 on success, non-zero on failure
int etcd_put(etcd_client_t* client, const char* key, const char* value);

// Get value from etcd
// Returns the value on success, NULL on failure
// Caller must free the returned string
char* etcd_get(etcd_client_t* client, const char* key);

// Delete key from etcd
// Returns 0 on success, non-zero on failure
int etcd_delete(etcd_client_t* client, const char* key);

// List all keys with given prefix
// Returns array of keys, last element is NULL
// Caller must free the returned array and strings
char** etcd_list(etcd_client_t* client, const char* prefix);

// Persistent pod operations
int etcd_save_pod(etcd_client_t* client, k8s_pod_t* pod);
k8s_pod_t* etcd_load_pod(etcd_client_t* client, const char* namespace, const char* name);
int etcd_delete_pod(etcd_client_t* client, const char* namespace, const char* name);

// Restore all pods from etcd
// Returns number of pods restored
int etcd_restore_all_pods(etcd_client_t* client);

#endif // SIRAH_STORAGE_ETCD_CLIENT_H
