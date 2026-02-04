#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// Configuration creation with defaults
agent_config_t* agent_config_new(void) {
    agent_config_t* config = (agent_config_t*)malloc(sizeof(agent_config_t));
    if (!config) return NULL;
    
    memset(config, 0, sizeof(agent_config_t));
    
    // Set Kubernetes defaults
    config->api_server_url = strdup("http://localhost:6443");
    config->node_name = strdup("worker-1");
    config->node_ip = strdup("127.0.0.1");
    config->pod_cidr = strdup("10.0.1.0/24");
    
    // Kubernetes node capacity defaults
    config->max_pods = 110;
    config->memory_bytes = 4294967296;  // 4GB
    config->cpu_millicores = 2000;
    
    // Communication intervals
    config->node_register_interval = 60;
    config->heartbeat_interval = 10;
    config->pod_sync_interval = 5;
    config->health_check_interval = 10;
    
    // Pod restart policy
    config->max_pod_restarts = 5;
    config->restart_backoff_ms = 5000;
    
    // Network defaults
    config->cluster_dns_ip = strdup("10.96.0.10");
    config->service_cidr = strdup("10.96.0.0/12");
    
    // Container runtime
    config->vm_runtime = strdup("qemu");
    config->vm_memory_mb = 512;
    config->vm_cpus = 1;
    config->qemu_path = strdup("/usr/bin/qemu-system-x86_64");
    
    // Logging
    config->log_level = strdup("info");
    config->log_file = strdup("/var/log/sirah-kubelet.log");
    
    return config;
}

void agent_config_free(agent_config_t* config) {
    if (!config) return;
    
    free(config->api_server_url);
    free(config->node_name);
    free(config->node_ip);
    free(config->pod_cidr);
    free(config->cluster_dns_ip);
    free(config->service_cidr);
    free(config->vm_runtime);
    free(config->qemu_path);
    free(config->log_level);
    free(config->log_file);
    free(config);
}

// Load configuration from file (key=value format)
int agent_config_load_file(agent_config_t* config, const char* path) {
    if (!config || !path) return -1;
    
    FILE* f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Cannot open config file: %s\n", path);
        return -1;
    }
    
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n') continue;
        
        // Parse key=value
        char* eq = strchr(line, '=');
        if (!eq) continue;
        
        // Trim whitespace
        int len = strlen(line);
        if (line[len-1] == '\n') line[len-1] = 0;
        
        *eq = 0;
        char* key = line;
        char* value = eq + 1;
        
        // Strip leading/trailing whitespace from value
        while (isspace(*value)) value++;
        int vlen = strlen(value);
        while (vlen > 0 && isspace(value[vlen-1])) value[--vlen] = 0;
        
        agent_config_set_string(config, key, value);
    }
    
    fclose(f);
    return 0;
}

// Load configuration from environment variables (SIRAH_* prefix)
int agent_config_load_env(agent_config_t* config) {
    if (!config) return -1;
    
    const char* env_vars[] = {
        "API_SERVER_URL",
        "NODE_NAME",
        "NODE_IP",
        "POD_CIDR",
        "MAX_PODS",
        "MEMORY_BYTES",
        "CPU_MILLICORES",
        "HEARTBEAT_INTERVAL",
        "POD_SYNC_INTERVAL",
        "HEALTH_CHECK_INTERVAL",
        "LOG_LEVEL",
        NULL
    };
    
    for (int i = 0; env_vars[i]; i++) {
        char env_key[128];
        snprintf(env_key, sizeof(env_key), "SIRAH_%s", env_vars[i]);
        
        const char* value = getenv(env_key);
        if (value) {
            agent_config_set_string(config, env_vars[i], value);
        }
    }
    
    return 0;
}

