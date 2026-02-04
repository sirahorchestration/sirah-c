// internal/agent/config.h
// Agent Configuration Management - Load, parse, and manage agent configuration

#ifndef SIRAH_AGENT_CONFIG_H
#define SIRAH_AGENT_CONFIG_H

// ============ Type Definitions ============

// Agent configuration
typedef struct {
    // Control plane
    char api_server_url[512];
    
    // Node identity
    char node_name[256];
    char node_ip[16];
    
    // Networking
    char pod_cidr[32];              // e.g., "10.0.1.0/24"
    char cluster_dns_ip[16];        // Cluster DNS server IP
    char service_cidr[32];          // Service CIDR for routing
    
    // Kubelet-like configuration
    int max_pods;                   // Max pods per node (default: 110)
    long memory_bytes;              // Total memory (default: 4GB)
    long cpu_millicores;            // Total CPU (default: 2000m)
    
    // Intervals (seconds)
    int node_register_interval;     // Register node with control plane (default: 60)
    int heartbeat_interval;         // Send heartbeat (default: 10)
    int pod_sync_interval;          // Sync pods from control plane (default: 5)
    int health_check_interval;      // Check pod health (default: 10)
    int metrics_interval;           // Collect metrics (default: 15)
    
    // Pod restart policy
    int max_pod_restarts;           // Max restarts per pod (default: 5)
    int pod_restart_backoff_ms;     // Backoff between restarts (default: 5000)
    
    // VM/QEMU configuration
    char vm_runtime[32];            // "qemu", "kvm", "container" (default: "qemu")
    int vm_memory_mb;               // Memory per VM (default: 512)
    int vm_cpus;                    // CPUs per VM (default: 1)
    char qemu_path[512];            // Path to qemu-system-x86_64
    
    // Logging
    char log_level[16];             // "debug", "info", "warn", "error" (default: "info")
    char log_file[512];             // Log file path (default: stderr)
    
    // TLS/Security
    int use_tls;                    // Use TLS for API communication (default: 1)
    char ca_cert_path[512];         // CA certificate path
    char cert_path[512];            // Client certificate path
    char key_path[512];             // Client key path
} agent_config_t;

// ============ Configuration Lifecycle ============

// Create default agent configuration
agent_config_t* agent_config_new(void);

// Free configuration
void agent_config_free(agent_config_t* config);

// Load configuration from file
int agent_config_load_file(agent_config_t* config, const char* config_file);

// Load configuration from environment variables
int agent_config_load_env(agent_config_t* config);

// Load configuration from command line arguments
int agent_config_load_args(agent_config_t* config, int argc, char* argv[]);

// Validate configuration
int agent_config_validate(agent_config_t* config);

// Print configuration (for debugging)
void agent_config_print(agent_config_t* config);

// ============ Configuration Access ============

// Get configuration value as string
const char* agent_config_get_string(agent_config_t* config,
                                   const char* key);

// Get configuration value as integer
int agent_config_get_int(agent_config_t* config,
                        const char* key);

// Get configuration value as long
long agent_config_get_long(agent_config_t* config,
                          const char* key);

// Set configuration value
int agent_config_set(agent_config_t* config,
                    const char* key,
                    const char* value);

// ============ Configuration Sources ============

// Parse YAML/JSON configuration file
int agent_config_parse_file(const char* path, agent_config_t* out_config);

// Parse environment variables (SIRAH_* prefix)
int agent_config_parse_env(agent_config_t* out_config);

// Parse command line arguments (--key=value format)
int agent_config_parse_args(int argc, char* argv[], agent_config_t* out_config);

#endif // SIRAH_AGENT_CONFIG_H
