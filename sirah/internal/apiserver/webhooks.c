#include "webhooks.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============ Helper Functions ============

static int add_string_to_array(char* array[], int* count, int max, const char* str) {
    if (!str || !array || !count) return -1;
    if (*count >= max) return -1;
    array[*count] = strdup(str);
    if (!array[*count]) return -1;
    (*count)++;
    return 0;
}

// ============ Webhook Rule Functions ============

static k8s_webhook_rule_t* k8s_webhook_rule_new() {
    return calloc(1, sizeof(k8s_webhook_rule_t));
}

static void k8s_webhook_rule_free(k8s_webhook_rule_t* rule) {
    if (!rule) return;
    
    for (int i = 0; i < rule->num_api_groups; i++) {
        if (rule->api_groups[i]) free(rule->api_groups[i]);
    }
    for (int i = 0; i < rule->num_api_versions; i++) {
        if (rule->api_versions[i]) free(rule->api_versions[i]);
    }
    for (int i = 0; i < rule->num_resources; i++) {
        if (rule->resources[i]) free(rule->resources[i]);
    }
    for (int i = 0; i < rule->num_operations; i++) {
        if (rule->operations[i]) free(rule->operations[i]);
    }
    if (rule->scope) free(rule->scope);
    
    free(rule);
}

// ============ Webhook Config Entry Functions ============

static k8s_webhook_config_entry_t* k8s_webhook_config_entry_new(const char* name, const char* url) {
    if (!name || !url) return NULL;
    
    k8s_webhook_config_entry_t* entry = calloc(1, sizeof(k8s_webhook_config_entry_t));
    if (!entry) return NULL;
    
    entry->name = strdup(name);
    entry->client_config_url = strdup(url);
    entry->timeout_seconds = 30;
    entry->failure_policy = K8S_FAILURE_POLICY_FAIL;
    entry->side_effects = K8S_SIDE_EFFECTS_UNKNOWN;
    
    return entry;
}

static void k8s_webhook_config_entry_free(k8s_webhook_config_entry_t* entry) {
    if (!entry) return;
    
    if (entry->name) free(entry->name);
    if (entry->client_config_url) free(entry->client_config_url);
    if (entry->ca_bundle) free(entry->ca_bundle);
    
    for (int i = 0; i < entry->num_rules; i++) {
        if (entry->rules[i]) {
            k8s_webhook_rule_free(entry->rules[i]);
        }
    }
    
    free(entry);
}

// ============ Validating Webhook Configuration ============

k8s_validating_webhook_config_t* k8s_validating_webhook_config_new(const char* name) {
    if (!name) return NULL;
    
    k8s_validating_webhook_config_t* config = calloc(1, sizeof(k8s_validating_webhook_config_t));
    if (!config) return NULL;
    
    config->metadata = k8s_metadata_new(name, "");
    if (!config->metadata) {
        free(config);
        return NULL;
    }
    
    return config;
}

void k8s_validating_webhook_config_free(k8s_validating_webhook_config_t* config) {
    if (!config) return;
    
    if (config->metadata) k8s_metadata_free(config->metadata);
    
    for (int i = 0; i < config->webhooks.num_webhooks; i++) {
        if (config->webhooks.webhooks[i]) {
            k8s_webhook_config_entry_free(config->webhooks.webhooks[i]);
        }
    }
    
    free(config);
}

int k8s_validating_webhook_config_add(k8s_validating_webhook_config_t* config, k8s_webhook_config_entry_t* entry) {
    if (!config || !entry) return -1;
    if (config->webhooks.num_webhooks >= K8S_MAX_WEBHOOKS) return -1;
    
    config->webhooks.webhooks[config->webhooks.num_webhooks] = entry;
    config->webhooks.num_webhooks++;
    
    return 0;
}

