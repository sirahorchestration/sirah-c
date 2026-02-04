// internal/agent/agent.h
// Sirah Worker Agent - Node-local agent for pod lifecycle management
// Runs on each worker node, manages pods, communicates with control plane API

#ifndef SIRAH_AGENT_H
#define SIRAH_AGENT_H

#include <time.h>
#include <json-c/json.h>

// ============ Type Definitions ============

// Agent node information
typedef struct {
    char node_name[256];        // Node identifier
    char node_ip[16];           // Node IP address
    char pod_cidr[32];          // Pod CIDR for this node (e.g., "10.0.1.0/24")
    int pod_cidr_start;         // First IP in pod CIDR
    int pod_cidr_end;           // Last IP in pod CIDR
    
    // Node capacity
    int max_pods;               // Max pods on this node (default: 110)
    long memory_bytes;          // Total memory available
    long cpu_millicores;        // Total CPU available
    
    // Node status
    int ready;                  // 1 if node is ready, 0 otherwise
    char status[32];            // "Ready", "NotReady", "Unknown"
    time_t last_heartbeat;      // When we last sent heartbeat
} node_info_t;

// Managed pod state
typedef struct {
    char pod_name[256];
    char namespace[256];
    char phase[16];             // "Pending", "Running", "Succeeded", "Failed"
    char pod_ip[16];            // Assigned pod IP
    int pid;                    // Process ID of pod container
    int restart_count;
    time_t created_at;
    time_t last_updated;
} managed_pod_t;

// VM/container runtime instance
typedef struct {
    char pod_name[256];
    char namespace[256];
    int pid;                    // Process ID
    char container_id[256];     // Container/QEMU instance ID
    char status[16];            // "running", "stopped", "error"
    time_t started_at;
    int exit_code;
    int restart_count;
} vm_instance_t;

// Main agent structure
typedef struct {
    char* api_server_url;
    void* curl_handle;
    
    node_info_t node_info;
    
    // Pod management state
    managed_pod_t* pods;
    int num_pods;
    int max_pods_capacity;
    
    // VM/container runtime state
    vm_instance_t* vms;
    int num_vms;
    int max_vms_capacity;
    
    // Agent control
    int running;
    int ready;
    
    // Configuration
    int heartbeat_interval;     // Seconds between heartbeats (default: 10)
    int pod_sync_interval;      // Seconds between pod reconciliation (default: 5)
    int max_pod_restarts;       // Max restarts per pod (default: 5)
    int pod_restart_backoff;    // Backoff in seconds (default: 5)
    
    // Statistics
    int total_pods_created;
    int total_pods_deleted;
    int total_restarts;
    time_t started_at;
} agent_t;

// ============ Lifecycle Functions ============

// Create new agent
agent_t* agent_new(const char* api_server_url,
                   const char* node_name,
                   const char* node_ip,
                   const char* pod_cidr);

// Free agent
void agent_free(agent_t* agent);

// Initialize agent (register with control plane, get initial config)
int agent_init(agent_t* agent);

// Shutdown agent gracefully
void agent_shutdown(agent_t* agent);

// Run agent main loop (blocking)
int agent_run(agent_t* agent);

// ============ Control Plane Communication ============

// Register node with control plane API
int agent_register_node(agent_t* agent);

// Send heartbeat to control plane
int agent_send_heartbeat(agent_t* agent);

// Fetch pods assigned to this node from control plane
int agent_sync_pods(agent_t* agent);

// Update pod status on control plane
int agent_update_pod_status(agent_t* agent,
                           const char* pod_name,
                           const char* namespace,
                           const char* phase,
                           const char* pod_ip);

// Delete pod from node
int agent_delete_pod(agent_t* agent,
                    const char* pod_name,
                    const char* namespace);

// ============ Pod Lifecycle Management ============

// Create a new pod on this node
int agent_create_pod(agent_t* agent,
                    json_object* pod_spec);

// Start pod container/VM
int agent_start_pod(agent_t* agent,
                   const char* pod_name,
                   const char* namespace);

// Stop pod container/VM
int agent_stop_pod(agent_t* agent,
                  const char* pod_name,
                  const char* namespace);

// Restart pod with backoff
int agent_restart_pod(agent_t* agent,
                     const char* pod_name,
                     const char* namespace);

// Check pod health (via health probes)
int agent_check_pod_health(agent_t* agent,
                          const char* pod_name,
                          const char* namespace);

// ============ VM/Container Runtime Management ============

// Start a VM instance for a pod
int agent_spawn_vm_instance(agent_t* agent,
                           const char* pod_name,
                           const char* namespace,
                           json_object* pod_spec);

// Stop a VM instance
int agent_stop_vm_instance(agent_t* agent,
                          const char* container_id);

// Monitor VM process
int agent_monitor_vm_process(agent_t* agent,
                            const char* container_id,
                            int* out_exit_code);

// ============ IP Address Management ============

// Allocate pod IP from node's CIDR
int agent_allocate_pod_ip(agent_t* agent,
                         char* out_pod_ip);

// Release pod IP back to pool
int agent_release_pod_ip(agent_t* agent,
                        const char* pod_ip);

// ============ Node Management ============

// Mark node as ready
int agent_mark_node_ready(agent_t* agent);

// Mark node as not ready
int agent_mark_node_not_ready(agent_t* agent);

// Drain node (stop accepting new pods)
int agent_drain_node(agent_t* agent);

// Update node status on control plane
int agent_update_node_status(agent_t* agent);

// Get node info
node_info_t* agent_get_node_info(agent_t* agent);

// ============ Pod Query Functions ============

// Get pod by name
managed_pod_t* agent_get_pod(agent_t* agent,
                            const char* pod_name,
                            const char* namespace);

// List all pods on this node
managed_pod_t** agent_list_pods(agent_t* agent,
                               int* out_count);

// Get pod count
int agent_get_pod_count(agent_t* agent);

// ============ Utility Functions ============

// Convert uint32 IP to string
void agent_uint32_to_ip_string(unsigned int ip, char* out_str);

// Convert string IP to uint32
unsigned int agent_ip_string_to_uint32(const char* ip_str);

// Get next available pod IP from CIDR
char* agent_get_next_pod_ip(agent_t* agent);

#endif // SIRAH_AGENT_H
