#ifndef SIRAH_AGENT_CONFIG_H
#define SIRAH_AGENT_CONFIG_H

typedef struct {
    // Control Plane
    char* api_server_url;
    
    // Node Identity
    char* node_name;
    char* node_ip;
    char* pod_cidr;
    
    // Kubernetes Defaults
    int max_pods;
    long memory_bytes;
    int cpu_millicores;
    
    // Communication Intervals (seconds)
    int node_register_interval;
    int heartbeat_interval;
    int pod_sync_interval;
    int health_check_interval;
    
    // Pod Restart Policy
    int max_pod_restarts;
    int restart_backoff_ms;
    
    // Network Configuration
    char* cluster_dns_ip;
    char* service_cidr;
    
    // Container Runtime
    char* vm_runtime;
    int vm_memory_mb;
    int vm_cpus;
    char* qemu_path;
    
    // Logging
    char* log_level;
    char* log_file;
} agent_config_t;

// Configuration lifecycle
agent_config_t* agent_config_new(void);
void agent_config_free(agent_config_t* config);

// Configuration loading
int agent_config_load_file(agent_config_t* config, const char* path);
int agent_config_load_env(agent_config_t* config);
int agent_config_load_args(agent_config_t* config, int argc, char* argv[]);

// Configuration validation
int agent_config_validate(agent_config_t* config);
void agent_config_print(agent_config_t* config);

// Configuration access
const char* agent_config_get_string(agent_config_t* config, const char* key);
int agent_config_get_int(agent_config_t* config, const char* key);
long agent_config_get_long(agent_config_t* config, const char* key);
int agent_config_set_string(agent_config_t* config, const char* key, const char* value);
int agent_config_set_int(agent_config_t* config, const char* key, int value);

#endif // SIRAH_AGENT_CONFIG_H