char* k8s_validating_webhook_config_to_json(k8s_validating_webhook_config_t* config) {
    if (!config || !config->metadata) return NULL;
    
    static char json[16384];
    int offset = 0;
    
    offset += snprintf(json + offset, sizeof(json) - offset,
        "{\"apiVersion\":\"admissionregistration.k8s.io/v1\","
        "\"kind\":\"ValidatingWebhookConfiguration\","
        "\"metadata\":{\"name\":\"%s\"},",
        config->metadata->name ? config->metadata->name : "");
    
    offset += snprintf(json + offset, sizeof(json) - offset, "\"webhooks\":[");
    
    for (int i = 0; i < config->webhooks.num_webhooks; i++) {
        if (i > 0) offset += snprintf(json + offset, sizeof(json) - offset, ",");
        
        k8s_webhook_config_entry_t* entry = config->webhooks.webhooks[i];
        offset += snprintf(json + offset, sizeof(json) - offset,
            "{\"name\":\"%s\","
            "\"clientConfig\":{\"url\":\"%s\",\"timeoutSeconds\":%d},"
            "\"failurePolicy\":\"%s\","
            "\"sideEffects\":\"%s\"}",
            entry->name ? entry->name : "",
            entry->client_config_url ? entry->client_config_url : "",
            entry->timeout_seconds,
            entry->failure_policy == K8S_FAILURE_POLICY_FAIL ? "Fail" : "Ignore",
            entry->side_effects == K8S_SIDE_EFFECTS_NONE ? "None" : 
            entry->side_effects == K8S_SIDE_EFFECTS_SOME ? "Some" : "Unknown");
    }
    
    offset += snprintf(json + offset, sizeof(json) - offset, "]}");
    
    return json;
}

// ============ Mutating Webhook Configuration ============

k8s_mutating_webhook_config_t* k8s_mutating_webhook_config_new(const char* name) {
    if (!name) return NULL;
    
    k8s_mutating_webhook_config_t* config = calloc(1, sizeof(k8s_mutating_webhook_config_t));
    if (!config) return NULL;
    
    config->metadata = k8s_metadata_new(name, "");
    if (!config->metadata) {
        free(config);
        return NULL;
    }
    
    return config;
}

void k8s_mutating_webhook_config_free(k8s_mutating_webhook_config_t* config) {
    if (!config) return;
    
    if (config->metadata) k8s_metadata_free(config->metadata);
    
    for (int i = 0; i < config->webhooks.num_webhooks; i++) {
        if (config->webhooks.webhooks[i]) {
            k8s_webhook_config_entry_free(config->webhooks.webhooks[i]);
        }
    }
    
    free(config);
}

int k8s_mutating_webhook_config_add(k8s_mutating_webhook_config_t* config, k8s_webhook_config_entry_t* entry) {
    if (!config || !entry) return -1;
    if (config->webhooks.num_webhooks >= K8S_MAX_WEBHOOKS) return -1;
    
    config->webhooks.webhooks[config->webhooks.num_webhooks] = entry;
    config->webhooks.num_webhooks++;
    
    return 0;
}

char* k8s_mutating_webhook_config_to_json(k8s_mutating_webhook_config_t* config) {
    if (!config || !config->metadata) return NULL;
    
    static char json[16384];
    int offset = 0;
    
    offset += snprintf(json + offset, sizeof(json) - offset,
        "{\"apiVersion\":\"admissionregistration.k8s.io/v1\","
        "\"kind\":\"MutatingWebhookConfiguration\","
        "\"metadata\":{\"name\":\"%s\"},",
        config->metadata->name ? config->metadata->name : "");
    
    offset += snprintf(json + offset, sizeof(json) - offset, "\"webhooks\":[");
    
    for (int i = 0; i < config->webhooks.num_webhooks; i++) {
        if (i > 0) offset += snprintf(json + offset, sizeof(json) - offset, ",");
        
        k8s_webhook_config_entry_t* entry = config->webhooks.webhooks[i];
        offset += snprintf(json + offset, sizeof(json) - offset,
            "{\"name\":\"%s\","
            "\"clientConfig\":{\"url\":\"%s\",\"timeoutSeconds\":%d},"
            "\"failurePolicy\":\"%s\","
            "\"sideEffects\":\"%s\"}",
            entry->name ? entry->name : "",
            entry->client_config_url ? entry->client_config_url : "",
            entry->timeout_seconds,
            entry->failure_policy == K8S_FAILURE_POLICY_FAIL ? "Fail" : "Ignore",
            entry->side_effects == K8S_SIDE_EFFECTS_NONE ? "None" : 
            entry->side_effects == K8S_SIDE_EFFECTS_SOME ? "Some" : "Unknown");
    }
    
    offset += snprintf(json + offset, sizeof(json) - offset, "]}");
    
    return json;
}

