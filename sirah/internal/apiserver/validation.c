// internal/apiserver/validation.c
// Resource validation rules enforcement

#include "validation.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <regex.h>
#include <ctype.h>

// ============ Validation Result Management ============

validation_result_t* validation_result_new(void) {
    validation_result_t* result = (validation_result_t*)malloc(sizeof(validation_result_t));
    if (!result) return NULL;
    
    result->valid = true;
    result->errors = (validation_error_t**)malloc(sizeof(validation_error_t*) * 100);
    result->error_count = 0;
    
    return result;
}

void validation_result_free(validation_result_t* result) {
    if (!result) return;
    
    for (int i = 0; i < result->error_count; i++) {
        if (result->errors[i]) {
            free(result->errors[i]->field);
            free(result->errors[i]->message);
            free(result->errors[i]);
        }
    }
    free(result->errors);
    free(result);
}

int validation_result_add_error(validation_result_t* result,
                               const char* field, const char* message) {
    if (!result || !field || !message) return -1;
    
    if (result->error_count >= 99) return -1;  // Max 100 errors
    
    validation_error_t* error = (validation_error_t*)malloc(sizeof(validation_error_t));
    if (!error) return -1;
    
    error->field = (char*)malloc(strlen(field) + 1);
    strcpy(error->field, field);
    
    error->message = (char*)malloc(strlen(message) + 1);
    strcpy(error->message, message);
    
    result->errors[result->error_count++] = error;
    result->valid = false;
    
    return 0;
}

int validation_result_to_json(validation_result_t* result,
                             char* output_buffer, int buffer_size) {
    if (!result || !output_buffer || buffer_size < 10) return -1;
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "valid", json_object_new_boolean(result->valid));
    
    json_object* errors = json_object_new_array();
    for (int i = 0; i < result->error_count; i++) {
        json_object* error = json_object_new_object();
        json_object_object_add(error, "field", json_object_new_string(result->errors[i]->field));
        json_object_object_add(error, "message", json_object_new_string(result->errors[i]->message));
        json_object_array_add(errors, error);
    }
    json_object_object_add(root, "errors", errors);
    
    const char* json_str = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    strncpy(output_buffer, json_str, buffer_size - 1);
    json_object_put(root);
    
    return 0;
}

// ============ Name/Namespace Validation ============

bool validate_name(const char* name) {
    if (!name || strlen(name) == 0 || strlen(name) > 253) return false;
    
    // RFC 1123 subdomain: lowercase alphanumeric or '-', start/end with alphanumeric
    for (int i = 0; name[i]; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return false;
        }
        if (i == 0 || !name[i+1]) {
            if (c == '-') return false;  // Can't start or end with '-'
        }
    }
    return true;
}

bool validate_namespace(const char* namespace) {
    if (!namespace || strlen(namespace) == 0) return false;
    return validate_name(namespace);  // Same rules as names
}

bool validate_label(const char* key, const char* value) {
    if (!key || strlen(key) == 0 || strlen(key) > 63) return false;
    if (value && strlen(value) > 63) return false;
    
    // Key format: (optional prefix/)name
    const char* slash = strchr(key, '/');
    if (slash) {
        // Has prefix - validate domain portion
        int prefix_len = slash - key;
        if (prefix_len == 0 || prefix_len > 253) return false;
        key = slash + 1;  // Check name portion
    }
    
    // Name portion: alphanumeric, '-', '_', '.', start/end with alphanumeric
    if (strlen(key) == 0 || strlen(key) > 63) return false;
    
    char first = key[0];
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || (first >= '0' && first <= '9'))) {
        return false;
    }
    
    char last = key[strlen(key)-1];
    if (!((last >= 'a' && last <= 'z') || (last >= 'A' && last <= 'Z') || (last >= '0' && last <= '9'))) {
        return false;
    }
    
    for (int i = 0; key[i]; i++) {
        char c = key[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) {
            return false;
        }
    }
    
    // Validate value if present
    if (value && strlen(value) > 0) {
        char first_val = value[0];
        if (!((first_val >= 'a' && first_val <= 'z') || (first_val >= 'A' && first_val <= 'Z') ||
              (first_val >= '0' && first_val <= '9'))) {
            return false;
        }
        
        char last_val = value[strlen(value)-1];
        if (!((last_val >= 'a' && last_val <= 'z') || (last_val >= 'A' && last_val <= 'Z') ||
              (last_val >= '0' && last_val <= '9'))) {
            return false;
        }
        
        for (int i = 0; value[i]; i++) {
            char c = value[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
                  (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) {
                return false;
            }
        }
    }
    
    return true;
}

