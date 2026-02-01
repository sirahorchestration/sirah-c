#ifndef K8S_ADMISSION_H
#define K8S_ADMISSION_H

#include "webhooks.h"
#include <stdbool.h>

#define K8S_MAX_MUTATING_WEBHOOKS 20
#define K8S_MAX_VALIDATING_WEBHOOKS 20

// Admission controller - manages webhook chain
typedef struct {
    k8s_mutating_webhook_config_t* mutating_configs[K8S_MAX_MUTATING_WEBHOOKS];
    int num_mutating;
    
    k8s_validating_webhook_config_t* validating_configs[K8S_MAX_VALIDATING_WEBHOOKS];
    int num_validating;
    
    bool enabled;
} k8s_admission_controller_t;

// ============ Initialization ============

/**
 * Create new admission controller
 */
k8s_admission_controller_t* k8s_admission_controller_new();

/**
 * Free admission controller
 */
void k8s_admission_controller_free(k8s_admission_controller_t* controller);

// ============ Webhook Registration ============

/**
 * Register a mutating webhook configuration
 */
int k8s_admission_register_mutating_webhook(k8s_admission_controller_t* controller, 
                                            k8s_mutating_webhook_config_t* config);

/**
 * Register a validating webhook configuration
 */
int k8s_admission_register_validating_webhook(k8s_admission_controller_t* controller,
                                              k8s_validating_webhook_config_t* config);

/**
 * Unregister a mutating webhook configuration
 */
int k8s_admission_unregister_mutating_webhook(k8s_admission_controller_t* controller, const char* config_name);

/**
 * Unregister a validating webhook configuration
 */
int k8s_admission_unregister_validating_webhook(k8s_admission_controller_t* controller, const char* config_name);

// ============ Admission Flow ============

/**
 * Apply all mutating webhooks to an object
 * Returns modified JSON in patched_json buffer
 * Returns 0 on success, -1 on failure
 */
int k8s_admission_mutate(k8s_admission_controller_t* controller,
                         const char* kind,
                         const char* api_version,
                         const char* operation,
                         const char* namespace,
                         const char* name,
                         const char* object_json,
                         char* patched_json,
                         size_t patched_size);

/**
 * Apply all validating webhooks to an object
 * Returns 0 if allowed, -1 if denied
 */
int k8s_admission_validate(k8s_admission_controller_t* controller,
                           const char* kind,
                           const char* api_version,
                           const char* operation,
                           const char* namespace,
                           const char* name,
                           const char* object_json,
                           char* error_message,
                           size_t error_size);

// ============ Control ============

/**
 * Enable/disable admission control
 */
void k8s_admission_set_enabled(k8s_admission_controller_t* controller, bool enabled);

/**
 * Check if admission control is enabled
 */
bool k8s_admission_is_enabled(k8s_admission_controller_t* controller);

// ============ Global Controller ============

/**
 * Get global admission controller instance
 */
k8s_admission_controller_t* k8s_admission_controller_global();

#endif // K8S_ADMISSION_H
