#include "pod_lifecycle.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

pod_lifecycle_t* pod_lifecycle_new(k8s_pod_t* pod) {
    if (!pod) return NULL;
    
    pod_lifecycle_t* lifecycle = (pod_lifecycle_t*)malloc(sizeof(pod_lifecycle_t));
    lifecycle->pod = pod;
    lifecycle->events = (pod_event_t*)malloc(sizeof(pod_event_t) * 100);
    lifecycle->num_events = 0;
    lifecycle->phase_transition_time = time(NULL);
    
    // Initial event: pod created
    pod_lifecycle_add_event(lifecycle, EVENT_POD_CREATED, "Pod created");
    
    return lifecycle;
}

void pod_lifecycle_free(pod_lifecycle_t* lifecycle) {
    if (!lifecycle) return;
    
    for (int i = 0; i < lifecycle->num_events; i++) {
        if (lifecycle->events[i].pod_name) free(lifecycle->events[i].pod_name);
        if (lifecycle->events[i].pod_namespace) free(lifecycle->events[i].pod_namespace);
        if (lifecycle->events[i].message) free(lifecycle->events[i].message);
    }
    
    free(lifecycle->events);
    free(lifecycle);
}

// Transition pod to Running phase
int pod_lifecycle_transition_to_running(pod_lifecycle_t* lifecycle, const char* pod_ip, const char* host_ip) {
    if (!lifecycle || lifecycle->pod->status.phase != PHASE_PENDING) return -1;
    
    // Set pod and host IPs
    if (pod_ip) {
        lifecycle->pod->status.pod_ip = strdup(pod_ip);
    }
    if (host_ip) {
        lifecycle->pod->status.host_ip = strdup(host_ip);
    }
    
    // Transition to Running
    lifecycle->pod->status.phase = PHASE_RUNNING;
    lifecycle->pod->status.start_time = time(NULL);
    lifecycle->phase_transition_time = time(NULL);
    
    // Add event
    pod_lifecycle_add_event(lifecycle, EVENT_POD_STARTED, "Pod scheduled and running");
    pod_lifecycle_add_event(lifecycle, EVENT_POD_READY, "Pod ready to receive traffic");
    
    fprintf(stderr, "[pod-lifecycle] Pod %s/%s transitioned to Running (IP: %s)\n",
            lifecycle->pod->metadata.namespace, lifecycle->pod->metadata.name, pod_ip ? pod_ip : "N/A");
    
    return 0;
}

// Transition pod to Succeeded phase
int pod_lifecycle_transition_to_succeeded(pod_lifecycle_t* lifecycle) {
    if (!lifecycle || lifecycle->pod->status.phase == PHASE_SUCCEEDED || 
        lifecycle->pod->status.phase == PHASE_FAILED) return -1;
    
    lifecycle->pod->status.phase = PHASE_SUCCEEDED;
    lifecycle->phase_transition_time = time(NULL);
    
    pod_lifecycle_add_event(lifecycle, EVENT_POD_COMPLETED, "Pod completed successfully");
    
    fprintf(stderr, "[pod-lifecycle] Pod %s/%s transitioned to Succeeded\n",
            lifecycle->pod->metadata.namespace, lifecycle->pod->metadata.name);
    
    return 0;
}

// Transition pod to Failed phase
int pod_lifecycle_transition_to_failed(pod_lifecycle_t* lifecycle, const char* reason, const char* message) {
    if (!lifecycle || lifecycle->pod->status.phase == PHASE_SUCCEEDED || 
        lifecycle->pod->status.phase == PHASE_FAILED) return -1;
    
    lifecycle->pod->status.phase = PHASE_FAILED;
    lifecycle->phase_transition_time = time(NULL);
    
    char event_msg[256];
    snprintf(event_msg, sizeof(event_msg), "Pod failed: %s - %s", reason ? reason : "Unknown", message ? message : "");
    pod_lifecycle_add_event(lifecycle, EVENT_POD_FAILED, event_msg);
    
    fprintf(stderr, "[pod-lifecycle] Pod %s/%s transitioned to Failed: %s\n",
            lifecycle->pod->metadata.namespace, lifecycle->pod->metadata.name, event_msg);
    
    return 0;
}

// Transition pod to Terminating phase
int pod_lifecycle_transition_to_terminating(pod_lifecycle_t* lifecycle) {
    if (!lifecycle) return -1;
    
    if (lifecycle->pod->status.phase != PHASE_TERMINATING &&
        lifecycle->pod->status.phase != PHASE_SUCCEEDED &&
        lifecycle->pod->status.phase != PHASE_FAILED) {
        lifecycle->pod->status.phase = PHASE_TERMINATING;
        lifecycle->phase_transition_time = time(NULL);
        pod_lifecycle_add_event(lifecycle, EVENT_POD_FAILED, "Pod terminating");
    }
    
    return 0;
}

// Add event to lifecycle
int pod_lifecycle_add_event(pod_lifecycle_t* lifecycle, pod_event_type_t type, const char* message) {
    if (!lifecycle || lifecycle->num_events >= 100) return -1;
    
    pod_event_t* event = &lifecycle->events[lifecycle->num_events];
    event->type = type;
    event->pod_name = strdup(lifecycle->pod->metadata.name);
    event->pod_namespace = strdup(lifecycle->pod->metadata.namespace);
    event->message = strdup(message ? message : "");
    event->timestamp = time(NULL);
    
    lifecycle->num_events++;
    
    fprintf(stderr, "[pod-event] %s/%s: %s\n", 
            event->pod_namespace, event->pod_name, event->message);
    
    return 0;
}

// Get all events
pod_event_t* pod_lifecycle_get_events(pod_lifecycle_t* lifecycle, int* count) {
    if (!lifecycle || !count) return NULL;
    *count = lifecycle->num_events;
    return lifecycle->events;
}

// Check if pod is ready
int pod_lifecycle_is_ready(pod_lifecycle_t* lifecycle) {
    if (!lifecycle) return 0;
    return lifecycle->pod->status.phase == PHASE_RUNNING;
}

// Check if pod is completed
int pod_lifecycle_is_completed(pod_lifecycle_t* lifecycle) {
    if (!lifecycle) return 0;
    return lifecycle->pod->status.phase == PHASE_SUCCEEDED || 
           lifecycle->pod->status.phase == PHASE_FAILED;
}

// Convert phase enum to string
const char* pod_lifecycle_get_phase_string(k8s_phase_t phase) {
    switch (phase) {
        case PHASE_PENDING: return "Pending";
        case PHASE_RUNNING: return "Running";
        case PHASE_SUCCEEDED: return "Succeeded";
        case PHASE_FAILED: return "Failed";
        case PHASE_TERMINATING: return "Terminating";
        default: return "Unknown";
    }
}