// ============ Resource Quantity Validation ============

bool validate_resource_quantity(const char* quantity) {
    if (!quantity || strlen(quantity) == 0) return false;
    
    // Format: number + optional unit (Ki, Mi, Gi, m, k, M, G)
    // Examples: "100m", "256Mi", "1Gi", "500", "1K"
    
    int i = 0;
    bool has_digit = false;
    bool has_decimal = false;
    
    // Parse number part
    while (quantity[i]) {
        char c = quantity[i];
        
        if (c >= '0' && c <= '9') {
            has_digit = true;
            i++;
        } else if (c == '.' && !has_decimal) {
            has_decimal = true;
            i++;
        } else {
            break;
        }
    }
    
    if (!has_digit) return false;
    
    // Parse unit part (optional)
    if (quantity[i]) {
        const char* units[] = {"m", "k", "M", "G", "T", "P", "Ki", "Mi", "Gi", "Ti", "Pi", NULL};
        bool found = false;
        
        for (int u = 0; units[u]; u++) {
            if (strcmp(&quantity[i], units[u]) == 0) {
                found = true;
                break;
            }
        }
        
        if (!found) return false;
    }
    
    return true;
}

// ============ Pod Validation ============

validation_result_t* validate_pod_metadata(json_object* metadata) {
    validation_result_t* result = validation_result_new();
    if (!metadata) {
        validation_result_add_error(result, "metadata", "Metadata is required");
        return result;
    }
    
    // Check name
    json_object* name_obj = json_object_object_get(metadata, "name");
    if (!name_obj) {
        validation_result_add_error(result, "metadata.name", "Name is required");
    } else {
        const char* name = json_object_get_string(name_obj);
        if (!validate_name(name)) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Invalid name format: '%s'", name);
            validation_result_add_error(result, "metadata.name", msg);
        }
    }
    
    // Check namespace (optional, defaults to 'default')
    json_object* namespace_obj = json_object_object_get(metadata, "namespace");
    if (namespace_obj) {
        const char* ns = json_object_get_string(namespace_obj);
        if (!validate_namespace(ns)) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Invalid namespace format: '%s'", ns);
            validation_result_add_error(result, "metadata.namespace", msg);
        }
    }
    
    // Check labels
    json_object* labels = json_object_object_get(metadata, "labels");
    if (labels && json_object_get_type(labels) == json_type_object) {
        json_object_iter iter;
        json_object_object_foreachC(labels, iter) {
            if (!validate_label(iter.key, json_object_get_string(iter.val))) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Invalid label '%s'", iter.key);
                validation_result_add_error(result, "metadata.labels", msg);
            }
        }
    }
    
    return result;
}

