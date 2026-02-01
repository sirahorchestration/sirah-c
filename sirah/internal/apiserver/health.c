#include "health.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

static k8s_system_health_t* g_system_health = NULL;

// ============ Component Health ============

k8s_component_health_t* k8s_component_health_new(const char* name) {
    if (!name) return NULL;
    
    k8s_component_health_t* health = (k8s_component_health_t*)malloc(sizeof(k8s_component_health_t));
    if (!health) return NULL;
    
    health->name = (char*)malloc(strlen(name) + 1);
    if (!health->name) { free(health); return NULL; }
    strcpy(health->name, name);
    
    health->status = K8S_COMPONENT_UNKNOWN;
    health->message = (char*)malloc(256);
    if (!health->message) { free(health->name); free(health); return NULL; }
    strcpy(health->message, "Component initializing...");
    
    health->last_check_time = time(NULL);
    health->response_time_ms = 0;
    
    return health;
}

void k8s_component_health_free(k8s_component_health_t* health) {
    if (!health) return;
    
    free(health->name);
    free(health->message);
    free(health);
}

int k8s_component_health_set_status(k8s_component_health_t* health, k8s_component_status_t status) {
    if (!health) return -1;
    health->status = status;
    return 0;
}

int k8s_component_health_set_message(k8s_component_health_t* health, const char* message) {
    if (!health || !message) return -1;
    
    if (strlen(message) >= 256) return -1;
    
    strcpy(health->message, message);
    return 0;
}

int k8s_component_health_record_check(k8s_component_health_t* health, int response_time_ms) {
    if (!health) return -1;
    
    health->response_time_ms = response_time_ms;
    health->last_check_time = time(NULL);
    
    return 0;
}

// ============ System Health ============

k8s_system_health_t* k8s_system_health_new() {
    k8s_system_health_t* health = (k8s_system_health_t*)malloc(sizeof(k8s_system_health_t));
    if (!health) return NULL;
    
    health->overall_status = K8S_COMPONENT_UNKNOWN;
    health->components = NULL;
    health->num_components = 0;
    health->total_requests = 0;
    health->failed_requests = 0;
    health->uptime_seconds = 0;
    health->last_update_time = time(NULL);
    
    return health;
}

void k8s_system_health_free(k8s_system_health_t* health) {
    if (!health) return;
    
    if (health->components) {
        for (int i = 0; i < health->num_components; i++) {
            k8s_component_health_free(health->components[i]);
        }
        free(health->components);
    }
    
    free(health);
}

int k8s_system_health_add_component(k8s_system_health_t* health, k8s_component_health_t* component) {
    if (!health || !component || health->num_components >= 20) return -1;  // Max 20 components
    
    // Check for duplicates
    for (int i = 0; i < health->num_components; i++) {
        if (strcmp(health->components[i]->name, component->name) == 0) {
            return -1;
        }
    }
    
    k8s_component_health_t** new_components = (k8s_component_health_t**)realloc(
        health->components,
        (health->num_components + 1) * sizeof(k8s_component_health_t*)
    );
    if (!new_components) return -1;
    
    health->components = new_components;
    health->components[health->num_components] = component;
    health->num_components++;
    
    return 0;
}

int k8s_system_health_remove_component(k8s_system_health_t* health, const char* component_name) {
    if (!health || !component_name) return -1;
    
    for (int i = 0; i < health->num_components; i++) {
        if (strcmp(health->components[i]->name, component_name) == 0) {
            k8s_component_health_free(health->components[i]);
            
            for (int j = i; j < health->num_components - 1; j++) {
                health->components[j] = health->components[j + 1];
            }
            health->num_components--;
            return 0;
        }
    }
    
    return -1;
}

k8s_component_health_t* k8s_system_health_get_component(k8s_system_health_t* health, const char* component_name) {
    if (!health || !component_name) return NULL;
    
    for (int i = 0; i < health->num_components; i++) {
        if (strcmp(health->components[i]->name, component_name) == 0) {
            return health->components[i];
        }
    }
    
    return NULL;
}

int k8s_system_health_check_component(k8s_system_health_t* health, const char* component_name, k8s_component_status_t status, const char* message) {
    if (!health || !component_name || !message) return -1;
    
    k8s_component_health_t* component = k8s_system_health_get_component(health, component_name);
    if (!component) return -1;
    
    k8s_component_health_set_status(component, status);
    k8s_component_health_set_message(component, message);
    k8s_component_health_record_check(component, 0);
    
    return 0;
}

void k8s_system_health_evaluate(k8s_system_health_t* health) {
    if (!health) return;
    
    // System is healthy if all components are healthy
    health->overall_status = K8S_COMPONENT_HEALTHY;
    
    for (int i = 0; i < health->num_components; i++) {
        if (health->components[i]->status == K8S_COMPONENT_UNHEALTHY) {
            health->overall_status = K8S_COMPONENT_UNHEALTHY;
            break;
        }
        if (health->components[i]->status == K8S_COMPONENT_UNKNOWN) {
            if (health->overall_status != K8S_COMPONENT_UNHEALTHY) {
                health->overall_status = K8S_COMPONENT_UNKNOWN;
            }
        }
    }
    
    health->last_update_time = time(NULL);
}

