#ifndef CONFIGMAP_CONTROLLER_H
#define CONFIGMAP_CONTROLLER_H

#include "types/config.h"

typedef struct {
    k8s_configmap_t** configmaps;
    int num_configmaps;
} configmap_controller_t;

// Create a new ConfigMap controller
configmap_controller_t* configmap_controller_new(void);

// Create a ConfigMap
int configmap_create(configmap_controller_t* controller, k8s_configmap_t* cm);

// Get a ConfigMap by name and namespace
k8s_configmap_t* configmap_get(configmap_controller_t* controller,
                              const char* name, const char* namespace);

// List all ConfigMaps in a namespace (namespace=NULL for all)
k8s_configmap_t** configmap_list(configmap_controller_t* controller,
                                const char* namespace, int* count);

// Update a ConfigMap
int configmap_update(configmap_controller_t* controller, k8s_configmap_t* cm);

// Delete a ConfigMap
int configmap_delete(configmap_controller_t* controller,
                    const char* name, const char* namespace);

// Free the controller
void configmap_controller_free(configmap_controller_t* controller);

#endif // CONFIGMAP_CONTROLLER_H