// Load configuration from command-line arguments (--key=value format)
int agent_config_load_args(agent_config_t* config, int argc, char* argv[]) {
    if (!config) return -1;
    
    for (int i = 1; i < argc; i++) {
        char* arg = argv[i];
        
        // Skip non-option arguments
        if (arg[0] != '-' || arg[1] != '-') continue;
        
        // Skip leading dashes
        arg += 2;
        
        // Parse --key=value
        char* eq = strchr(arg, '=');
        if (!eq) {
            // Handle flags like --help, --version
            if (strcmp(arg, "help") == 0) {
                printf("Usage: sirah-kubelet [OPTIONS]\n");
                printf("Options:\n");
                printf("  --api-server=URL      Control plane API URL\n");
                printf("  --node-name=NAME      Node name\n");
                printf("  --node-ip=IP          Node IP address\n");
                printf("  --pod-cidr=CIDR       Pod CIDR range\n");
                printf("  --max-pods=NUM        Maximum pods per node\n");
                printf("  --config-file=PATH    Load config from file\n");
                printf("  --help                Show this help\n");
                printf("  --version             Show version\n");
                return 1;  // Signal to exit
            }
            continue;
        }
        
        // Parse key=value
        *eq = 0;
        char* key = arg;
        char* value = eq + 1;
        
        agent_config_set_string(config, key, value);
    }
    
    return 0;
}

// Validate required configuration
int agent_config_validate(agent_config_t* config) {
    if (!config) return -1;
    
    int errors = 0;
    
    if (!config->api_server_url || strlen(config->api_server_url) == 0) {
        fprintf(stderr, "Error: api_server_url is required\n");
        errors++;
    }
    
    if (!config->node_name || strlen(config->node_name) == 0) {
        fprintf(stderr, "Error: node_name is required\n");
        errors++;
    }
    
    if (!config->node_ip || strlen(config->node_ip) == 0) {
        fprintf(stderr, "Error: node_ip is required\n");
        errors++;
    }
    
    if (!config->pod_cidr || strlen(config->pod_cidr) == 0) {
        fprintf(stderr, "Error: pod_cidr is required\n");
        errors++;
    }
    
    return errors > 0 ? -1 : 0;
}

// Print configuration for debugging
void agent_config_print(agent_config_t* config) {
    if (!config) return;
    
    printf("Agent Configuration:\n");
    printf("  api_server_url: %s\n", config->api_server_url);
    printf("  node_name: %s\n", config->node_name);
    printf("  node_ip: %s\n", config->node_ip);
    printf("  pod_cidr: %s\n", config->pod_cidr);
    printf("  max_pods: %d\n", config->max_pods);
    printf("  memory_bytes: %ld\n", config->memory_bytes);
    printf("  cpu_millicores: %d\n", config->cpu_millicores);
    printf("  heartbeat_interval: %d\n", config->heartbeat_interval);
    printf("  pod_sync_interval: %d\n", config->pod_sync_interval);
    printf("  log_level: %s\n", config->log_level);
    printf("\n");
}

// Configuration access (string)
const char* agent_config_get_string(agent_config_t* config, const char* key) {
    if (!config || !key) return NULL;
    
    if (strcmp(key, "api_server_url") == 0) return config->api_server_url;
    if (strcmp(key, "API_SERVER_URL") == 0) return config->api_server_url;
    if (strcmp(key, "node_name") == 0) return config->node_name;
    if (strcmp(key, "NODE_NAME") == 0) return config->node_name;
    if (strcmp(key, "node_ip") == 0) return config->node_ip;
    if (strcmp(key, "NODE_IP") == 0) return config->node_ip;
    if (strcmp(key, "pod_cidr") == 0) return config->pod_cidr;
    if (strcmp(key, "POD_CIDR") == 0) return config->pod_cidr;
    if (strcmp(key, "cluster_dns_ip") == 0) return config->cluster_dns_ip;
    if (strcmp(key, "service_cidr") == 0) return config->service_cidr;
    if (strcmp(key, "vm_runtime") == 0) return config->vm_runtime;
    if (strcmp(key, "qemu_path") == 0) return config->qemu_path;
    if (strcmp(key, "log_level") == 0) return config->log_level;
    if (strcmp(key, "LOG_LEVEL") == 0) return config->log_level;
    if (strcmp(key, "log_file") == 0) return config->log_file;
    
    return NULL;
}

