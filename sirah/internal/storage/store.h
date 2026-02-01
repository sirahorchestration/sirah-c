#ifndef SIRAH_STORAGE_STORE_H
#define SIRAH_STORAGE_STORE_H

#include "../../pkg/types/pod.h"

typedef struct store {
    void* backend;
    int (*put)(void* backend, const char* key, const char* value);
    int (*get)(void* backend, const char* key, char** value);
    int (*delete)(void* backend, const char* key);
} store_t;

int store_put_pod(store_t* s, k8s_pod_t* pod);
k8s_pod_t* store_get_pod(store_t* s, const char* namespace, const char* name);
int store_delete_pod(store_t* s, const char* namespace, const char* name);

#endif // SIRAH_STORAGE_STORE_H
