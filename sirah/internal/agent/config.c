// internal/agent/config.c
// Agent Configuration Management Implementation

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

// ============ Configuration Lifecycle ============

agent_config_t* agent_config_new(void) {
    agent_config_t* config = calloc(1, sizeof(agent_config_t));
    if (!config) return NULL;

    // Set defaults
    strcpy(config->api_server_url, "http://localhost:6443");
    strcpy(config->node_name, "worker-1");
    strcpy(config->node_ip, "127.0.0.1");
    strcpy(config->pod_cidr, "10.0.1.0/24");
    strcpy(config->cluster_dns_ip, "10.96.0.10");
    strcpy(config->service_cidr, "10.96.0.0/12");

    config->max_pods = 110;
    config->memory_bytes = 4L * 1024 * 1024 * 1024;  // 4GB
    config->cpu_millicores = 2000;                   // 2 CPUs

    config->node_register_interval = 60;
    config->heartbeat_interval = 10;
    config->pod_sync_interval = 5;
    config->health_check_interval = 10;
    config->metrics_interval = 15;

    config->max_pod_restarts = 5;
    config->pod_restart_backoff_ms = 5000;

    strcpy(config->vm_runtime, "qemu");
    config->vm_memory_mb = 512;
    config->vm_cpus = 1;
    strcpy(config->qemu_path, "/usr/bin/qemu-system-x86_64");

    strcpy(config->log_level, "info");
    strcpy(config->use_tls, "1");

    return config;
}

void agent_config_free(agent_config_t* config) {
    if (!config) return;
    free(config);
}

int agent_config_load_file(agent_config_t* config, const char* config_file) {
    if (!config || !config_file) return -1;

    printf("Loading configuration from file: %s\n", config_file);

    FILE* f = fopen(config_file, "r");
    if (!f) {
        printf("WARNING: Could not open config file, using defaults\n");
        return -1;
    }

    // Simple line-by-line parsing
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n') continue;

        // Parse key=value
        char* eq = strchr(line, '=');
        if (!eq) continue;

        char key[256], value[256];
        sscanf(line, "%255[^=]=%255[^\n]", key, value);

        agent_config_set(config, key, value);
    }

    fclose(f);
    return 0;
}

int agent_config_load_env(agent_config_t* config) {
    if (!config) return -1;

    printf("Loading configuration from environment variables\n");

    // Load from environment (SIRAH_* prefix)
    const char* api_url = getenv("SIRAH_API_SERVER_URL");
    if (api_url) strcpy(config->api_server_url, api_url);

    const char* node_name = getenv("SIRAH_NODE_NAME");
    if (node_name) strcpy(config->node_name, node_name);

    const char* node_ip = getenv("SIRAH_NODE_IP");
    if (node_ip) strcpy(config->node_ip, node_ip);

    const char* pod_cidr = getenv("SIRAH_POD_CIDR");
    if (pod_cidr) strcpy(config->pod_cidr, pod_cidr);

    return 0;
}

int agent_config_load_args(agent_config_t* config, int argc, char* argv[]) {
    if (!config) return -1;

    printf("Loading configuration from command line arguments\n");

    for (int i = 1; i < argc; i++) {
        char* arg = argv[i];
        if (arg[0] != '-') continue;

        // Skip --prefix
        if (arg[1] == '-') arg += 2;
        else arg += 1;

        // Parse --key=value
        char* eq = strchr(arg, '=');
        if (!eq) continue;

        char key[256], value[256];
        sscanf(arg, "%255[^=]=%255s", key, value);

        agent_config_set(config, key, value);
    }

    return 0;
}

int agent_config_validate(agent_config_t* config) {
    if (!config) return -1;

    printf("Validating configuration...\n");

    // Validate required fields
    if (strlen(config->api_server_url) == 0) {
        printf("ERROR: api_server_url not set\n");
        return -1;
    }

    if (strlen(config->node_name) == 0) {
        printf("ERROR: node_name not set\n");
        return -1;
    }

    if (strlen(config->node_ip) == 0) {
        printf("ERROR: node_ip not set\n");
        return -1;
    }

    if (strlen(config->pod_cidr) == 0) {
        printf("ERROR: pod_cidr not set\n");
        return -1;
    }

    printf("Configuration validation passed\n");
    return 0;
}