validation_result_t* validate_pod_containers(json_object* containers) {
    validation_result_t* result = validation_result_new();
    if (!containers || json_object_get_type(containers) != json_type_array) {
        validation_result_add_error(result, "spec.containers", "Containers must be a list");
        return result;
    }
    
    int num_containers = json_object_array_length(containers);
    if (num_containers == 0) {
        validation_result_add_error(result, "spec.containers", "At least one container is required");
        return result;
    }
    
    for (int i = 0; i < num_containers; i++) {
        json_object* container = json_object_array_get_idx(containers, i);
        if (!container) continue;
        
        // Check name
        json_object* name_obj = json_object_object_get(container, "name");
        if (!name_obj) {
            char msg[256];
            snprintf(msg, sizeof(msg), "spec.containers[%d].name is required", i);
            validation_result_add_error(result, "spec.containers", msg);
        } else {
            const char* name = json_object_get_string(name_obj);
            if (!validate_name(name)) {
                char msg[256];
                snprintf(msg, sizeof(msg), "spec.containers[%d].name: invalid format", i);
                validation_result_add_error(result, "spec.containers", msg);
            }
        }
        
        // Check image
        json_object* image_obj = json_object_object_get(container, "image");
        if (!image_obj) {
            char msg[256];
            snprintf(msg, sizeof(msg), "spec.containers[%d].image is required", i);
            validation_result_add_error(result, "spec.containers", msg);
        } else {
            const char* image = json_object_get_string(image_obj);
            if (!image || strlen(image) == 0) {
                char msg[256];
                snprintf(msg, sizeof(msg), "spec.containers[%d].image cannot be empty", i);
                validation_result_add_error(result, "spec.containers", msg);
            }
        }
        
        // Check resource requests/limits if present
        json_object* resources = json_object_object_get(container, "resources");
        if (resources) {
            json_object* requests = json_object_object_get(resources, "requests");
            if (requests) {
                json_object* cpu = json_object_object_get(requests, "cpu");
                if (cpu && !validate_resource_quantity(json_object_get_string(cpu))) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "spec.containers[%d].resources.requests.cpu: invalid format", i);
                    validation_result_add_error(result, "spec.containers", msg);
                }
                json_object* memory = json_object_object_get(requests, "memory");
                if (memory && !validate_resource_quantity(json_object_get_string(memory))) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "spec.containers[%d].resources.requests.memory: invalid format", i);
                    validation_result_add_error(result, "spec.containers", msg);
                }
            }
            
            json_object* limits = json_object_object_get(resources, "limits");
            if (limits) {
                json_object* cpu = json_object_object_get(limits, "cpu");
                if (cpu && !validate_resource_quantity(json_object_get_string(cpu))) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "spec.containers[%d].resources.limits.cpu: invalid format", i);
                    validation_result_add_error(result, "spec.containers", msg);
                }
                json_object* memory = json_object_object_get(limits, "memory");
                if (memory && !validate_resource_quantity(json_object_get_string(memory))) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "spec.containers[%d].resources.limits.memory: invalid format", i);
                    validation_result_add_error(result, "spec.containers", msg);
                }
            }
        }
        
        // Check port numbers if present
        json_object* ports = json_object_object_get(container, "ports");
        if (ports && json_object_get_type(ports) == json_type_array) {
            for (int p = 0; p < json_object_array_length(ports); p++) {
                json_object* port_obj = json_object_array_get_idx(ports, p);
                json_object* port_num = json_object_object_get(port_obj, "containerPort");
                if (port_num) {
                    int port = json_object_get_int(port_num);
                    if (port < 1 || port > 65535) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "spec.containers[%d].ports[%d].containerPort: port out of range", i, p);
                        validation_result_add_error(result, "spec.containers", msg);
                    }
                }
            }
        }
    }
    
    return result;
}

validation_result_t* validate_pod_spec(json_object* pod_spec) {
    validation_result_t* result = validation_result_new();
    if (!pod_spec) {
        validation_result_add_error(result, "spec", "Spec is required");
        return result;
    }
    
    // Check containers
    json_object* containers = json_object_object_get(pod_spec, "containers");
    validation_result_t* container_result = validate_pod_containers(containers);
    if (!container_result->valid) {
        for (int i = 0; i < container_result->error_count; i++) {
            validation_result_add_error(result, container_result->errors[i]->field,
                                       container_result->errors[i]->message);
        }
    }
    validation_result_free(container_result);
    
    return result;
}

// ============ Service Validation ============

