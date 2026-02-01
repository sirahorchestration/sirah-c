#include "configmap.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

configmap_controller_t* configmap_controller_new(void) {
    configmap_controller_t* controller =
        (configmap_controller_t*)malloc(sizeof(configmap_controller_t));
    
    controller->configmaps = (k8s_configmap_t**)malloc(sizeof(k8s_configmap_t*) * 10000);
    controller->num_configmaps = 0;
    
    return controller;
}

int configmap_create(configmap_controller_t* controller, k8s_configmap_t* cm) {
    if (!controller || !cm || controller->num_configmaps >= 10000) return -1;
    
    // Check for duplicates
    for (int i = 0; i < controller->num_configmaps; i++) {
        if (strcmp(controller->configmaps[i]->metadata.name, cm->metadata.name) == 0 &&
            strcmp(controller->configmaps[i]->metadata.namespace, cm->metadata.namespace) == 0) {
            return -1;  // Already exists
        }
    }
    
    // Set UID and timestamps if not set
    if (strlen(cm->metadata.uid) == 0) {
        snprintf(cm->metadata.uid, 256, "cm-%ld", time(NULL));
    }
    if (cm->metadata.creation_timestamp == 0) {
        cm->metadata.creation_timestamp = time(NULL);
    }
    
    controller->configmaps[controller->num_configmaps++] = cm;
    return 0;
}

k8s_configmap_t* configmap_get(configmap_controller_t* controller,
                              const char* name, const char* namespace) {
    if (!controller || !name || !namespace) return NULL;
    
    for (int i = 0; i < controller->num_configmaps; i++) {
        k8s_configmap_t* cm = controller->configmaps[i];
        if (strcmp(cm->metadata.name, name) == 0 &&
            strcmp(cm->metadata.namespace, namespace) == 0) {
            return cm;
        }
    }
    
    return NULL;
}

k8s_configmap_t** configmap_list(configmap_controller_t* controller,
                                const char* namespace, int* count) {
    if (!controller || !count) return NULL;
    
    k8s_configmap_t** results = (k8s_configmap_t**)malloc(sizeof(k8s_configmap_t*) * 10000);
    *count = 0;
    
    for (int i = 0; i < controller->num_configmaps; i++) {
        k8s_configmap_t* cm = controller->configmaps[i];
        if (namespace == NULL || strcmp(cm->metadata.namespace, namespace) == 0) {
            results[(*count)++] = cm;
        }
    }
    
    return results;
}

int configmap_update(configmap_controller_t* controller, k8s_configmap_t* cm) {
    if (!controller || !cm) return -1;
    
    k8s_configmap_t* existing = configmap_get(controller, cm->metadata.name,
                                              cm->metadata.namespace);
    if (!existing) return -1;
    
    // Update the resource version
    snprintf(existing->metadata.resource_version, 256, "%d",
             atoi(existing->metadata.resource_version) + 1);
    
    // Copy new data (in a real system we'd deep copy)
    existing->num_items = cm->num_items;
    for (int i = 0; i < cm->num_items; i++) {
        existing->keys[i] = cm->keys[i];
        existing->values[i] = cm->values[i];
    }
    
    return 0;
}

int configmap_delete(configmap_controller_t* controller,
                    const char* name, const char* namespace) {
    if (!controller || !name || !namespace) return -1;
    
    for (int i = 0; i < controller->num_configmaps; i++) {
        k8s_configmap_t* cm = controller->configmaps[i];
        if (strcmp(cm->metadata.name, name) == 0 &&
            strcmp(cm->metadata.namespace, namespace) == 0) {
            
            // Remove and free
            for (int j = i; j < controller->num_configmaps - 1; j++) {
                controller->configmaps[j] = controller->configmaps[j + 1];
            }
            controller->num_configmaps--;
            
            k8s_configmap_free(cm);
            return 0;
        }
    }
    
    return -1;
}

void configmap_controller_free(configmap_controller_t* controller) {
    if (!controller) return;
    
    for (int i = 0; i < controller->num_configmaps; i++) {
        k8s_configmap_free(controller->configmaps[i]);
    }
    free(controller->configmaps);
    free(controller);
}
