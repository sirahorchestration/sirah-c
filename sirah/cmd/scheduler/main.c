// cmd/scheduler/main.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int scheduler_init(const char* apiserver_url);
int scheduler_run(void);

int main(int argc, char** argv) {
    const char* apiserver_url = "http://localhost:6443";
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--apiserver") == 0 && i + 1 < argc) {
            apiserver_url = argv[++i];
        }
    }
    
    printf("Sirah Scheduler starting...\n");
    printf("  API Server: %s\n", apiserver_url);
    
    if (scheduler_init(apiserver_url) != 0) {
        fprintf(stderr, "Failed to initialize scheduler\n");
        return 1;
    }
    
    if (scheduler_run() != 0) {
        fprintf(stderr, "Scheduler error\n");
        return 1;
    }
    
    return 0;
}
