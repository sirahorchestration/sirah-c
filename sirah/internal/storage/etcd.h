#ifndef SIRAH_STORAGE_ETCD_H
#define SIRAH_STORAGE_ETCD_H

#include "store.h"

// etcd client initialization and management
int etcd_init(store_t* s, const char* addr);
int etcd_put(store_t* s, const char* key, const char* value);
char* etcd_get(store_t* s, const char* key);
int etcd_delete(store_t* s, const char* key);
int etcd_shutdown(store_t* s);

#endif
