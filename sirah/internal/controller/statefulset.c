#include "statefulset.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

statefulset_controller_t* statefulset_controller_new(void) {
    statefulset_controller_t* controller = 
        (statefulset_controller_t*)malloc(sizeof(statefulset_controller_t));
    
    controller->statefulsets = (k8s_statefulset_t**)malloc(sizeof(k8s_statefulset_t*) * 1000);
    controller->num_statefulsets = 0;
    
    controller->managed_pods = (k8s_pod_t**)malloc(sizeof(k8s_pod_t*) * 10000);
    controller->num_managed_pods = 0;
    
    return controller;
}

int statefulset_create(statefulset_controller_t* controller, k8s_statefulset_t* sts) {
    if (!controller || !sts || controller->num_statefulsets >= 1000) return -1;
    
    // Check for duplicates
    for (int i = 0; i < controller->num_statefulsets; i++) {
        if (strcmp(controller->statefulsets[i]->metadata.name, sts->metadata.name) == 0 &&
            strcmp(controller->statefulsets[i]->metadata.namespace, sts->metadata.namespace) == 0) {
            return -1;  // Already exists
        }
    }
    
    controller->statefulsets[controller->num_statefulsets++] = sts;
    
    // Generate UID if not set
    if (strlen(sts->metadata.uid) == 0) {
        snprintf(sts->metadata.uid, 256, "sts-%ld", time(NULL));
    }
    
    return 0;
}

k8s_statefulset_t* statefulset_get(statefulset_controller_t* controller,
                                  const char* name, const char* namespace) {
    if (!controller || !name || !namespace) return NULL;
    
    for (int i = 0; i < controller->num_statefulsets; i++) {
        k8s_statefulset_t* sts = controller->statefulsets[i];
        if (strcmp(sts->metadata.name, name) == 0 &&
            strcmp(sts->metadata.namespace, namespace) == 0) {
            return sts;
        }
    }
    
    return NULL;
}

k8s_statefulset_t** statefulset_list(statefulset_controller_t* controller,
                                    const char* namespace, int* count) {
    if (!controller || !count) return NULL;
    
    k8s_statefulset_t** results = (k8s_statefulset_t**)malloc(sizeof(k8s_statefulset_t*) * 1000);
    *count = 0;
    
    for (int i = 0; i < controller->num_statefulsets; i++) {
        k8s_statefulset_t* sts = controller->statefulsets[i];
        if (namespace == NULL || strcmp(sts->metadata.namespace, namespace) == 0) {
            results[(*count)++] = sts;
        }
    }
    
    return results;
}

int statefulset_delete(statefulset_controller_t* controller,
                      const char* name, const char* namespace) {
    if (!controller || !name || !namespace) return -1;
    
    for (int i = 0; i < controller->num_statefulsets; i++) {
        k8s_statefulset_t* sts = controller->statefulsets[i];
        if (strcmp(sts->metadata.name, name) == 0 &&
            strcmp(sts->metadata.namespace, namespace) == 0) {
            
            // Delete all managed pods
            for (int p = 0; p < controller->num_managed_pods; p++) {
                k8s_pod_t* pod = controller->managed_pods[p];
                if (strcmp(pod->metadata.namespace, namespace) == 0 &&
                    strstr(pod->metadata.name, name) != NULL) {
                    
                    // Remove from managed pods
                    for (int j = p; j < controller->num_managed_pods - 1; j++) {
                        controller->managed_pods[j] = controller->managed_pods[j + 1];
                    }
                    controller->num_managed_pods--;
                    p--;
                    k8s_pod_free(pod);
                }
            }
            
            // Remove StatefulSet
            for (int j = i; j < controller->num_statefulsets - 1; j++) {
                controller->statefulsets[j] = controller->statefulsets[j + 1];
            }
            controller->num_statefulsets--;
            
            k8s_statefulset_free(sts);
            return 0;
        }
    }
    
    return -1;
}

k8s_pod_t* statefulset_create_pod(const char* sts_name, const char* namespace,
                                 int ordinal, const char* service_name) {
    if (!sts_name || !namespace || !service_name) return NULL;
    
    // Create pod name: sts_name-ordinal
    char pod_name[256];
    snprintf(pod_name, sizeof(pod_name), "%s-%d", sts_name, ordinal);
    
    // Create pod
    k8s_pod_t* pod = k8s_pod_new(pod_name, namespace);
    if (!pod) return NULL;
    
    // Add hostname for StatefulSet identity
    // Hostname would be: pod-0.service-name.namespace.svc.cluster.local
    // For now store in a label
    
    return pod;
}

int statefulset_reconcile(statefulset_controller_t* controller, const char* sts_name) {
    if (!controller || !sts_name) return -1;
    
    // Find the StatefulSet (need to search all namespaces)
    k8s_statefulset_t* sts = NULL;
    for (int i = 0; i < controller->num_statefulsets; i++) {
        if (strcmp(controller->statefulsets[i]->metadata.name, sts_name) == 0) {
            sts = controller->statefulsets[i];
            break;
        }
    }
    
    if (!sts) return -1;
    
    // Count existing managed pods for this StatefulSet
    int existing_pods = 0;
    for (int i = 0; i < controller->num_managed_pods; i++) {
        k8s_pod_t* pod = controller->managed_pods[i];
        if (strstr(pod->metadata.name, sts->metadata.name) != NULL &&
            strcmp(pod->metadata.namespace, sts->metadata.namespace) == 0) {
            existing_pods++;
        }
    }
    
    // Create missing pods
    while (existing_pods < sts->spec.replicas) {
        k8s_pod_t* pod = statefulset_create_pod(sts->metadata.name,
                                               sts->metadata.namespace,
                                               existing_pods,
                                               sts->spec.service_name);
        if (pod && controller->num_managed_pods < 10000) {
            controller->managed_pods[controller->num_managed_pods++] = pod;
            existing_pods++;
            sts->status.updated_replicas++;
        } else {
            break;
        }
    }
    
    // Delete extra pods (currently we don't do this for stable ordering)
    // In a real system, pods would be deleted in reverse order (pod-n, pod-n-1, etc)
    
    return 0;
}

void statefulset_controller_free(statefulset_controller_t* controller) {
    if (!controller) return;
    
    for (int i = 0; i < controller->num_statefulsets; i++) {
        k8s_statefulset_free(controller->statefulsets[i]);
    }
    free(controller->statefulsets);
    
    for (int i = 0; i < controller->num_managed_pods; i++) {
        k8s_pod_free(controller->managed_pods[i]);
    }
    free(controller->managed_pods);
    
    free(controller);
}