validation_result_t* validate_service_selector(json_object* selector) {
    validation_result_t* result = validation_result_new();
    
    if (selector && json_object_get_type(selector) == json_type_object) {
        json_object_iter iter;
        json_object_object_foreachC(selector, iter) {
            if (!validate_label(iter.key, json_object_get_string(iter.val))) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Invalid label selector '%s'", iter.key);
                validation_result_add_error(result, "spec.selector", msg);
            }
        }
    }
    
    return result;
}

validation_result_t* validate_service_ports(json_object* ports) {
    validation_result_t* result = validation_result_new();
    
    if (ports && json_object_get_type(ports) == json_type_array) {
        for (int i = 0; i < json_object_array_length(ports); i++) {
            json_object* port_obj = json_object_array_get_idx(ports, i);
            
            json_object* port_num = json_object_object_get(port_obj, "port");
            if (port_num) {
                int port = json_object_get_int(port_num);
                if (port < 1 || port > 65535) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "spec.ports[%d].port: out of range (1-65535)", i);
                    validation_result_add_error(result, "spec.ports", msg);
                }
            }
            
            json_object* target_port = json_object_object_get(port_obj, "targetPort");
            if (target_port) {
                if (json_object_get_type(target_port) == json_type_int) {
                    int port = json_object_get_int(target_port);
                    if (port < 1 || port > 65535) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "spec.ports[%d].targetPort: out of range (1-65535)", i);
                        validation_result_add_error(result, "spec.ports", msg);
                    }
                }
            }
        }
    }
    
    return result;
}

validation_result_t* validate_service_spec(json_object* service_spec) {
    validation_result_t* result = validation_result_new();
    if (!service_spec) {
        validation_result_add_error(result, "spec", "Spec is required");
        return result;
    }
    
    // Check selector
    json_object* selector = json_object_object_get(service_spec, "selector");
    validation_result_t* selector_result = validate_service_selector(selector);
    if (!selector_result->valid) {
        for (int i = 0; i < selector_result->error_count; i++) {
            validation_result_add_error(result, selector_result->errors[i]->field,
                                       selector_result->errors[i]->message);
        }
    }
    validation_result_free(selector_result);
    
    // Check ports
    json_object* ports = json_object_object_get(service_spec, "ports");
    validation_result_t* ports_result = validate_service_ports(ports);
    if (!ports_result->valid) {
        for (int i = 0; i < ports_result->error_count; i++) {
            validation_result_add_error(result, ports_result->errors[i]->field,
                                       ports_result->errors[i]->message);
        }
    }
    validation_result_free(ports_result);
    
    return result;
}

// ============ Deployment Validation ============

validation_result_t* validate_replica_count(json_object* replicas) {
    validation_result_t* result = validation_result_new();
    
    if (replicas && json_object_get_type(replicas) == json_type_int) {
        int count = json_object_get_int(replicas);
        if (count < 0 || count > 10000) {
            validation_result_add_error(result, "spec.replicas", "Replicas must be between 0 and 10000");
        }
    }
    
    return result;
}

validation_result_t* validate_deployment_spec(json_object* deployment_spec) {
    validation_result_t* result = validation_result_new();
    if (!deployment_spec) {
        validation_result_add_error(result, "spec", "Spec is required");
        return result;
    }
    
    // Check replicas
    json_object* replicas = json_object_object_get(deployment_spec, "replicas");
    validation_result_t* replicas_result = validate_replica_count(replicas);
    if (!replicas_result->valid) {
        for (int i = 0; i < replicas_result->error_count; i++) {
            validation_result_add_error(result, replicas_result->errors[i]->field,
                                       replicas_result->errors[i]->message);
        }
    }
    validation_result_free(replicas_result);
    
    // Check template pod spec
    json_object* template = json_object_object_get(deployment_spec, "template");
    if (template) {
        json_object* spec = json_object_object_get(template, "spec");
        validation_result_t* pod_result = validate_pod_spec(spec);
        if (!pod_result->valid) {
            for (int i = 0; i < pod_result->error_count; i++) {
                validation_result_add_error(result, pod_result->errors[i]->field,
                                           pod_result->errors[i]->message);
            }
        }
        validation_result_free(pod_result);
    }
    
    return result;
}
