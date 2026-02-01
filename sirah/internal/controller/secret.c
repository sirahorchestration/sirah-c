#include "secret.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

secret_controller_t* secret_controller_new(void) {
    secret_controller_t* controller =
        (secret_controller_t*)malloc(sizeof(secret_controller_t));
    
    controller->secrets = (k8s_secret_t**)malloc(sizeof(k8s_secret_t*) * 10000);
    controller->num_secrets = 0;
    
    return controller;
}

int secret_create(secret_controller_t* controller, k8s_secret_t* secret) {
    if (!controller || !secret || controller->num_secrets >= 10000) return -1;
    
    // Check for duplicates
    for (int i = 0; i < controller->num_secrets; i++) {
        if (strcmp(controller->secrets[i]->metadata.name, secret->metadata.name) == 0 &&
            strcmp(controller->secrets[i]->metadata.namespace, secret->metadata.namespace) == 0) {
            return -1;  // Already exists
        }
    }
    
    // Set UID and timestamps if not set
    if (strlen(secret->metadata.uid) == 0) {
        snprintf(secret->metadata.uid, 256, "secret-%ld", time(NULL));
    }
    if (secret->metadata.creation_timestamp == 0) {
        secret->metadata.creation_timestamp = time(NULL);
    }
    
    controller->secrets[controller->num_secrets++] = secret;
    return 0;
}

k8s_secret_t* secret_get(secret_controller_t* controller,
                        const char* name, const char* namespace) {
    if (!controller || !name || !namespace) return NULL;
    
    for (int i = 0; i < controller->num_secrets; i++) {
        k8s_secret_t* secret = controller->secrets[i];
        if (strcmp(secret->metadata.name, name) == 0 &&
            strcmp(secret->metadata.namespace, namespace) == 0) {
            return secret;
        }
    }
    
    return NULL;
}

k8s_secret_t** secret_list(secret_controller_t* controller,
                          const char* namespace, int* count) {
    if (!controller || !count) return NULL;
    
    k8s_secret_t** results = (k8s_secret_t**)malloc(sizeof(k8s_secret_t*) * 10000);
    *count = 0;
    
    for (int i = 0; i < controller->num_secrets; i++) {
        k8s_secret_t* secret = controller->secrets[i];
        if (namespace == NULL || strcmp(secret->metadata.namespace, namespace) == 0) {
            results[(*count)++] = secret;
        }
    }
    
    return results;
}

int secret_update(secret_controller_t* controller, k8s_secret_t* secret) {
    if (!controller || !secret) return -1;
    
    k8s_secret_t* existing = secret_get(controller, secret->metadata.name,
                                        secret->metadata.namespace);
    if (!existing) return -1;
    
    // Update the resource version
    snprintf(existing->metadata.resource_version, 256, "%d",
             atoi(existing->metadata.resource_version) + 1);
    
    // Copy new data
    existing->num_items = secret->num_items;
    for (int i = 0; i < secret->num_items; i++) {
        existing->keys[i] = secret->keys[i];
        existing->values[i] = secret->values[i];
    }
    
    return 0;
}

int secret_delete(secret_controller_t* controller,
                 const char* name, const char* namespace) {
    if (!controller || !name || !namespace) return -1;
    
    for (int i = 0; i < controller->num_secrets; i++) {
        k8s_secret_t* secret = controller->secrets[i];
        if (strcmp(secret->metadata.name, name) == 0 &&
            strcmp(secret->metadata.namespace, namespace) == 0) {
            
            // Remove and free
            for (int j = i; j < controller->num_secrets - 1; j++) {
                controller->secrets[j] = controller->secrets[j + 1];
            }
            controller->num_secrets--;
            
            k8s_secret_free(secret);
            return 0;
        }
    }
    
    return -1;
}

void secret_controller_free(secret_controller_t* controller) {
    if (!controller) return;
    
    for (int i = 0; i < controller->num_secrets; i++) {
        k8s_secret_free(controller->secrets[i]);
    }
    free(controller->secrets);
    free(controller);
}
