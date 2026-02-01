// cmd/apiserver/main.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int api_server_init(int port);
int api_server_run(void);

int main(int argc, char** argv) {
    int port = 6443;
    const char* etcd_addr = "localhost:2379";
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        }
        if (strcmp(argv[i], "--etcd") == 0 && i + 1 < argc) {
            etcd_addr = argv[++i];
        }
    }
    
    printf("Sirah API Server starting...\n");
    printf("  Listen: 0.0.0.0:%d\n", port);
    printf("  etcd: %s\n", etcd_addr);
    
    if (api_server_init(port) != 0) {
        fprintf(stderr, "Failed to initialize API server\n");
        return 1;
    }
    
    if (api_server_run() != 0) {
        fprintf(stderr, "API server error\n");
        return 1;
    }
    
    return 0;
}
