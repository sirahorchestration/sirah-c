/**
 * ResourceQuota Admission Control Middleware Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include "quota_middleware.h"
#include "../controller/resource_quota.h"

/* ============================================================================
   Helper Functions - Quantity Parsing
   ============================================================================ */

/**
 * Parse CPU quantity string (100m, 1, 0.5, etc.)
 */
static long long parse_cpu_quantity(const char* cpu_str) {
    if (!cpu_str) return 0;
    
    long long millicores = 0;
    
    /* Handle millicores (e.g., "100m" = 100 millicores) */
    if (strstr(cpu_str, "m")) {
        sscanf(cpu_str, "%lld", &millicores);
        return millicores;
    }
    
    /* Handle decimal cores (e.g., "0.5" = 500 millicores) */
    double cores = 0;
    if (sscanf(cpu_str, "%lf", &cores) == 1) {
        return (long long)(cores * 1000);
    }
    
    return 0;
}

/**
 * Parse memory quantity string (Mi, Gi, Ki, etc.)
 */
static long long parse_memory_quantity(const char* mem_str) {
    if (!mem_str) return 0;
    
    long long value = 0;
    char unit[32] = {0};
    
    sscanf(mem_str, "%lld%31s", &value, unit);
    
    if (strcmp(unit, "Mi") == 0) {
        return value * 1048576;  /* Mebibytes to bytes */
    } else if (strcmp(unit, "Gi") == 0) {
        return value * 1073741824;  /* Gibibytes to bytes */
    } else if (strcmp(unit, "Ki") == 0) {
        return value * 1024;  /* Kibibytes to bytes */
    } else if (strcmp(unit, "Ei") == 0) {
        return value * 1152921504606846976LL;  /* Exbibytes */
    } else if (strcmp(unit, "Pi") == 0) {
        return value * 1125899906842624LL;  /* Pebibytes */
    } else if (strcmp(unit, "Ti") == 0) {
        return value * 1099511627776LL;  /* Tebibytes */
    }
    
    return value;  /* Default to bytes */
}

/**
 * Parse CPU quantity from JSON pod spec
 * Looks for spec.containers[].resources.requests.cpu or limits.cpu
 */
static long long parse_pod_cpu(const char* pod_spec_json) {
    if (!pod_spec_json) return 0;
    
    json_object* root = json_tokener_parse(pod_spec_json);
    if (!root) return 0;
    
    long long total_cpu = 0;
    
    /* Navigate to spec.containers */
    json_object* spec = NULL;
    if (json_object_object_get_ex(root, "spec", &spec)) {
        json_object* containers = NULL;
        if (json_object_object_get_ex(spec, "containers", &containers)) {
            
            int container_count = json_object_array_length(containers);
            for (int i = 0; i < container_count; i++) {
                json_object* container = json_object_array_get_idx(containers, i);
                if (!container) continue;
                
                /* Check for resources.requests.cpu */
                json_object* resources = NULL;
                if (json_object_object_get_ex(container, "resources", &resources)) {
                    
                    json_object* requests = NULL;
                    if (json_object_object_get_ex(resources, "requests", &requests)) {
                        json_object* cpu_obj = NULL;
                        if (json_object_object_get_ex(requests, "cpu", &cpu_obj)) {
                            const char* cpu_str = json_object_get_string(cpu_obj);
                            total_cpu += parse_cpu_quantity(cpu_str);
                        }
                    }
                    
                    /* If no requests, check limits */
                    json_object* limits = NULL;
                    if (json_object_object_get_ex(resources, "limits", &limits)) {
                        json_object* cpu_obj = NULL;
                        if (json_object_object_get_ex(limits, "cpu", &cpu_obj)) {
                            const char* cpu_str = json_object_get_string(cpu_obj);
                            long long limit_cpu = parse_cpu_quantity(cpu_str);
                            if (limit_cpu > total_cpu) {
                                total_cpu = limit_cpu;
                            }
                        }
                    }
                }
            }
        }
    }
    
    json_object_put(root);
    return total_cpu;
}

