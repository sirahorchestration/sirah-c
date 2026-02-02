// internal/runtime/qemu_log_capture.h
// Capture and stream QEMU unikernel logs to Sirah API

#ifndef SIRAH_QEMU_LOG_CAPTURE_H
#define SIRAH_QEMU_LOG_CAPTURE_H

#include <pthread.h>

typedef struct {
    char vm_id[256];
    char namespace[256];
    char pod_name[256];
    char container_name[256];
    char serial_socket[512];
    char log_file[512];
    int fd;
    pthread_t capture_thread;
    int running;
} qemu_log_capture_t;

// Initialize log capture for a VM
int qemu_log_capture_init(const char* vm_id, const char* namespace, 
                          const char* pod_name, const char* container_name,
                          const char* serial_socket);

// Start capturing logs (spawns thread)
int qemu_log_capture_start(qemu_log_capture_t* capture);

// Stop capturing logs
int qemu_log_capture_stop(qemu_log_capture_t* capture);

// Write captured logs to pod logs API
int qemu_log_write_to_api(const char* vm_id, const char* namespace,
                          const char* pod_name, const char* container_name,
                          const char* log_line);

#endif
