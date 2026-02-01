#ifndef SIRAH_TYPES_COMMON_H
#define SIRAH_TYPES_COMMON_H

#include <time.h>
#include <uuid/uuid.h>
#include <json-c/json.h>

// Kubernetes object metadata
typedef struct {
    char* name;
    char* namespace;
    char* uid;                    // UUID
    char* resource_version;       // For optimistic concurrency
    time_t creation_timestamp;
    time_t deletion_timestamp;
    char** owner_references;      // Parent objects
    int num_owners;
    char** finalizers;
    int num_finalizers;
} k8s_metadata_t;

// Object phase/status
typedef enum {
    PHASE_PENDING = 0,
    PHASE_RUNNING = 1,
    PHASE_SUCCEEDED = 2,
    PHASE_FAILED = 3,
    PHASE_UNKNOWN = 4,
    PHASE_TERMINATING = 5
} k8s_phase_t;

// Condition structure
typedef struct {
    char* type;
    char* status;              // "True", "False", "Unknown"
    time_t last_probe_time;
    time_t last_transition_time;
    char* reason;
    char* message;
} k8s_condition_t;

// Resource quantities
typedef struct {
    int cpu_millicores;        // 500m = 500
    int memory_bytes;          // 128Mi = 134217728
    int storage_bytes;
} k8s_resource_quantity_t;

// Label and annotation helpers
typedef struct {
    char* key;
    char* value;
} k8s_label_t;

// Type helpers
k8s_metadata_t* k8s_metadata_new(const char* name, const char* namespace);
void k8s_metadata_free(k8s_metadata_t* meta);
char* k8s_metadata_to_json(k8s_metadata_t* meta);
k8s_metadata_t* k8s_metadata_from_json(json_object* obj);

#endif
