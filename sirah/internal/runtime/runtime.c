#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime.h"
#include "qemu.h"

runtime_t* g_runtime = NULL;

int runtime_init(runtime_type_t type) {
    if (g_runtime) {
        fprintf(stderr, "Runtime already initialized\n");
        return -1;
    }
    
    g_runtime = (runtime_t*)malloc(sizeof(runtime_t));
    if (!g_runtime) {
        return -1;
    }
    
    memset(g_runtime, 0, sizeof(runtime_t));
    g_runtime->type = type;
    
    switch (type) {
        case RUNTIME_QEMU:
            printf("[Runtime] Initializing QEMU runtime\n");
            g_runtime->init = qemu_init;
            g_runtime->spawn = qemu_spawn;
            g_runtime->stop = qemu_stop;
            g_runtime->get_status = qemu_get_status;
            g_runtime->cleanup = qemu_cleanup;
            return g_runtime->init();
            
        default:
            fprintf(stderr, "Unsupported runtime type\n");
            free(g_runtime);
            g_runtime = NULL;
            return -1;
    }
}

runtime_t* runtime_get(void) {
    return g_runtime;
}

int runtime_spawn_vm(vm_spec_t* spec) {
    if (!g_runtime || !g_runtime->spawn) {
        fprintf(stderr, "Runtime not initialized\n");
        return -1;
    }
    return g_runtime->spawn(spec);
}

int runtime_stop_vm(const char* vm_id) {
    if (!g_runtime || !g_runtime->stop) {
        fprintf(stderr, "Runtime not initialized\n");
        return -1;
    }
    return g_runtime->stop(vm_id);
}

int runtime_get_vm_status(const char* vm_id, char* status, size_t size) {
    if (!g_runtime || !g_runtime->get_status) {
        fprintf(stderr, "Runtime not initialized\n");
        return -1;
    }
    return g_runtime->get_status(vm_id, status, size);
}

int runtime_cleanup(void) {
    if (!g_runtime) {
        return 0;
    }
    
    if (g_runtime->cleanup) {
        g_runtime->cleanup();
    }
    
    free(g_runtime);
    g_runtime = NULL;
    return 0;
}
