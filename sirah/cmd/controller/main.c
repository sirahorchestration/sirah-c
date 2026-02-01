// cmd/controller/main.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int controller_manager_init(const char* apiserver_url);
int controller_manager_run(void);

int main(int argc, char** argv) {
    const char* apiserver_url = "http://localhost:6443";
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--apiserver") == 0 && i + 1 < argc) {
            apiserver_url = argv[++i];
        }
    }
    
    printf("Sirah Controller Manager starting...\n");
    printf("  API Server: %s\n", apiserver_url);
    
    if (controller_manager_init(apiserver_url) != 0) {
        fprintf(stderr, "Failed to initialize controller manager\n");
        return 1;
    }
    
    if (controller_manager_run() != 0) {
        fprintf(stderr, "Controller manager error\n");
        return 1;
    }
    
    return 0;
}
