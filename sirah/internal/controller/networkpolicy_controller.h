#ifndef K8S_NETWORKPOLICY_CONTROLLER_H
#define K8S_NETWORKPOLICY_CONTROLLER_H

#include <types/network_policy.h>
#include <stdbool.h>

// Controller for managing NetworkPolicy resources
typedef struct {
    k8s_network_policy_t** policies;
    int num_policies;
    int max_policies;
    bool enabled;
} k8s_networkpolicy_controller_t;

// ============ Controller Lifecycle ============

k8s_networkpolicy_controller_t* k8s_networkpolicy_controller_new();
void k8s_networkpolicy_controller_free(k8s_networkpolicy_controller_t* controller);

// ============ Policy Management ============

int k8s_networkpolicy_controller_create(k8s_networkpolicy_controller_t* controller,
                                        k8s_network_policy_t* policy);

int k8s_networkpolicy_controller_get(k8s_networkpolicy_controller_t* controller,
                                     const char* name,
                                     const char* namespace,
                                     k8s_network_policy_t** policy);

int k8s_networkpolicy_controller_update(k8s_networkpolicy_controller_t* controller,
                                        k8s_network_policy_t* policy);

int k8s_networkpolicy_controller_delete(k8s_networkpolicy_controller_t* controller,
                                        const char* name,
                                        const char* namespace);

int k8s_networkpolicy_controller_list(k8s_networkpolicy_controller_t* controller,
                                      const char* namespace,
                                      k8s_network_policy_t*** policies,
                                      int* count);

// ============ Validation ============

int k8s_networkpolicy_controller_validate(k8s_networkpolicy_controller_t* controller,
                                          k8s_network_policy_t* policy);

bool k8s_networkpolicy_controller_exists(k8s_networkpolicy_controller_t* controller,
                                         const char* name,
                                         const char* namespace);

// ============ Control ============

void k8s_networkpolicy_controller_set_enabled(k8s_networkpolicy_controller_t* controller, bool enabled);
bool k8s_networkpolicy_controller_is_enabled(k8s_networkpolicy_controller_t* controller);

// ============ Global Controller ============

k8s_networkpolicy_controller_t* k8s_networkpolicy_controller_global();

#endif
