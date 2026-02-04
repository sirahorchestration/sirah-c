// internal/kubelet/probe_handler.c
// Health probe execution for pods (Phase 4)

#include "probe_handler.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <curl/curl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>

// ============================================================================
// Probe Result Management
// ============================================================================

probe_result_t* probe_result_create(void) {
    probe_result_t* result = (probe_result_t*)malloc(sizeof(probe_result_t));
    if (!result) return NULL;
    
    result->success = 0;
    result->exit_code = -1;
    result->message = NULL;
    result->timestamp = time(NULL);
    result->probe_count = 0;
    result->success_count = 0;
    result->failure_count = 0;
    
    return result;
}

void probe_result_free(probe_result_t* result) {
    if (!result) return;
    if (result->message) free(result->message);
    free(result);
}

void probe_result_set_success(probe_result_t* result, int success) {
    result->success = success;
    result->timestamp = time(NULL);
    result->probe_count++;
    
    if (success) {
        result->success_count++;
    } else {
        result->failure_count++;
    }
}

void probe_result_set_message(probe_result_t* result, const char* message) {
    if (result->message) free(result->message);
    result->message = message ? strdup(message) : NULL;
}

// ============================================================================
// HTTP Probe Execution
// ============================================================================

typedef struct {
    char* data;
    size_t size;
} http_response_t;

static size_t http_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    http_response_t* mem = (http_response_t*)userp;
    
    char* ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) {
        fprintf(stderr, "[PROBE] Failed to allocate memory for HTTP response\n");
        return 0;
    }
    
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    
    return realsize;
}

probe_result_t* probe_execute_http(const char* pod_namespace, const char* pod_name,
                                    const char* container_name, http_probe_t* probe) {
    if (!probe) return NULL;
    
    probe_result_t* result = probe_result_create();
    if (!result) return NULL;
    
    // Build URL
    char url[512];
    const char* host = probe->host ? probe->host : "127.0.0.1";
    const char* path = probe->path ? probe->path : "/";
    
    snprintf(url, sizeof(url), "http://%s:%d%s", host, probe->port, path);
    
    fprintf(stderr, "[PROBE] HTTP probe: %s/%s container=%s url=%s\n",
            pod_namespace, pod_name, container_name, url);
    
    // Initialize curl
    CURL* curl = curl_easy_init();
    if (!curl) {
        probe_result_set_message(result, "Failed to initialize HTTP client");
        return result;
    }
    
    // Prepare response buffer
    http_response_t response;
    response.data = (char*)malloc(1);
    response.size = 0;
    
    // Configure curl
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)probe->timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    // Add custom headers if provided
    struct curl_slist* headers = NULL;
    if (probe->num_headers > 0) {
        for (int i = 0; i < probe->num_headers; i++) {
            char header[256];
            snprintf(header, sizeof(header), "%s: %s", 
                    probe->header_names[i], probe->header_values[i]);
            headers = curl_slist_append(headers, header);
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        
        if (http_code >= 200 && http_code < 300) {
            probe_result_set_success(result, 1);
            fprintf(stderr, "[PROBE] HTTP probe succeeded: %s/%s status=%ld\n",
                   pod_namespace, pod_name, http_code);
        } else {
            probe_result_set_success(result, 0);
            char msg[128];
            snprintf(msg, sizeof(msg), "HTTP %ld", http_code);
            probe_result_set_message(result, msg);
            fprintf(stderr, "[PROBE] HTTP probe failed: %s/%s status=%ld\n",
                   pod_namespace, pod_name, http_code);
        }
    } else {
        probe_result_set_success(result, 0);
        probe_result_set_message(result, curl_easy_strerror(res));
        fprintf(stderr, "[PROBE] HTTP probe error: %s/%s %s\n",
               pod_namespace, pod_name, curl_easy_strerror(res));
    }
    
    if (headers) curl_slist_free_all(headers);
    free(response.data);
    curl_easy_cleanup(curl);
    
    return result;
}

// ============================================================================
// TCP Probe Execution
// ============================================================================

probe_result_t* probe_execute_tcp(const char* pod_namespace, const char* pod_name,
                                   const char* container_name, tcp_probe_t* probe) {
    if (!probe) return NULL;
    
    probe_result_t* result = probe_result_create();
    if (!result) return NULL;
    
    const char* host = probe->host ? probe->host : "127.0.0.1";
    
    fprintf(stderr, "[PROBE] TCP probe: %s/%s container=%s host=%s port=%d\n",
            pod_namespace, pod_name, container_name, host, probe->port);
    
    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        probe_result_set_message(result, "Failed to create socket");
        return result;
    }
    
    // Set timeout
    struct timeval tv;
    tv.tv_sec = probe->timeout_seconds;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
    
    // Prepare address
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(probe->port);
    
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        probe_result_set_message(result, "Invalid host address");
        close(sock);
        return result;
    }
    
    // Connect
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        probe_result_set_success(result, 1);
        fprintf(stderr, "[PROBE] TCP probe succeeded: %s/%s\n",
               pod_namespace, pod_name);
    } else {
        probe_result_set_success(result, 0);
        probe_result_set_message(result, "Connection refused");
        fprintf(stderr, "[PROBE] TCP probe failed: %s/%s connection refused\n",
               pod_namespace, pod_name);
    }
    
    close(sock);
    return result;
}