int k8s_system_health_record_request(k8s_system_health_t* health, bool success) {
    if (!health) return -1;
    
    health->total_requests++;
    if (!success) {
        health->failed_requests++;
    }
    
    return 0;
}

int k8s_system_health_set_uptime(k8s_system_health_t* health, long uptime_seconds) {
    if (!health) return -1;
    
    health->uptime_seconds = uptime_seconds;
    return 0;
}

// ============ Serialization ============

char* k8s_system_health_to_json(k8s_system_health_t* health) {
    if (!health) return NULL;
    
    char* json = (char*)malloc(4096);
    if (!json) return NULL;
    
    char* ptr = json;
    int remaining = 4096;
    
    ptr += snprintf(ptr, remaining, "{\"status\":\"%s\",\"components\":[",
        health->overall_status == K8S_COMPONENT_HEALTHY ? "Healthy" : 
        health->overall_status == K8S_COMPONENT_UNHEALTHY ? "Unhealthy" : "Unknown"
    );
    remaining = 4096 - (ptr - json);
    
    for (int i = 0; i < health->num_components; i++) {
        k8s_component_health_t* comp = health->components[i];
        if (i > 0) {
            ptr += snprintf(ptr, remaining, ",");
            remaining = 4096 - (ptr - json);
        }
        
        ptr += snprintf(ptr, remaining, 
            "{\"name\":\"%s\",\"status\":\"%s\",\"message\":\"%s\",\"responseTimeMs\":%d}",
            comp->name,
            comp->status == K8S_COMPONENT_HEALTHY ? "Healthy" : 
            comp->status == K8S_COMPONENT_UNHEALTHY ? "Unhealthy" : "Unknown",
            comp->message,
            comp->response_time_ms
        );
        remaining = 4096 - (ptr - json);
    }
    
    ptr += snprintf(ptr, remaining, 
        "],\"metrics\":{\"totalRequests\":%d,\"failedRequests\":%d,\"uptimeSeconds\":%ld}}",
        health->total_requests, health->failed_requests, health->uptime_seconds
    );
    
    return json;
}

char* k8s_healthz_response(k8s_system_health_t* health) {
    if (!health) return NULL;
    
    k8s_system_health_evaluate(health);
    
    // For /healthz, return simple OK response if all components are healthy or unknown
    char* response = (char*)malloc(512);
    if (!response) return NULL;
    
    if (health->overall_status == K8S_COMPONENT_UNHEALTHY) {
        sprintf(response, "{\"status\":\"unhealthy\",\"message\":\"One or more components are unhealthy\"}");
    } else {
        sprintf(response, "{\"status\":\"ok\",\"message\":\"API server is healthy\"}");
    }
    
    return response;
}

char* k8s_readyz_response(k8s_system_health_t* health) {
    if (!health) return NULL;
    
    k8s_system_health_evaluate(health);
    
    // For /readyz, return ready only if all components are healthy
    char* response = (char*)malloc(512);
    if (!response) return NULL;
    
    if (health->overall_status == K8S_COMPONENT_HEALTHY) {
        sprintf(response, "{\"status\":\"ready\",\"message\":\"API server is ready to accept traffic\"}");
    } else if (health->overall_status == K8S_COMPONENT_UNKNOWN) {
        sprintf(response, "{\"status\":\"not-ready\",\"message\":\"Some components are still initializing\"}");
    } else {
        sprintf(response, "{\"status\":\"not-ready\",\"message\":\"One or more components are unhealthy\"}");
    }
    
    return response;
}

// ============ HTTP Handlers ============

char* k8s_api_healthz_handler(k8s_system_health_t* health) {
    return k8s_healthz_response(health);
}

char* k8s_api_readyz_handler(k8s_system_health_t* health) {
    return k8s_readyz_response(health);
}

// ============ Global Instance ============

k8s_system_health_t* k8s_system_health_global() {
    if (!g_system_health) {
        g_system_health = k8s_system_health_new();
        
        // Initialize with common components
        k8s_component_health_t* apiserver = k8s_component_health_new("apiserver");
        k8s_component_health_set_status(apiserver, K8S_COMPONENT_HEALTHY);
        k8s_component_health_set_message(apiserver, "API server running");
        k8s_system_health_add_component(g_system_health, apiserver);
        
        k8s_component_health_t* etcd = k8s_component_health_new("etcd");
        k8s_component_health_set_status(etcd, K8S_COMPONENT_HEALTHY);
        k8s_component_health_set_message(etcd, "etcd backend healthy");
        k8s_system_health_add_component(g_system_health, etcd);
        
        k8s_component_health_t* scheduler = k8s_component_health_new("scheduler");
        k8s_component_health_set_status(scheduler, K8S_COMPONENT_HEALTHY);
        k8s_component_health_set_message(scheduler, "Scheduler running");
        k8s_system_health_add_component(g_system_health, scheduler);
        
        k8s_component_health_t* controller = k8s_component_health_new("controller-manager");
        k8s_component_health_set_status(controller, K8S_COMPONENT_HEALTHY);
        k8s_component_health_set_message(controller, "Controller manager running");
        k8s_system_health_add_component(g_system_health, controller);
    }
    
    return g_system_health;
}