/**
 * Parse memory quantity from JSON pod spec
 */
static long long parse_pod_memory(const char* pod_spec_json) {
    if (!pod_spec_json) return 0;
    
    json_object* root = json_tokener_parse(pod_spec_json);
    if (!root) return 0;
    
    long long total_memory = 0;
    
    json_object* spec = NULL;
    if (json_object_object_get_ex(root, "spec", &spec)) {
        json_object* containers = NULL;
        if (json_object_object_get_ex(spec, "containers", &containers)) {
            
            int container_count = json_object_array_length(containers);
            for (int i = 0; i < container_count; i++) {
                json_object* container = json_object_array_get_idx(containers, i);
                if (!container) continue;
                
                json_object* resources = NULL;
                if (json_object_object_get_ex(container, "resources", &resources)) {
                    
                    json_object* requests = NULL;
                    if (json_object_object_get_ex(resources, "requests", &requests)) {
                        json_object* mem_obj = NULL;
                        if (json_object_object_get_ex(requests, "memory", &mem_obj)) {
                            const char* mem_str = json_object_get_string(mem_obj);
                            total_memory += parse_memory_quantity(mem_str);
                        }
                    }
                    
                    json_object* limits = NULL;
                    if (json_object_object_get_ex(resources, "limits", &limits)) {
                        json_object* mem_obj = NULL;
                        if (json_object_object_get_ex(limits, "memory", &mem_obj)) {
                            const char* mem_str = json_object_get_string(mem_obj);
                            long long limit_mem = parse_memory_quantity(mem_str);
                            if (limit_mem > total_memory) {
                                total_memory = limit_mem;
                            }
                        }
                    }
                }
            }
        }
    }
    
    json_object_put(root);
    return total_memory;
}

/* ============================================================================
   Middleware Functions
   ============================================================================ */

int quota_middleware_get_namespace(const char* namespace_str,
                                  char* namespace_out) {
    if (!namespace_str || !namespace_out) {
        return -1;
    }
    
    strncpy(namespace_out, namespace_str, 63);
    namespace_out[63] = '\0';
    return 0;
}

int quota_middleware_check_pod_creation(const char* namespace,
                                       const char* pod_spec_json,
                                       quota_admission_result_t* result_out) {
    if (!namespace || !pod_spec_json || !result_out) {
        return -1;
    }
    
    /* Parse CPU and memory from pod spec */
    long long cpu_millicores = parse_pod_cpu(pod_spec_json);
    long long memory_bytes = parse_pod_memory(pod_spec_json);
    
    /* Use defaults if not specified */
    if (cpu_millicores == 0) cpu_millicores = 100;      /* 100m default */
    if (memory_bytes == 0) memory_bytes = 128 * 1048576; /* 128 Mi default */
    
    /* Check quota */
    int check_result = resource_quota_can_create_pod(namespace,
                                                     cpu_millicores,
                                                     memory_bytes,
                                                     result_out);
    
    return check_result;
}

int quota_middleware_check_service_creation(const char* namespace,
                                           quota_admission_result_t* result_out) {
    if (!namespace || !result_out) {
        return -1;
    }
    
    return resource_quota_can_create_service(namespace, result_out);
}

int quota_middleware_check_configmap_creation(const char* namespace,
                                             quota_admission_result_t* result_out) {
    if (!namespace || !result_out) {
        return -1;
    }
    
    return resource_quota_can_create_configmap(namespace, result_out);
}

int quota_middleware_check_secret_creation(const char* namespace,
                                          quota_admission_result_t* result_out) {
    if (!namespace || !result_out) {
        return -1;
    }
    
    return resource_quota_can_create_secret(namespace, result_out);
}

int quota_middleware_format_denied(const quota_admission_result_t* result,
                                  char* response_buffer,
                                  int* response_code) {
    if (!result || !response_buffer || !response_code) {
        return -1;
    }
    
    return resource_quota_format_quota_exceeded(result, response_buffer, response_code);
}