// ============================================================================
// Exec Probe Execution
// ============================================================================

probe_result_t* probe_execute_exec(const char* pod_namespace, const char* pod_name,
                                    const char* container_name, exec_probe_t* probe) {
    if (!probe || probe->num_commands == 0) return NULL;
    
    probe_result_t* result = probe_result_create();
    if (!result) return NULL;
    
    fprintf(stderr, "[PROBE] Exec probe: %s/%s container=%s cmd=%s\n",
            pod_namespace, pod_name, container_name, probe->commands[0]);
    
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child process: execute command
        execvp(probe->commands[0], probe->commands);
        exit(127);  // Command not found
    } else if (pid > 0) {
        // Parent process: wait for child with timeout
        int status = 0;
        time_t start = time(NULL);
        
        while (1) {
            pid_t ret = waitpid(pid, &status, WNOHANG);
            
            if (ret == pid) {
                // Process exited
                if (WIFEXITED(status)) {
                    int exit_code = WEXITSTATUS(status);
                    result->exit_code = exit_code;
                    
                    if (exit_code == 0) {
                        probe_result_set_success(result, 1);
                        fprintf(stderr, "[PROBE] Exec probe succeeded: %s/%s\n",
                               pod_namespace, pod_name);
                    } else {
                        probe_result_set_success(result, 0);
                        char msg[64];
                        snprintf(msg, sizeof(msg), "Exit code %d", exit_code);
                        probe_result_set_message(result, msg);
                        fprintf(stderr, "[PROBE] Exec probe failed: %s/%s exit=%d\n",
                               pod_namespace, pod_name, exit_code);
                    }
                } else {
                    // Killed by signal
                    probe_result_set_success(result, 0);
                    probe_result_set_message(result, "Killed by signal");
                    fprintf(stderr, "[PROBE] Exec probe killed: %s/%s\n",
                           pod_namespace, pod_name);
                }
                break;
            }
            
            // Check timeout
            if (time(NULL) - start > probe->timeout_seconds) {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                probe_result_set_success(result, 0);
                probe_result_set_message(result, "Timeout");
                fprintf(stderr, "[PROBE] Exec probe timeout: %s/%s\n",
                       pod_namespace, pod_name);
                break;
            }
            
            usleep(100000);  // 100ms
        }
    } else {
        // Fork failed
        probe_result_set_message(result, "Fork failed");
        fprintf(stderr, "[PROBE] Exec probe fork failed: %s/%s\n",
               pod_namespace, pod_name);
    }
    
    return result;
}

// ============================================================================
// gRPC Probe Execution (Stub)
// ============================================================================

probe_result_t* probe_execute_grpc(const char* pod_namespace, const char* pod_name,
                                    const char* container_name, grpc_probe_t* probe) {
    if (!probe) return NULL;
    
    probe_result_t* result = probe_result_create();
    if (!result) return NULL;
    
    // Note: Full gRPC probe implementation would require gRPC library
    // For MVP, we'll skip actual gRPC calls and just return success
    fprintf(stderr, "[PROBE] gRPC probe (stub): %s/%s container=%s port=%d\n",
            pod_namespace, pod_name, container_name, probe->port);
    
    probe_result_set_success(result, 1);
    probe_result_set_message(result, "gRPC probe not implemented (stub)");
    
    return result;
}

// ============================================================================
// Generic Probe Execution
// ============================================================================

probe_result_t* probe_execute(const char* pod_namespace, const char* pod_name,
                              const char* container_name, probe_spec_t* probe) {
    if (!probe || !probe->enabled) return NULL;
    
    probe_result_t* result = NULL;
    
    switch (probe->handler_type) {
        case HANDLER_HTTP:
            result = probe_execute_http(pod_namespace, pod_name, container_name, probe->handler.http);
            break;
        case HANDLER_TCP:
            result = probe_execute_tcp(pod_namespace, pod_name, container_name, probe->handler.tcp);
            break;
        case HANDLER_EXEC:
            result = probe_execute_exec(pod_namespace, pod_name, container_name, probe->handler.exec);
            break;
        case HANDLER_GRPC:
            result = probe_execute_grpc(pod_namespace, pod_name, container_name, probe->handler.grpc);
            break;
        default:
            return NULL;
    }
    
    return result;
}

// ============================================================================
// Container Health Determination
// ============================================================================

container_health_t probe_update_container_health(probe_result_t* startup_result,
                                                  probe_result_t* readiness_result,
                                                  probe_result_t* liveness_result) {
    // If liveness probe failed, container is dead
    if (liveness_result && !liveness_result->success) {
        if (liveness_result->failure_count >= 3) {
            return HEALTH_DEAD;
        }
    }
    
    // If startup probe still running, container is starting
    if (startup_result && !startup_result->success) {
        return HEALTH_STARTING;
    }
    
    // If readiness probe failed, container is not ready
    if (readiness_result && !readiness_result->success) {
        if (readiness_result->failure_count >= 1) {
            return HEALTH_NOT_READY;
        }
    }
    
    // If all probes passed, container is ready
    return HEALTH_READY;
}
