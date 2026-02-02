#!/bin/bash

# Enhanced QEMU log streaming setup
# This script explains and implements QEMU unikernel log streaming to Sirah

cat << 'EOF'
=== QEMU Unikernel Log Streaming Implementation ===

OBJECTIVE:
  Enable kubectl logs to show output from unikernels running inside QEMU

ARCHITECTURE:
  1. QEMU startup with serial console enabled
  2. Serial output captured to file
  3. Pod controller monitors and reads QEMU log file
  4. Logs written to Sirah pod logs API
  5. kubectl logs retrieves from API

IMPLEMENTATION STEPS:

---
STEP 1: QEMU Startup Configuration
---

File: internal/runtime/qemu.c - qemu_spawn()

Add these QEMU flags to enable serial logging:
  
  -serial stdio          # Route serial output to stdout (captured in log file)
  -monitor none          # Disable QEMU monitor to avoid mixing output
  -display none          # Disable display (already using -nographic)

Unikernel kernels must output to serial console (COM1) for logs to appear.

Example for common unikernels:
  • MirageOS: Logs to serial by default
  • IncludeOS: Logs to serial by default  
  • Rumprun: Logs to serial by default
  • Custom: Ensure app prints to /dev/ttyS0 or /dev/console

---
STEP 2: Log File Location
---

QEMU logs are written to:
  /tmp/qemu-{namespace}-{pod-name}.log

Example:
  /tmp/qemu-default-test-qemu-spawn.log

The pod controller reads from this file and streams lines to the Sirah API.

---
STEP 3: Pod Controller Log Capture
---

File: internal/controller/pod_controller.c

After spawning VM, the controller:
  1. Reads /tmp/qemu-{vm-id}.log continuously
  2. Extracts new lines that appear
  3. Sends each line to pod logs API via pod_log_write()

Function to add:
  - pod_capture_qemu_logs() - Thread that tails the QEMU log file
  - Call in pod_controller_sync_states() after runtime_spawn_vm()

---
STEP 4: Pod Logs API Integration
---

File: internal/apiserver/pod_logs.c - pod_log_write()

Already implemented! The function:
  1. Takes namespace, pod_name, container_name, log_line
  2. Stores in in-memory log_store[]
  3. Retrieved by kubectl logs API endpoint

Call from controller:
  pod_log_write("default", "test-qemu-spawn", "app", "Kernel boot message");

---
STEP 5: kubectl logs Integration
---

kubectl logs already works via:
  GET /api/v1/namespaces/{ns}/pods/{pod}/log?tailLines=50

The API endpoint:
  1. Retrieves logs from log_store[]
  2. Supports ?follow=true for streaming (basic tail)
  3. Returns plain text lines

---
IMPLEMENTATION CODE
---

Add this to pod_controller.c after spawn:

  // Start log capture thread
  pod_start_log_capture(pod->namespace, pod->pod_name, pod->vm_id);

Add this function to pod_controller.c:

void* pod_log_capture_thread(void* arg) {
    pod_log_capture_t* ctx = (pod_log_capture_t*)arg;
    
    char log_file[512];
    snprintf(log_file, sizeof(log_file), "/tmp/qemu-%s.log", ctx->vm_id);
    
    FILE* fp = fopen(log_file, "r");
    if (!fp) return NULL;
    
    // Seek to end
    fseek(fp, 0, SEEK_END);
    
    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        
        // Write to pod logs API
        pod_log_write(ctx->namespace, ctx->pod_name, "app", line);
        
        fprintf(stderr, "[QEMU LOG] %s/%s: %s\n", 
                ctx->namespace, ctx->pod_name, line);
    }
    
    fclose(fp);
    return NULL;
}

---
TESTING
---

1. Create pod with unikernel image:

  curl -X POST http://localhost:6443/api/v1/namespaces/default/pods \
    -H "Content-Type: application/json" \
    -u admin:admin \
    -d '{
      "apiVersion": "v1",
      "kind": "Pod",
      "metadata": {"name": "my-unikernel"},
      "spec": {
        "containers": [{
          "name": "app",
          "image": "/tmp/sirah-unikernels/test-kernel.img"
        }]
      }
    }'

2. Monitor QEMU log file:

  tail -f /tmp/qemu-default-my-unikernel.log

3. Get logs via API:

  curl -u admin:admin http://localhost:6443/api/v1/namespaces/default/pods/my-unikernel/log

4. Use kubectl:

  kubectl logs my-unikernel
  kubectl logs -f my-unikernel (with follow)

---
CONFIGURATION OPTIONS
---

For QEMU serial output control:

a) Send to file (current):
   -serial file:/tmp/qemu-{id}.log
   
b) Send to stdio (captured in process output):
   -serial stdio
   
c) Send to socket (for advanced streaming):
   -serial unix:/tmp/qemu-{id}.sock
   
d) Send to multiple outputs:
   -serial stdio -serial file:/tmp/qemu-{id}.log

Recommended: -serial stdio (stdout captured in log file)

---
TROUBLESHOOTING
---

Problem: No logs appearing
  → Check if unikernel outputs to serial console
  → Verify QEMU is started with -serial stdio
  → Check /tmp/qemu-*.log file exists and has content

Problem: Logs not appearing in kubectl logs
  → Check pod_log_write() is being called
  → Verify API logs endpoint works: curl ...​/log
  → Check in-memory log store isn't full

Problem: Logs mixed with QEMU debug output
  → Remove -monitor stdio or use -monitor none
  → Use -display none to suppress display warnings

---
NEXT STEPS
---

1. Implement log capture thread in pod_controller.c
2. Test with a real MirageOS or IncludeOS unikernel
3. Optimize log buffer for large outputs
4. Add persistent log storage (SQLite/etcd)
5. Implement WebSocket streaming for follow mode

EOF
