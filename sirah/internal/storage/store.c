// internal/storage/store.c
#include "store.h"
#include <stdlib.h>
#include <string.h>

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
