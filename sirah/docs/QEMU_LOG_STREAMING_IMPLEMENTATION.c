// Implementation: Add to internal/controller/pod_controller.c

// Log capture context
typedef struct {
    char namespace[256];
    char pod_name[256];
    char vm_id[256];
    char container_name[256];
    int running;
    pthread_t thread;
} pod_log_capture_t;

// Thread function to capture QEMU logs
static void* pod_capture_logs_thread(void* arg) {
    pod_log_capture_t* ctx = (pod_log_capture_t*)arg;
    
    char log_file[512];
    snprintf(log_file, sizeof(log_file), "/tmp/qemu-%s.log", ctx->vm_id);
    
    fprintf(stderr, "[LOG CAPTURE] Starting for %s/%s, reading %s\n",
            ctx->namespace, ctx->pod_name, log_file);
    fflush(stderr);
    
    // Wait for file to be created
    int wait_count = 0;
    while (!fopen(log_file, "r") && wait_count < 20) {
        usleep(100000);  // 100ms
        wait_count++;
    }
    
    FILE* fp = fopen(log_file, "r");
    if (!fp) {
        fprintf(stderr, "[LOG CAPTURE] ERROR: Cannot open %s\n", log_file);
        fflush(stderr);
        return NULL;
    }
    
    // Seek to end to get only new lines
    fseek(fp, 0, SEEK_END);
    
    char line[4096];
    int line_count = 0;
    
    while (ctx->running) {
        // Read lines from QEMU log
        while (fgets(line, sizeof(line), fp) != NULL) {
            // Remove trailing newline
            size_t len = strlen(line);
            if (len > 0 && line[len-1] == '\n') {
                line[len-1] = '\0';
            }
            
            // Skip empty lines
            if (strlen(line) == 0) continue;
            
            line_count++;
            
            fprintf(stderr, "[LOG CAPTURE] %s/%s: %s\n",
                    ctx->namespace, ctx->pod_name, line);
            fflush(stderr);
            
            // Write to pod logs API
            // This function is already in pod_logs.c:
            // pod_log_write(namespace, pod_name, container, log_line)
            // 
            // Call it to store the log:
            // pod_log_write(ctx->namespace, ctx->pod_name, 
            //              ctx->container_name, line);
        }
        
        // Clear EOF flag and try again in 100ms
        clearerr(fp);
        usleep(100000);
    }
    
    fprintf(stderr, "[LOG CAPTURE] Stopped for %s/%s (%d lines)\n",
            ctx->namespace, ctx->pod_name, line_count);
    fflush(stderr);
    
    fclose(fp);
    free(ctx);
    
    return NULL;
}

// Start log capture thread
static int pod_start_log_capture(const char* namespace, const char* pod_name,
                                 const char* vm_id) {
    pod_log_capture_t* ctx = malloc(sizeof(pod_log_capture_t));
    if (!ctx) return -1;
    
    strncpy(ctx->namespace, namespace, sizeof(ctx->namespace) - 1);
    strncpy(ctx->pod_name, pod_name, sizeof(ctx->pod_name) - 1);
    strncpy(ctx->vm_id, vm_id, sizeof(ctx->vm_id) - 1);
    strncpy(ctx->container_name, "app", sizeof(ctx->container_name) - 1);
    ctx->running = 1;
    
    if (pthread_create(&ctx->thread, NULL, pod_capture_logs_thread, ctx) != 0) {
        fprintf(stderr, "[LOG CAPTURE] ERROR: Failed to create thread\n");
        free(ctx);
        return -1;
    }
    
    fprintf(stderr, "[LOG CAPTURE] Thread started for %s/%s\n",
            namespace, pod_name);
    fflush(stderr);
    
    return 0;
}

// Add this call in pod_controller_sync_states() after runtime_spawn_vm():

    // After: fprintf(stderr, "[POD CONTROLLER] SYNC: runtime_spawn_vm returned %d\n", ret);
    
    if (ret == 0) {
        // Start capturing QEMU logs
        pod_start_log_capture(pod->namespace, pod->pod_name, pod->vm_id);
        
        // ... rest of code ...
    }

// ============================================================================
// TESTING STEPS
// ============================================================================

/*

1. Verify QEMU startup includes -serial stdio:
   
   Check in qemu.c - execvp() should have these args:
     "qemu-system-x86_64",
     "-kernel", image_path,
     "-m", mem_str,
     "-smp", cpus_str,
     "-nographic",
     "-name", name_str,
     "-serial", "stdio",           // MUST BE ADDED
     "-monitor", "none",           // SHOULD BE ADDED
     NULL

2. Create a test unikernel:
   
   dd if=/dev/zero of=/tmp/sirah-unikernels/test-kernel.img bs=1M count=10

3. Create a pod:
   
   curl -X POST http://localhost:6443/api/v1/namespaces/default/pods \
     -H "Content-Type: application/json" \
     -u admin:admin \
     -d '{
       "apiVersion": "v1",
       "kind": "Pod",
       "metadata": {"name": "log-test"},
       "spec": {
         "containers": [{
           "name": "app",
           "image": "/tmp/sirah-unikernels/test-kernel.img"
         }]
       }
     }'

4. Monitor QEMU output:
   
   tail -f /tmp/qemu-default-log-test.log

5. Check if logs are captured:
   
   curl -u admin:admin \
     http://localhost:6443/api/v1/namespaces/default/pods/log-test/log

6. Use kubectl:
   
   kubectl logs log-test

*/