void agent_config_print(agent_config_t* config) {
    if (!config) return;

    printf("=== Agent Configuration ===\n");
    printf("API Server URL: %s\n", config->api_server_url);
    printf("Node Name: %s\n", config->node_name);
    printf("Node IP: %s\n", config->node_ip);
    printf("Pod CIDR: %s\n", config->pod_cidr);
    printf("Cluster DNS IP: %s\n", config->cluster_dns_ip);
    printf("Service CIDR: %s\n", config->service_cidr);
    printf("Max Pods: %d\n", config->max_pods);
    printf("Memory: %ld bytes\n", config->memory_bytes);
    printf("CPU: %ld millicores\n", config->cpu_millicores);
    printf("Heartbeat Interval: %d s\n", config->heartbeat_interval);
    printf("Pod Sync Interval: %d s\n", config->pod_sync_interval);
    printf("VM Runtime: %s\n", config->vm_runtime);
    printf("Log Level: %s\n", config->log_level);
    printf("==========================\n");
}

// ============ Configuration Access ============

const char* agent_config_get_string(agent_config_t* config,
                                   const char* key) {
    if (!config || !key) return NULL;

    if (strcmp(key, "api_server_url") == 0) return config->api_server_url;
    if (strcmp(key, "node_name") == 0) return config->node_name;
    if (strcmp(key, "node_ip") == 0) return config->node_ip;
    if (strcmp(key, "pod_cidr") == 0) return config->pod_cidr;
    if (strcmp(key, "cluster_dns_ip") == 0) return config->cluster_dns_ip;
    if (strcmp(key, "service_cidr") == 0) return config->service_cidr;
    if (strcmp(key, "vm_runtime") == 0) return config->vm_runtime;
    if (strcmp(key, "qemu_path") == 0) return config->qemu_path;
    if (strcmp(key, "log_level") == 0) return config->log_level;
    if (strcmp(key, "log_file") == 0) return config->log_file;

    return NULL;
}

int agent_config_get_int(agent_config_t* config, const char* key) {
    if (!config || !key) return 0;

    if (strcmp(key, "max_pods") == 0) return config->max_pods;
    if (strcmp(key, "heartbeat_interval") == 0) return config->heartbeat_interval;
    if (strcmp(key, "pod_sync_interval") == 0) return config->pod_sync_interval;
    if (strcmp(key, "health_check_interval") == 0) return config->health_check_interval;
    if (strcmp(key, "max_pod_restarts") == 0) return config->max_pod_restarts;
    if (strcmp(key, "vm_memory_mb") == 0) return config->vm_memory_mb;
    if (strcmp(key, "vm_cpus") == 0) return config->vm_cpus;
    if (strcmp(key, "use_tls") == 0) return config->use_tls;

    return 0;
}

long agent_config_get_long(agent_config_t* config, const char* key) {
    if (!config || !key) return 0;

    if (strcmp(key, "memory_bytes") == 0) return config->memory_bytes;
    if (strcmp(key, "cpu_millicores") == 0) return config->cpu_millicores;

    return 0;
}

int agent_config_set(agent_config_t* config,
                    const char* key,
                    const char* value) {
    if (!config || !key || !value) return -1;

    if (strcmp(key, "api_server_url") == 0) {
        strncpy(config->api_server_url, value, sizeof(config->api_server_url) - 1);
    } else if (strcmp(key, "node_name") == 0) {
        strncpy(config->node_name, value, sizeof(config->node_name) - 1);
    } else if (strcmp(key, "node_ip") == 0) {
        strncpy(config->node_ip, value, sizeof(config->node_ip) - 1);
    } else if (strcmp(key, "pod_cidr") == 0) {
        strncpy(config->pod_cidr, value, sizeof(config->pod_cidr) - 1);
    } else if (strcmp(key, "max_pods") == 0) {
        config->max_pods = atoi(value);
    } else if (strcmp(key, "heartbeat_interval") == 0) {
        config->heartbeat_interval = atoi(value);
    } else if (strcmp(key, "pod_sync_interval") == 0) {
        config->pod_sync_interval = atoi(value);
    } else if (strcmp(key, "vm_runtime") == 0) {
        strncpy(config->vm_runtime, value, sizeof(config->vm_runtime) - 1);
    } else if (strcmp(key, "log_level") == 0) {
        strncpy(config->log_level, value, sizeof(config->log_level) - 1);
    } else {
        return -1;
    }

    return 0;
}
