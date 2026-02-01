#ifndef SIRAH_CONFIGMAP_H
#define SIRAH_CONFIGMAP_H

#include "common.h"

// ConfigMap object for storing non-confidential data
typedef struct {
    k8s_metadata_t metadata;
    
    // data: key-value pairs (strings)
    char** keys;
    char** values;
    int num_items;
} k8s_configmap_t;

// Secret object for storing sensitive data (base64 encoded)
typedef struct {
    k8s_metadata_t metadata;
    
    char* type;  // "Opaque", "kubernetes.io/basic-auth", etc.
    
    // data: key-value pairs (base64 encoded in real k8s)
    char** keys;
    char** values;
    int num_items;
} k8s_secret_t;

// ConfigMap operations
k8s_configmap_t* k8s_configmap_new(const char* name, const char* namespace);
void k8s_configmap_free(k8s_configmap_t* cm);
char* k8s_configmap_to_json(k8s_configmap_t* cm);
k8s_configmap_t* k8s_configmap_from_json(const char* json_str);
int k8s_configmap_set(k8s_configmap_t* cm, const char* key, const char* value);
const char* k8s_configmap_get(k8s_configmap_t* cm, const char* key);

// Secret operations
k8s_secret_t* k8s_secret_new(const char* name, const char* namespace, const char* type);
void k8s_secret_free(k8s_secret_t* secret);
char* k8s_secret_to_json(k8s_secret_t* secret);
k8s_secret_t* k8s_secret_from_json(const char* json_str);
int k8s_secret_set(k8s_secret_t* secret, const char* key, const char* value);
const char* k8s_secret_get(k8s_secret_t* secret, const char* key);

#endif
