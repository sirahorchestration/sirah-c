// internal/apiserver/probes.h
// Health check probe implementation for pod startup, readiness, liveness

#ifndef K8S_API_PROBES_H
#define K8S_API_PROBES_H

#include <time.h>
#include <stdbool.h>

// Probe types
typedef enum {
    PROBE_STARTUP = 1,      // Startup probe - wait for container to be ready
    PROBE_READINESS = 2,    // Readiness probe - track if pod ready to serve
    PROBE_LIVENESS = 3      // Liveness probe - detect dead/hanging containers
} probe_type_t;

// Probe result
typedef enum {
    PROBE_SUCCESS = 1,
    PROBE_FAILURE = 2,
    PROBE_UNKNOWN = 3
} probe_result_t;

// Probe spec (from pod definition)
typedef struct {
    probe_type_t type;
    char* handler_type;  // "http", "tcp", "exec"
    
    // HTTP handler
    char* http_path;
    int http_port;
    
    // TCP handler
    int tcp_port;
    
    // Exec handler
    char* exec_command;
    
    // Timing
    int initial_delay_seconds;  // Delay before first probe
    int timeout_seconds;         // Timeout for each probe
    int period_seconds;          // How often to probe
    int success_threshold;       // Consecutive successes needed
    int failure_threshold;       // Consecutive failures before stop
} probe_spec_t;

// Probe status
typedef struct {
    probe_type_t type;
    probe_result_t last_result;
    int success_count;
    int failure_count;
    time_t last_probe_time;
    char* failure_reason;
} probe_status_t;

// Pod probe tracking
typedef struct {
    char* pod_name;
    char* namespace;
    
    probe_spec_t* startup_probe;
    probe_status_t* startup_status;
    bool startup_complete;
    
    probe_spec_t* readiness_probe;
    probe_status_t* readiness_status;
    bool is_ready;
    
    probe_spec_t* liveness_probe;
    probe_status_t* liveness_status;
    bool is_alive;
    
    time_t created_time;
} pod_probes_t;

// ============ Probe Spec ============

probe_spec_t* probe_spec_new(probe_type_t type);
void probe_spec_free(probe_spec_t* spec);

int probe_spec_set_http_handler(probe_spec_t* spec, const char* path, int port);
int probe_spec_set_tcp_handler(probe_spec_t* spec, int port);
int probe_spec_set_exec_handler(probe_spec_t* spec, const char* command);
int probe_spec_set_timing(probe_spec_t* spec, int initial_delay, int timeout, 
                          int period, int success_threshold, int failure_threshold);

// ============ Probe Status ============

probe_status_t* probe_status_new(probe_type_t type);
void probe_status_free(probe_status_t* status);

int probe_status_record_success(probe_status_t* status);
int probe_status_record_failure(probe_status_t* status, const char* reason);

// ============ Pod Probes ============

pod_probes_t* pod_probes_new(const char* pod_name, const char* namespace);
void pod_probes_free(pod_probes_t* probes);

int pod_probes_set_startup_probe(pod_probes_t* probes, probe_spec_t* spec);
int pod_probes_set_readiness_probe(pod_probes_t* probes, probe_spec_t* spec);
int pod_probes_set_liveness_probe(pod_probes_t* probes, probe_spec_t* spec);

// Execute probes and update status
int pod_probe_startup(pod_probes_t* probes);
int pod_probe_readiness(pod_probes_t* probes);
int pod_probe_liveness(pod_probes_t* probes);

// Check if probe thresholds met
bool pod_startup_complete(pod_probes_t* probes);
bool pod_is_ready(pod_probes_t* probes);
bool pod_is_alive(pod_probes_t* probes);

#endif
