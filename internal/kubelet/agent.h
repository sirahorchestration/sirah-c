#ifndef SIRAH_AGENT_H
#define SIRAH_AGENT_H

#include <time.h>
#include <pthread.h>

typedef struct {
    char* name;
    char* namespace;
    char* status_phase;  // Pending, Running, Succeeded, Failed
    char* pod_ip;
    int restart_count;
    time_t created_at;
    time_t started_at;
} managed_pod_t;

typedef struct {
    int pid;
    char* status;  // Creating, Running, Exited, Failed
    int exit_code;
    int restart_count;
    time_t started_at;
} vm_instance_t;

typedef struct {
    char* node_name;
    char* node_ip;
    char* pod_cidr;
    char* status;  // NotReady, Ready
    int capacity_pods;
    int allocated_pods;
    int allocatable_cpu;
    int allocatable_memory;
    time_t last_heartbeat;
} node_info_t;

typedef struct {
    char* api_server_url;
    node_info_t* node_info;
    
    // Pod management
    managed_pod_t* pods;
    int pod_count;
    int pod_capacity;
    
    // VM instances
    vm_instance_t* instances;
    int instance_count;
    
    // Configuration
    int heartbeat_interval;
    int pod_sync_interval;
    int max_pod_restarts;
    
    // State
    int running;
    pthread_mutex_t lock;
} agent_t;

// Agent lifecycle
agent_t* agent_new(const char* api_url, const char* node_name, 
                   const char* node_ip, const char* pod_cidr);
void agent_free(agent_t* agent);
int agent_init(agent_t* agent);
int agent_shutdown(agent_t* agent);
int agent_run(agent_t* agent);

// Node management
int agent_register_node(agent_t* agent);
int agent_send_heartbeat(agent_t* agent);

// Pod management
int agent_sync_pods(agent_t* agent);
int agent_create_pod(agent_t* agent, const char* pod_json);
int agent_start_pod(agent_t* agent, const char* pod_name);
int agent_stop_pod(agent_t* agent, const char* pod_name);
int agent_restart_pod(agent_t* agent, const char* pod_name);
int agent_update_pod_status(agent_t* agent, const char* pod_name);

// IP allocation
char* agent_allocate_pod_ip(agent_t* agent);
void agent_release_pod_ip(agent_t* agent, const char* ip);

// Component functions for supervision
int agent_pod_sync_component(void* context);
int agent_heartbeat_component(void* context);
int agent_health_check_component(void* context);

#endif // SIRAH_AGENT_H
