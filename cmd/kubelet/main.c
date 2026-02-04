#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "../../internal/kubelet/agent.h"
#include "../../internal/kubelet/config.h"
#include "../../internal/kubelet/supervision.h"

#define VERSION "0.1.0"

// Global agent pointer for signal handlers
static agent_t* g_agent = NULL;
static supervisor_t* g_supervisor = NULL;

// Signal handler for graceful shutdown
void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        fprintf(stderr, "\nReceived signal %d, shutting down gracefully...\n", sig);
        
        if (g_agent) {
            agent_shutdown(g_agent);
        }
        
        if (g_supervisor) {
            supervisor_stop(g_supervisor);
        }
    }
}

int main(int argc, char* argv[]) {
    printf("Sirah Worker Agent v%s\n", VERSION);
    printf("=====================================\n\n");
    
    // ========== Configuration Loading ==========
    
    // Create config with defaults
    agent_config_t* config = agent_config_new();
    if (!config) {
        fprintf(stderr, "Failed to create configuration\n");
        return 1;
    }
    
    // Load from file
    agent_config_load_file(config, "/etc/sirah/agent.conf");
    
    // Load from environment
    agent_config_load_env(config);
    
    // Parse command-line arguments
    if (agent_config_load_args(config, argc, argv) == 1) {
        // --help was requested
        agent_config_free(config);
        return 0;
    }
    
    // Validate configuration
    if (agent_config_validate(config) != 0) {
        fprintf(stderr, "Configuration validation failed\n");
        agent_config_free(config);
        return 1;
    }
    
    // Print configuration
    printf("Configuration loaded:\n");
    agent_config_print(config);
    
    // ========== Agent Initialization ==========
    
    // Create agent
    g_agent = agent_new(
        config->api_server_url,
        config->node_name,
        config->node_ip,
        config->pod_cidr
    );
    
    if (!g_agent) {
        fprintf(stderr, "Failed to create agent\n");
        agent_config_free(config);
        return 1;
    }
    
    printf("Agent created for node '%s'\n", config->node_name);
    
    // Initialize agent (register with control plane)
    if (agent_init(g_agent) != 0) {
        fprintf(stderr, "Agent initialization failed\n");
        agent_free(g_agent);
        agent_config_free(config);
        return 1;
    }
    
    printf("Agent initialized and registered with control plane\n\n");
    
    // ========== Supervisor Setup ==========
    
    // Create supervisor
    // Max 5 failures per 60 second window
    g_supervisor = supervisor_new(5, 60000);
    if (!g_supervisor) {
        fprintf(stderr, "Failed to create supervisor\n");
        agent_free(g_agent);
        agent_config_free(config);
        return 1;
    }
    
    // Add supervised components
    printf("Setting up supervised components:\n");
    
    // Pod sync component (PERMANENT, max 5 restarts)
    supervisor_add_component(
        g_supervisor,
        "pod-sync",
        agent_pod_sync_component,
        g_agent,
        RESTART_PERMANENT,
        5
    );
    printf("  - pod-sync (PERMANENT, 5 restarts)\n");
    
    // Heartbeat component (PERMANENT, max 5 restarts)
    supervisor_add_component(
        g_supervisor,
        "heartbeat",
        agent_heartbeat_component,
        g_agent,
        RESTART_PERMANENT,
        5
    );
    printf("  - heartbeat (PERMANENT, 5 restarts)\n");
    
    // Health check component (PERMANENT, max 3 restarts)
    supervisor_add_component(
        g_supervisor,
        "health-check",
        agent_health_check_component,
        g_agent,
        RESTART_PERMANENT,
        3
    );
    printf("  - health-check (PERMANENT, 3 restarts)\n");
    printf("\n");
    
    // ========== Signal Handlers ==========
    
    // Register signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Signal handlers registered (SIGINT, SIGTERM)\n");
    printf("Agent running, press Ctrl+C to shutdown...\n");
    printf("=====================================\n\n");
    
    // ========== Main Loop ==========
    
    // Run supervisor (blocks until shutdown)
    supervisor_run(g_supervisor);
    
    // ========== Cleanup ==========
    
    printf("\nCleaning up...\n");
    
    supervisor_free(g_supervisor);
    agent_free(g_agent);
    agent_config_free(config);
    
    printf("Agent shutdown complete\n");
    
    return 0;
}
