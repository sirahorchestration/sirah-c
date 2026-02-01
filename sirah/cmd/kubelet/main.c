#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "../../internal/kubelet/kubelet.h"

static int running = 1;

void signal_handler(int sig) {
    fprintf(stderr, "\n[kubelet] Received signal %d, shutting down...\n", sig);
    running = 0;
}

int main(int argc, char* argv[]) {
    const char* node_name = "worker-1";
    const char* api_server = "http://localhost:6443";
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--node-name") == 0 && i + 1 < argc) {
            node_name = argv[++i];
        } else if (strcmp(argv[i], "--api-server") == 0 && i + 1 < argc) {
            api_server = argv[++i];
        }
    }
    
    // Setup signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    fprintf(stderr, "======================================\n");
    fprintf(stderr, "Sirah Kubelet (Node Agent)\n");
    fprintf(stderr, "======================================\n");
    fprintf(stderr, "Node Name: %s\n", node_name);
    fprintf(stderr, "API Server: %s\n", api_server);
    fprintf(stderr, "======================================\n\n");
    
    // Initialize kubelet
    kubelet_t* kubelet = kubelet_new(node_name, api_server);
    if (!kubelet) {
        fprintf(stderr, "[kubelet] Failed to create kubelet\n");
        return 1;
    }
    
    if (kubelet_init(kubelet) != 0) {
        fprintf(stderr, "[kubelet] Failed to initialize kubelet\n");
        kubelet_free(kubelet);
        return 1;
    }
    
    fprintf(stderr, "[kubelet %s] Ready to receive pod assignments\n", node_name);
    fprintf(stderr, "[kubelet] Press Ctrl+C to stop\n\n");
    
    // Run main loop
    while (running) {
        // Main kubelet loop will be here
        sleep(5);
    }
    
    fprintf(stderr, "\n[kubelet] Cleanup...\n");
    kubelet_shutdown(kubelet);
    kubelet_free(kubelet);
    
    fprintf(stderr, "[kubelet] Shutdown complete\n");
    return 0;
}
