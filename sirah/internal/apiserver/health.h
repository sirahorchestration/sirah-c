#ifndef K8S_API_HEALTH_H
#define K8S_API_HEALTH_H

#include <time.h>
#include <stdbool.h>

// Component health status
typedef enum {
    K8S_COMPONENT_HEALTHY = 1,
    K8S_COMPONENT_UNHEALTHY = 2,
    K8S_COMPONENT_UNKNOWN = 3
} k8s_component_status_t;

// Single component health info
typedef struct {
    char* name;
    k8s_component_status_t status;
    char* message;
    time_t last_check_time;
    int response_time_ms;
} k8s_component_health_t;

// Overall system health
typedef struct {
    k8s_component_status_t overall_status;
    
    // Component health checks
    k8s_component_health_t** components;
    int num_components;
    
    // Metrics
    int total_requests;
    int failed_requests;
    long uptime_seconds;
    
    time_t last_update_time;
} k8s_system_health_t;

// ============ Component Health ============

k8s_component_health_t* k8s_component_health_new(const char* name);
void k8s_component_health_free(k8s_component_health_t* health);

int k8s_component_health_set_status(k8s_component_health_t* health, k8s_component_status_t status);
int k8s_component_health_set_message(k8s_component_health_t* health, const char* message);
int k8s_component_health_record_check(k8s_component_health_t* health, int response_time_ms);

// ============ System Health ============

k8s_system_health_t* k8s_system_health_new();
void k8s_system_health_free(k8s_system_health_t* health);

// Add/remove components
int k8s_system_health_add_component(k8s_system_health_t* health, k8s_component_health_t* component);
int k8s_system_health_remove_component(k8s_system_health_t* health, const char* component_name);
k8s_component_health_t* k8s_system_health_get_component(k8s_system_health_t* health, const char* component_name);

// Check component
int k8s_system_health_check_component(k8s_system_health_t* health, const char* component_name, k8s_component_status_t status, const char* message);

// Evaluate overall health based on components
void k8s_system_health_evaluate(k8s_system_health_t* health);

// Track metrics
int k8s_system_health_record_request(k8s_system_health_t* health, bool success);
int k8s_system_health_set_uptime(k8s_system_health_t* health, long uptime_seconds);

// Serialization (JSON)
char* k8s_system_health_to_json(k8s_system_health_t* health);
char* k8s_healthz_response(k8s_system_health_t* health);  // /healthz endpoint
char* k8s_readyz_response(k8s_system_health_t* health);  // /readyz endpoint

// Global instance
k8s_system_health_t* k8s_system_health_global();

// HTTP Handler stubs
// Call these from HTTP handlers for /healthz and /readyz endpoints
char* k8s_api_healthz_handler(k8s_system_health_t* health);
char* k8s_api_readyz_handler(k8s_system_health_t* health);

#endif
