#ifndef STATEFULSET_CONTROLLER_H
#define STATEFULSET_CONTROLLER_H

#include "types/storage.h"
#include "types/pod.h"

typedef struct {
    k8s_statefulset_t** statefulsets;
    int num_statefulsets;
    
    k8s_pod_t** managed_pods;
    int num_managed_pods;
} statefulset_controller_t;

// Create a new StatefulSet controller
statefulset_controller_t* statefulset_controller_new(void);

// Create a StatefulSet
// Returns 0 on success
int statefulset_create(statefulset_controller_t* controller, k8s_statefulset_t* sts);

// Get a StatefulSet by name and namespace
k8s_statefulset_t* statefulset_get(statefulset_controller_t* controller,
                                  const char* name, const char* namespace);

// List all StatefulSets in a namespace (namespace=NULL for all)
k8s_statefulset_t** statefulset_list(statefulset_controller_t* controller,
                                    const char* namespace, int* count);

// Delete a StatefulSet
// Returns 0 on success
int statefulset_delete(statefulset_controller_t* controller,
                      const char* name, const char* namespace);

// Reconcile StatefulSet - ensure correct number of ordered pods exist
// Called periodically by controller to maintain desired state
int statefulset_reconcile(statefulset_controller_t* controller, const char* sts_name);

// Create ordered pod for StatefulSet (pod-0, pod-1, etc)
// Returns pod name in pod_name_out (caller must free)
k8s_pod_t* statefulset_create_pod(const char* sts_name, const char* namespace,
                                 int ordinal, const char* service_name);

// Free the controller
void statefulset_controller_free(statefulset_controller_t* controller);

#endif // STATEFULSET_CONTROLLER_H
