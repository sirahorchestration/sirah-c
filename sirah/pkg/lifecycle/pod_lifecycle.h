#ifndef SIRAH_POD_LIFECYCLE_H
#define SIRAH_POD_LIFECYCLE_H

#include "../types/pod.h"
#include <time.h>

// Pod lifecycle event types
typedef enum {
    EVENT_POD_CREATED,
    EVENT_POD_SCHEDULED,
    EVENT_POD_STARTED,
    EVENT_POD_READY,
    EVENT_POD_COMPLETED,
    EVENT_POD_FAILED,
    EVENT_CONTAINER_STARTED,
    EVENT_CONTAINER_FAILED,
    EVENT_PROBE_SUCCESS,
    EVENT_PROBE_FAILURE
} pod_event_type_t;

// Pod lifecycle event
typedef struct {
    pod_event_type_t type;
    char* pod_name;
    char* pod_namespace;
    char* message;
    time_t timestamp;
} pod_event_t;

// Pod lifecycle manager
typedef struct {
    k8s_pod_t* pod;
    pod_event_t* events;
    int num_events;
    time_t phase_transition_time;
} pod_lifecycle_t;

// Lifecycle operations
pod_lifecycle_t* pod_lifecycle_new(k8s_pod_t* pod);
void pod_lifecycle_free(pod_lifecycle_t* lifecycle);

// Phase transitions with validation
int pod_lifecycle_transition_to_running(pod_lifecycle_t* lifecycle, const char* pod_ip, const char* host_ip);
int pod_lifecycle_transition_to_succeeded(pod_lifecycle_t* lifecycle);
int pod_lifecycle_transition_to_failed(pod_lifecycle_t* lifecycle, const char* reason, const char* message);
int pod_lifecycle_transition_to_terminating(pod_lifecycle_t* lifecycle);

// Event tracking
int pod_lifecycle_add_event(pod_lifecycle_t* lifecycle, pod_event_type_t type, const char* message);
pod_event_t* pod_lifecycle_get_events(pod_lifecycle_t* lifecycle, int* count);

// Status queries
int pod_lifecycle_is_ready(pod_lifecycle_t* lifecycle);
int pod_lifecycle_is_completed(pod_lifecycle_t* lifecycle);
const char* pod_lifecycle_get_phase_string(k8s_phase_t phase);

#endif