// Configuration access (int)
int agent_config_get_int(agent_config_t* config, const char* key) {
    if (!config || !key) return -1;
    
    if (strcmp(key, "max_pods") == 0 || strcmp(key, "MAX_PODS") == 0)
        return config->max_pods;
    if (strcmp(key, "cpu_millicores") == 0 || strcmp(key, "CPU_MILLICORES") == 0)
        return config->cpu_millicores;
    if (strcmp(key, "heartbeat_interval") == 0 || strcmp(key, "HEARTBEAT_INTERVAL") == 0)
        return config->heartbeat_interval;
    if (strcmp(key, "pod_sync_interval") == 0 || strcmp(key, "POD_SYNC_INTERVAL") == 0)
        return config->pod_sync_interval;
    if (strcmp(key, "health_check_interval") == 0 || strcmp(key, "HEALTH_CHECK_INTERVAL") == 0)
        return config->health_check_interval;
    if (strcmp(key, "max_pod_restarts") == 0 || strcmp(key, "MAX_POD_RESTARTS") == 0)
        return config->max_pod_restarts;
    if (strcmp(key, "restart_backoff_ms") == 0 || strcmp(key, "RESTART_BACKOFF_MS") == 0)
        return config->restart_backoff_ms;
    if (strcmp(key, "vm_memory_mb") == 0 || strcmp(key, "VM_MEMORY_MB") == 0)
        return config->vm_memory_mb;
    if (strcmp(key, "vm_cpus") == 0 || strcmp(key, "VM_CPUS") == 0)
        return config->vm_cpus;
    
    return -1;
}

// Configuration access (long)
long agent_config_get_long(agent_config_t* config, const char* key) {
    if (!config || !key) return -1;
    
    if (strcmp(key, "memory_bytes") == 0 || strcmp(key, "MEMORY_BYTES") == 0)
        return config->memory_bytes;
    
    return -1;
}

// Configuration modification (string)
int agent_config_set_string(agent_config_t* config, const char* key, const char* value) {
    if (!config || !key || !value) return -1;
    
    if (strcmp(key, "api_server_url") == 0 || strcmp(key, "API_SERVER_URL") == 0) {
        free(config->api_server_url);
        config->api_server_url = strdup(value);
    } else if (strcmp(key, "node_name") == 0 || strcmp(key, "NODE_NAME") == 0) {
        free(config->node_name);
        config->node_name = strdup(value);
    } else if (strcmp(key, "node_ip") == 0 || strcmp(key, "NODE_IP") == 0) {
        free(config->node_ip);
        config->node_ip = strdup(value);
    } else if (strcmp(key, "pod_cidr") == 0 || strcmp(key, "POD_CIDR") == 0) {
        free(config->pod_cidr);
        config->pod_cidr = strdup(value);
    } else if (strcmp(key, "log_level") == 0 || strcmp(key, "LOG_LEVEL") == 0) {
        free(config->log_level);
        config->log_level = strdup(value);
    } else {
        return -1;
    }
    
    return 0;
}

// Configuration modification (int)
int agent_config_set_int(agent_config_t* config, const char* key, int value) {
    if (!config || !key) return -1;
    
    if (strcmp(key, "max_pods") == 0 || strcmp(key, "MAX_PODS") == 0) {
        config->max_pods = value;
    } else if (strcmp(key, "cpu_millicores") == 0 || strcmp(key, "CPU_MILLICORES") == 0) {
        config->cpu_millicores = value;
    } else if (strcmp(key, "heartbeat_interval") == 0 || strcmp(key, "HEARTBEAT_INTERVAL") == 0) {
        config->heartbeat_interval = value;
    } else if (strcmp(key, "pod_sync_interval") == 0 || strcmp(key, "POD_SYNC_INTERVAL") == 0) {
        config->pod_sync_interval = value;
    } else {
        return -1;
    }
    
    return 0;
}
