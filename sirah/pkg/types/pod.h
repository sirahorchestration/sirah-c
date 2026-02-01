#ifndef SIRAH_POD_H
#define SIRAH_POD_H

#include "common.h"
#include "affinity.h"  // Include for affinity and toleration types

// Container definition
typedef struct {
    char* name;
    char* image;
    char* image_pull_policy;  // "Always", "IfNotPresent", "Never"
    
    char** command;            // Entrypoint override
    int num_command_args;
    
    char** args;               // Command arguments
    int num_args;
    
    char** env_names;          // Environment variables
    char** env_values;
    int num_env_vars;
    
    int cpu_millicores;
    int memory_bytes;
    int storage_bytes;
    
    char** ports;              // Port numbers (e.g., "8080/TCP")
    int num_ports;
    
    char* working_dir;
    char* stdin_policy;        // stdin: true/false
    char* tty_policy;          // tty: true/false
} k8s_container_t;

// Volume definition in pod spec
typedef struct {
    char* name;
    char* type;                // "emptyDir", "hostPath", "persistentVolumeClaim"
    char* path;                // For hostPath
    char* pvc_name;            // For persistentVolumeClaim
    char* storage_class;       // Storage class name
} k8s_volume_t;

// Volume mount
typedef struct {
    char* name;
    char* mount_path;
    int read_only;
} k8s_volume_mount_t;

// Pod spec
typedef struct {
    k8s_container_t* containers;
    int num_containers;
    
    k8s_volume_t* volumes;
    int num_volumes;
    
    char* node_name;           // Assigned node (set by scheduler)
    char* service_account;
    char* restart_policy;      // "Always", "OnFailure", "Never"
    
    int termination_grace_period_seconds;
    char* dns_policy;          // "ClusterFirst", "HostNetwork"
    char* host_network;
    
    char** node_selectors;     // Label selectors for node affinity
    int num_node_selectors;
    
    k8s_affinity_t* affinity;      // Pod affinity rules
    k8s_toleration_t* tolerations; // Toleration rules
    int num_tolerations;
} k8s_pod_spec_t;

// Pod status
typedef struct {
    k8s_phase_t phase;
    char* host_ip;
    char* pod_ip;
    
    k8s_condition_t* conditions;
    int num_conditions;
    
    struct {
        char* container_id;
        char* image_id;
        k8s_phase_t state;     // running, waiting, terminated
        int exit_code;
        char* reason;
        char* message;
    } container_statuses[32];
    int num_container_statuses;
    
    time_t start_time;
} k8s_pod_status_t;

// Full Pod object
typedef struct {
    k8s_metadata_t metadata;
    k8s_pod_spec_t spec;
    k8s_pod_status_t status;
} k8s_pod_t;

// Pod operations
k8s_pod_t* k8s_pod_new(const char* name, const char* namespace);
void k8s_pod_free(k8s_pod_t* pod);
char* k8s_pod_to_json(k8s_pod_t* pod);
k8s_pod_t* k8s_pod_from_json(const char* json_str);

int k8s_pod_add_container(k8s_pod_t* pod, k8s_container_t* container);
int k8s_pod_set_image(k8s_pod_t* pod, const char* image);
int k8s_pod_set_node(k8s_pod_t* pod, const char* node_name);

// Volume operations
int k8s_pod_add_volume(k8s_pod_t* pod, const char* name, const char* type);
int k8s_pod_add_pvc_volume(k8s_pod_t* pod, const char* name, const char* pvc_name);
int k8s_pod_add_hostpath_volume(k8s_pod_t* pod, const char* name, const char* path);
int k8s_pod_add_emptydir_volume(k8s_pod_t* pod, const char* name);

#endif