// ============ Webhook Request/Response Functions ============

k8s_webhook_request_t* k8s_webhook_request_new(const char* kind, const char* api_version, const char* operation) {
    if (!kind || !api_version || !operation) return NULL;
    
    k8s_webhook_request_t* request = calloc(1, sizeof(k8s_webhook_request_t));
    if (!request) return NULL;
    
    request->kind = strdup(kind);
    request->api_version = strdup(api_version);
    request->operation = strdup(operation);
    
    // Generate unique UID
    request->uid = strdup("webhook-req-1234567890");  // TODO: Use proper UUID generation
    
    return request;
}

void k8s_webhook_request_free(k8s_webhook_request_t* request) {
    if (!request) return;
    
    if (request->uid) free(request->uid);
    if (request->kind) free(request->kind);
    if (request->api_version) free(request->api_version);
    if (request->operation) free(request->operation);
    if (request->namespace) free(request->namespace);
    if (request->name) free(request->name);
    if (request->object_json) free(request->object_json);
    if (request->old_object_json) free(request->old_object_json);
    if (request->options_json) free(request->options_json);
    
    free(request);
}

k8s_webhook_response_t* k8s_webhook_response_new() {
    k8s_webhook_response_t* response = calloc(1, sizeof(k8s_webhook_response_t));
    if (!response) return NULL;
    
    response->allowed = true;
    response->http_code = 200;
    
    return response;
}

void k8s_webhook_response_free(k8s_webhook_response_t* response) {
    if (!response) return;
    
    if (response->message) free(response->message);
    if (response->patch_json) free(response->patch_json);
    if (response->patch_type) free(response->patch_type);
    
    free(response);
}

char* k8s_webhook_request_to_json(k8s_webhook_request_t* request) {
    if (!request) return NULL;
    
    static char json[16384];
    snprintf(json, sizeof(json),
        "{\"apiVersion\":\"admission.k8s.io/v1\","
        "\"kind\":\"AdmissionReview\","
        "\"request\":{"
        "\"uid\":\"%s\","
        "\"kind\":{\"group\":\"\",\"version\":\"%s\",\"kind\":\"%s\"},"
        "\"operation\":\"%s\","
        "\"namespace\":\"%s\","
        "\"name\":\"%s\","
        "\"object\":%s,"
        "\"oldObject\":%s"
        "}}",
        request->uid ? request->uid : "",
        request->api_version ? request->api_version : "",
        request->kind ? request->kind : "",
        request->operation ? request->operation : "",
        request->namespace ? request->namespace : "default",
        request->name ? request->name : "",
        request->object_json ? request->object_json : "null",
        request->old_object_json ? request->old_object_json : "null");
    
    return json;
}

k8s_webhook_response_t* k8s_webhook_response_from_json(const char* json) {
    if (!json) return NULL;
    
    k8s_webhook_response_t* response = k8s_webhook_response_new();
    if (!response) return NULL;
    
    // TODO: Parse JSON response
    // For now, create a default response
    response->allowed = true;
    response->http_code = 200;
    
    return response;
}
