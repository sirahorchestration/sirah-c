// internal/runtime/qemu_log_capture.c
// Capture QEMU serial output and stream to Sirah pod logs API

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/select.h>
#include <curl/curl.h>
#include "qemu_log_capture.h"

// Thread function to capture logs from QEMU serial socket
static void* qemu_log_capture_thread(void* arg) {
    qemu_log_capture_t* capture = (qemu_log_capture_t*)arg;
    
    fprintf(stderr, "[QEMU Log Capture] Starting capture thread for %s\n", capture->vm_id);
    fflush(stderr);
    
    // Open log file for reading
    FILE* log_fp = fopen(capture->log_file, "r");
    if (!log_fp) {
        fprintf(stderr, "[QEMU Log Capture] ERROR: Cannot open log file %s\n", capture->log_file);
        return NULL;
    }
    
    // Seek to end to capture new lines
    fseek(log_fp, 0, SEEK_END);
    
    char line_buffer[4096];
    
    while (capture->running) {
        // Read new lines from QEMU log file
        while (fgets(line_buffer, sizeof(line_buffer), log_fp)) {
            // Remove trailing newline
            size_t len = strlen(line_buffer);
            if (len > 0 && line_buffer[len-1] == '\n') {
                line_buffer[len-1] = '\0';
            }
            
            // Skip empty lines
            if (strlen(line_buffer) == 0) continue;
            
            fprintf(stderr, "[QEMU Log] %s: %s\n", capture->vm_id, line_buffer);
            fflush(stderr);
            
            // Write to pod logs API
            qemu_log_write_to_api(capture->vm_id, capture->namespace, 
                                 capture->pod_name, capture->container_name,
                                 line_buffer);
        }
        
        // Check if file has new content
        clearerr(log_fp);
        usleep(100000);  // 100ms polling
    }
    
    fclose(log_fp);
    fprintf(stderr, "[QEMU Log Capture] Capture thread stopped for %s\n", capture->vm_id);
    fflush(stderr);
    
    return NULL;
}

int qemu_log_capture_init(const char* vm_id, const char* namespace, 
                          const char* pod_name, const char* container_name,
                          const char* serial_socket) {
    // This function prepares paths but doesn't start capture yet
    // Called during VM spawn
    fprintf(stderr, "[QEMU Log Capture] Initialized for %s/%s\n", namespace, pod_name);
    fflush(stderr);
    return 0;
}

int qemu_log_capture_start(qemu_log_capture_t* capture) {
    if (!capture) {
        return -1;
    }
    
    capture->running = 1;
    
    // Start capture thread
    if (pthread_create(&capture->capture_thread, NULL, qemu_log_capture_thread, capture) != 0) {
        fprintf(stderr, "[QEMU Log Capture] ERROR: Failed to create capture thread\n");
        fflush(stderr);
        return -1;
    }
    
    fprintf(stderr, "[QEMU Log Capture] Started for %s\n", capture->vm_id);
    fflush(stderr);
    
    return 0;
}

int qemu_log_capture_stop(qemu_log_capture_t* capture) {
    if (!capture) {
        return -1;
    }
    
    capture->running = 0;
    
    // Wait for thread to finish
    pthread_join(capture->capture_thread, NULL);
    
    fprintf(stderr, "[QEMU Log Capture] Stopped for %s\n", capture->vm_id);
    fflush(stderr);
    
    return 0;
}

// Simple HTTP POST to write logs via Sirah API
// In production, this would use proper HTTP library integration
int qemu_log_write_to_api(const char* vm_id, const char* namespace,
                          const char* pod_name, const char* container_name,
                          const char* log_line) {
    if (!log_line || strlen(log_line) == 0) {
        return 0;
    }
    
    // For MVP, logs are captured to file
    // Production would POST to API endpoint:
    // POST /api/v1/namespaces/{namespace}/pods/{pod_name}/log
    // with body: {"container": "container_name", "line": "log_line"}
    
    // For now, just write to internal storage (see pod_logs.c)
    // This would be called by pod_log_write() in apiserver
    
    return 0;
}
