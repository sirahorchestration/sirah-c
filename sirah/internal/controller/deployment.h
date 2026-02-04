// internal/controller/deployment.h
// Deployment Controller - Manages Deployment objects and reconciles replica counts
// Watches deployments, creates/deletes pods to match desired replica count,
// implements rolling updates, tracks status, and generates events

#ifndef SIRAH_CONTROLLER_DEPLOYMENT_H
#define SIRAH_CONTROLLER_DEPLOYMENT_H

#include <stdint.h>

typedef struct {
    char name[256];
    char namespace[256];
    int desired_replicas;
    int current_replicas;
    int ready_replicas;
    int updated_replicas;
    char image[512];
    uint64_t resource_version;
    time_t last_update;
} deployment_t;

// Initialize deployment controller with API server URL
int deployment_controller_init(void);

// Main reconciliation loop - runs continuously
int deployment_controller_run(void);

// Manually reconcile a specific deployment
int deployment_controller_reconcile(deployment_t* deployment);

// Create pods for deployment replicas
int deployment_create_replicas(const char* namespace, const char* deployment_name,
                               const char* image, int desired_count);

// Delete excess pods from deployment
int deployment_delete_excess_pods(const char* namespace, const char* deployment_name,
                                  int desired_count);

// Rolling update - gradually replace pods with new image
int deployment_rolling_update(const char* namespace, const char* deployment_name,
                              const char* new_image, int max_surge, int max_unavailable);

// Update deployment status in API server
int deployment_update_status(const char* namespace, const char* deployment_name,
                            int current, int ready, int updated);

// Generate event for deployment status changes
int deployment_emit_event(const char* namespace, const char* deployment_name,
                         const char* reason, const char* message);

// Shutdown deployment controller
int deployment_controller_shutdown(void);

#endif // SIRAH_CONTROLLER_DEPLOYMENT_H
