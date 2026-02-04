// cmd/agent/main.c
// Sirah Worker Agent - Main Entry Point
// Standalone agent binary for running on worker nodes

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "../../internal/agent/agent.h"
#include "../../internal/agent/config.h"
#include "../../internal/agent/supervision.h"

// Global agent for signal handler
agent_t* g_agent = NULL;

// Signal handler for graceful shutdown
void signal_handler(int sig) {
    printf("\nReceived signal %d, shutting down agent...\n", sig);
    if (g_agent) {
        agent_shutdown(g_agent);
    }
}

// Pod sync component function (for supervision)
int agent_pod_sync_component(void* context) {
    agent_t* agent = (agent_t*)context;
    if (!agent) return -1;

    // Run pod sync once
    int result = agent_sync_pods(agent);
    
    // Sleep for the configured interval
    sleep(agent->pod_sync_interval);

    return result;
}

// Heartbeat component function (for supervision)
int agent_heartbeat_component(void* context) {
    agent_t* agent = (agent_t*)context;
    if (!agent) return -1;

    // Send heartbeat
    int result = agent_send_heartbeat(agent);

    // Sleep for the configured interval
    sleep(agent->heartbeat_interval);

    return result;
}

// Health check component function (for supervision)
int agent_health_check_component(void* context) {
    agent_t* agent = (agent_t*)context;
    if (!agent) return -1;

    // Check all pods
    managed_pod_t** pods = agent_list_pods(agent, NULL);
    int pod_count = agent_get_pod_count(agent);

    for (int i = 0; i < pod_count; i++) {
        agent_check_pod_health(agent, pods[i]->pod_name, pods[i]->namespace);
    }

    sleep(10);  // Check every 10 seconds
    return 0;
}

// Print usage information
void print_usage(const char* program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("\nOptions:\n");
    printf("  --api-server=URL        API server URL (default: http://localhost:6443)\n");
    printf("  --node-name=NAME        Node name (default: worker-1)\n");
    printf("  --node-ip=IP            Node IP address (default: 127.0.0.1)\n");
    printf("  --pod-cidr=CIDR         Pod CIDR (default: 10.0.1.0/24)\n");
    printf("  --max-pods=NUM          Max pods per node (default: 110)\n");
    printf("  --config-file=PATH      Load configuration from file\n");
    printf("  --help                  Print this help message\n");
    printf("  --version               Print version information\n");
}

int main(int argc, char* argv[]) {
    printf("=== Sirah Worker Agent v1.28 ===\n");
    printf("Starting worker node agent...\n\n");

    // Parse configuration
    agent_config_t* config = agent_config_new();
    if (!config) {
        fprintf(stderr, "ERROR: Failed to create config\n");
        return 1;
    }

    // Load configuration in order: defaults -> file -> env -> args
    
    // Check for config file
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--config-file=", 14) == 0) {
            agent_config_load_file(config, argv[i] + 14);
        }
    }

    // Load from environment
    agent_config_load_env(config);

    // Load from command line arguments
    agent_config_load_args(config, argc, argv);

    // Print usage or version if requested
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("Sirah Worker Agent v1.28.0\n");
            return 0;
        }
    }

    // Validate configuration
    if (agent_config_validate(config) != 0) {
        fprintf(stderr, "ERROR: Configuration validation failed\n");
        agent_config_free(config);
        return 1;
    }

    // Print configuration
    agent_config_print(config);

    // Create agent
    agent_t* agent = agent_new(config->api_server_url,
                               config->node_name,
                               config->node_ip,
                               config->pod_cidr);
    if (!agent) {
        fprintf(stderr, "ERROR: Failed to create agent\n");
        agent_config_free(config);
        return 1;
    }

    g_agent = agent;

    // Initialize agent
    if (agent_init(agent) != 0) {
        fprintf(stderr, "ERROR: Agent initialization failed\n");
        agent_free(agent);
        agent_config_free(config);
        return 1;
    }

    // Create supervisor for agent components
    supervisor_t* supervisor = supervisor_new("agent-supervisor");
    if (!supervisor) {
        fprintf(stderr, "ERROR: Failed to create supervisor\n");
        agent_free(agent);
        agent_config_free(config);
        return 1;
    }

    // Add components to supervisor
    printf("\nAdding components to supervisor:\n");
    supervisor_add_component(supervisor, "pod-sync",
                            agent_pod_sync_component, agent,
                            RESTART_PERMANENT, 5);

    supervisor_add_component(supervisor, "heartbeat",
                            agent_heartbeat_component, agent,
                            RESTART_PERMANENT, 5);

    supervisor_add_component(supervisor, "health-check",
                            agent_health_check_component, agent,
                            RESTART_PERMANENT, 3);

    // Register signal handlers for graceful shutdown
    printf("Registering signal handlers...\n");
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Agent ready, entering main loop...\n\n");

    // Run agent main loop
    int result = agent_run(agent);

    printf("\nAgent shut down\n");

    // Cleanup
    supervisor_free(supervisor);
    agent_free(agent);
    agent_config_free(config);

    return result == 0 ? 0 : 1;
}
