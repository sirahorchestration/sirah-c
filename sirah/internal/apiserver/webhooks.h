#ifndef K8S_WEBHOOKS_H
#define K8S_WEBHOOKS_H

#include <types/common.h>
#include <stdbool.h>

#define K8S_MAX_WEBHOOKS 50
#define K8S_MAX_WEBHOOK_RULES 20
#define K8S_MAX_API_GROUPS 10
#define K8S_MAX_API_VERSIONS 10
#define K8S_MAX_RESOURCES 10
#define K8S_MAX_OPERATIONS 10

// Webhook failure policy
typedef enum {
    K8S_FAILURE_POLICY_IGNORE,
    K8S_FAILURE_POLICY_FAIL
} k8s_webhook_failure_policy_t;

// Webhook side effects
typedef enum {
    K8S_SIDE_EFFECTS_NONE,
    K8S_SIDE_EFFECTS_SOME,
    K8S_SIDE_EFFECTS_UNKNOWN
} k8s_webhook_side_effects_t;

// Rules for when to invoke webhook
typedef struct {
    char* api_groups[K8S_MAX_API_GROUPS];
    int num_api_groups;
    
    char* api_versions[K8S_MAX_API_VERSIONS];
    int num_api_versions;
    
    char* resources[K8S_MAX_RESOURCES];
    int num_resources;
    
    char* operations[K8S_MAX_OPERATIONS];  // "CREATE", "UPDATE", "DELETE", "CONNECT"
    int num_operations;
    
    // Scope: "Cluster" or "Namespaced"
    char* scope;
} k8s_webhook_rule_t;

// Webhook client configuration
typedef struct {
    char* name;                         // e.g., "pod-validator.example.com"
    char* client_config_url;            // e.g., "https://webhook.example.com:8443/validate"
    int timeout_seconds;                // Max 30 seconds
    
    k8s_webhook_rule_t* rules[K8S_MAX_WEBHOOK_RULES];
    int num_rules;
    
    k8s_webhook_failure_policy_t failure_policy;
    k8s_webhook_side_effects_t side_effects;
    
    // Optional: CA bundle for TLS verification
    char* ca_bundle;
} k8s_webhook_config_entry_t;

// Validating Webhook Configuration
typedef struct {
    k8s_metadata_t* metadata;
    
    struct {
        k8s_webhook_config_entry_t* webhooks[K8S_MAX_WEBHOOKS];
        int num_webhooks;
    } webhooks;
} k8s_validating_webhook_config_t;

// Mutating Webhook Configuration
typedef struct {
    k8s_metadata_t* metadata;
    
    struct {
        k8s_webhook_config_entry_t* webhooks[K8S_MAX_WEBHOOKS];
        int num_webhooks;
    } webhooks;
} k8s_mutating_webhook_config_t;

// Webhook admission review (request from API server to webhook)
typedef struct {
    char* uid;                          // Unique ID for this request
    char* kind;                         // e.g., "Pod"
    char* api_version;                  // e.g., "v1"
    char* operation;                    // "CREATE", "UPDATE", "DELETE", "CONNECT"
    char* namespace;
    char* name;
    char* object_json;                  // Full object being evaluated
    char* old_object_json;              // Old object (for UPDATE)
    char* options_json;                 // CreateOptions/UpdateOptions/DeleteOptions
} k8s_webhook_request_t;

// Webhook admission review (response from webhook to API server)
typedef struct {
    bool allowed;                       // Whether operation is allowed
    int http_code;                      // HTTP status code
    char* message;                      // Human-readable explanation
    
    // For mutating webhooks
    char* patch_json;                   // JSONPatch operations
    char* patch_type;                   // "JSONPatch", "MergePatch", "StrategicMergePatch"
} k8s_webhook_response_t;

// ============ Lifecycle Functions ============

/**
 * Create new validating webhook configuration
 */
k8s_validating_webhook_config_t* k8s_validating_webhook_config_new(const char* name);

/**
 * Create new mutating webhook configuration
 */
k8s_mutating_webhook_config_t* k8s_mutating_webhook_config_new(const char* name);

/**
 * Free webhook configurations
 */
void k8s_validating_webhook_config_free(k8s_validating_webhook_config_t* config);
void k8s_mutating_webhook_config_free(k8s_mutating_webhook_config_t* config);

// ============ Webhook Configuration Management ============

/**
 * Add webhook to configuration
 */
int k8s_validating_webhook_config_add(k8s_validating_webhook_config_t* config, k8s_webhook_config_entry_t* entry);
int k8s_mutating_webhook_config_add(k8s_mutating_webhook_config_t* config, k8s_webhook_config_entry_t* entry);

/**
 * Serialization
 */
char* k8s_validating_webhook_config_to_json(k8s_validating_webhook_config_t* config);
char* k8s_mutating_webhook_config_to_json(k8s_mutating_webhook_config_t* config);

// ============ Webhook Request/Response ============

/**
 * Create webhook request
 */
k8s_webhook_request_t* k8s_webhook_request_new(const char* kind, const char* api_version, const char* operation);
void k8s_webhook_request_free(k8s_webhook_request_t* request);

/**
 * Create webhook response
 */
k8s_webhook_response_t* k8s_webhook_response_new();
void k8s_webhook_response_free(k8s_webhook_response_t* response);

/**
 * Serialize request to JSON for sending to webhook
 */
char* k8s_webhook_request_to_json(k8s_webhook_request_t* request);

/**
 * Parse response JSON from webhook
 */
k8s_webhook_response_t* k8s_webhook_response_from_json(const char* json);

#endif // K8S_WEBHOOKS_H
