// internal/apiserver/probes.c
// Health check probe implementation

#include "probes.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <curl/curl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// ============ Probe Spec ============

probe_spec_t* probe_spec_new(probe_type_t type) {
    probe_spec_t* spec = (probe_spec_t*)malloc(sizeof(probe_spec_t));
    if (!spec) return NULL;
    
    spec->type = type;
    spec->handler_type = (char*)malloc(16);
    strcpy(spec->handler_type, "http");
    spec->http_path = (char*)malloc(256);
    strcpy(spec->http_path, "/");
    spec->http_port = 8080;
    spec->tcp_port = 0;
    spec->exec_command = (char*)malloc(256);
    strcpy(spec->exec_command, "");
    
    spec->initial_delay_seconds = 0;
    spec->timeout_seconds = 1;
    spec->period_seconds = 10;
    spec->success_threshold = 1;
    spec->failure_threshold = 3;
    
    return spec;
}

void probe_spec_free(probe_spec_t* spec) {
    if (!spec) return;
    free(spec->handler_type);
    free(spec->http_path);
    free(spec->exec_command);
    free(spec);
}

int probe_spec_set_http_handler(probe_spec_t* spec, const char* path, int port) {
    if (!spec || !path) return -1;
    strcpy(spec->handler_type, "http");
    strcpy(spec->http_path, path);
    spec->http_port = port;
    return 0;
}

int probe_spec_set_tcp_handler(probe_spec_t* spec, int port) {
    if (!spec) return -1;
    strcpy(spec->handler_type, "tcp");
    spec->tcp_port = port;
    return 0;
}

int probe_spec_set_exec_handler(probe_spec_t* spec, const char* command) {
    if (!spec || !command) return -1;
    strcpy(spec->handler_type, "exec");
    strcpy(spec->exec_command, command);
    return 0;
}

int probe_spec_set_timing(probe_spec_t* spec, int initial_delay, int timeout,
                          int period, int success_threshold, int failure_threshold) {
    if (!spec) return -1;
    spec->initial_delay_seconds = initial_delay;
    spec->timeout_seconds = timeout;
    spec->period_seconds = period;
    spec->success_threshold = success_threshold;
    spec->failure_threshold = failure_threshold;
    return 0;
}

// ============ Probe Status ============

probe_status_t* probe_status_new(probe_type_t type) {
    probe_status_t* status = (probe_status_t*)malloc(sizeof(probe_status_t));
    if (!status) return NULL;
    
    status->type = type;
    status->last_result = PROBE_UNKNOWN;
    status->success_count = 0;
    status->failure_count = 0;
    status->last_probe_time = time(NULL);
    status->failure_reason = (char*)malloc(256);
    strcpy(status->failure_reason, "Not yet probed");
    
    return status;
}

void probe_status_free(probe_status_t* status) {
    if (!status) return;
    free(status->failure_reason);
    free(status);
}

int probe_status_record_success(probe_status_t* status) {
    if (!status) return -1;
    status->last_result = PROBE_SUCCESS;
    status->success_count++;
    status->failure_count = 0;
    status->last_probe_time = time(NULL);
    strcpy(status->failure_reason, "");
    return 0;
}

int probe_status_record_failure(probe_status_t* status, const char* reason) {
    if (!status) return -1;
    status->last_result = PROBE_FAILURE;
    status->failure_count++;
    status->success_count = 0;
    status->last_probe_time = time(NULL);
    if (reason) strncpy(status->failure_reason, reason, 255);
    return 0;
}

// ============ Pod Probes ============

pod_probes_t* pod_probes_new(const char* pod_name, const char* namespace) {
    if (!pod_name || !namespace) return NULL;
    
    pod_probes_t* probes = (pod_probes_t*)malloc(sizeof(pod_probes_t));
    if (!probes) return NULL;
    
    probes->pod_name = (char*)malloc(strlen(pod_name) + 1);
    strcpy(probes->pod_name, pod_name);
    
    probes->namespace = (char*)malloc(strlen(namespace) + 1);
    strcpy(probes->namespace, namespace);
    
    probes->startup_probe = NULL;
    probes->startup_status = NULL;
    probes->startup_complete = false;
    
    probes->readiness_probe = NULL;
    probes->readiness_status = NULL;
    probes->is_ready = false;
    
    probes->liveness_probe = NULL;
    probes->liveness_status = NULL;
    probes->is_alive = true;
    
    probes->created_time = time(NULL);
    
    return probes;
}

void pod_probes_free(pod_probes_t* probes) {
    if (!probes) return;
    free(probes->pod_name);
    free(probes->namespace);
    if (probes->startup_probe) probe_spec_free(probes->startup_probe);
    if (probes->startup_status) probe_status_free(probes->startup_status);
    if (probes->readiness_probe) probe_spec_free(probes->readiness_probe);
    if (probes->readiness_status) probe_status_free(probes->readiness_status);
    if (probes->liveness_probe) probe_spec_free(probes->liveness_probe);
    if (probes->liveness_status) probe_status_free(probes->liveness_status);
    free(probes);
}

int pod_probes_set_startup_probe(pod_probes_t* probes, probe_spec_t* spec) {
    if (!probes || !spec) return -1;
    if (probes->startup_probe) probe_spec_free(probes->startup_probe);
    probes->startup_probe = spec;
    if (!probes->startup_status) {
        probes->startup_status = probe_status_new(PROBE_STARTUP);
    }
    return 0;
}

int pod_probes_set_readiness_probe(pod_probes_t* probes, probe_spec_t* spec) {
    if (!probes || !spec) return -1;
    if (probes->readiness_probe) probe_spec_free(probes->readiness_probe);
    probes->readiness_probe = spec;
    if (!probes->readiness_status) {
        probes->readiness_status = probe_status_new(PROBE_READINESS);
    }
    return 0;
}

int pod_probes_set_liveness_probe(pod_probes_t* probes, probe_spec_t* spec) {
    if (!probes || !spec) return -1;
    if (probes->liveness_probe) probe_spec_free(probes->liveness_probe);
    probes->liveness_probe = spec;
    if (!probes->liveness_status) {
        probes->liveness_status = probe_status_new(PROBE_LIVENESS);
    }
    return 0;
}

// ============ Probe Execution ============

// Execute HTTP probe
static probe_result_t execute_http_probe(const char* host, const char* path, int port, int timeout_secs) {
    CURL* curl = curl_easy_init();
    if (!curl) return PROBE_UNKNOWN;
    
    char url[512];
    snprintf(url, sizeof(url), "http://%s:%d%s", host, port, path);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)timeout_secs);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)timeout_secs);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) return PROBE_FAILURE;
    if (http_code >= 200 && http_code < 300) return PROBE_SUCCESS;
    
    return PROBE_FAILURE;
}

// Execute TCP probe
static probe_result_t execute_tcp_probe(const char* host, int port, int timeout_secs) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return PROBE_FAILURE;
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_aton(host ? host : "127.0.0.1", &addr.sin_addr);
    
    // Set timeout
    struct timeval tv;
    tv.tv_sec = timeout_secs;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    
    int result = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);
    
    return (result == 0) ? PROBE_SUCCESS : PROBE_FAILURE;
}

// Execute Exec probe
static probe_result_t execute_exec_probe(const char* command, int timeout_secs) {
    if (!command || strlen(command) == 0) return PROBE_UNKNOWN;
    
    // Execute command and check exit code
    int exit_code = system(command);
    
    // system() returns exit code shifted by 8 bits
    int actual_code = WEXITSTATUS(exit_code);
    
    return (actual_code == 0) ? PROBE_SUCCESS : PROBE_FAILURE;
}

int pod_probe_startup(pod_probes_t* probes) {
    if (!probes || !probes->startup_probe || !probes->startup_status) return -1;
    
    // Check if initial delay has passed
    time_t now = time(NULL);
    if (now - probes->created_time < probes->startup_probe->initial_delay_seconds) {
        return 0;  // Still in initial delay
    }
    
    probe_result_t result = PROBE_UNKNOWN;
    
    if (strcmp(probes->startup_probe->handler_type, "http") == 0) {
        result = execute_http_probe("127.0.0.1", probes->startup_probe->http_path,
                                   probes->startup_probe->http_port,
                                   probes->startup_probe->timeout_seconds);
    } else if (strcmp(probes->startup_probe->handler_type, "tcp") == 0) {
        result = execute_tcp_probe("127.0.0.1", probes->startup_probe->tcp_port,
                                  probes->startup_probe->timeout_seconds);
    } else if (strcmp(probes->startup_probe->handler_type, "exec") == 0) {
        result = execute_exec_probe(probes->startup_probe->exec_command,
                                   probes->startup_probe->timeout_seconds);
    }
    
    if (result == PROBE_SUCCESS) {
        probe_status_record_success(probes->startup_status);
        if (probes->startup_status->success_count >= probes->startup_probe->success_threshold) {
            probes->startup_complete = true;
        }
    } else if (result == PROBE_FAILURE) {
        probe_status_record_failure(probes->startup_status, "Startup probe failed");
        if (probes->startup_status->failure_count >= probes->startup_probe->failure_threshold) {
            // Mark as failed after too many failures
        }
    }
    
    return 0;
}

int pod_probe_readiness(pod_probes_t* probes) {
    if (!probes || !probes->readiness_probe || !probes->readiness_status) return -1;
    
    // Skip if still in startup phase
    if (probes->startup_probe && !probes->startup_complete) return 0;
    
    probe_result_t result = PROBE_UNKNOWN;
    
    if (strcmp(probes->readiness_probe->handler_type, "http") == 0) {
        result = execute_http_probe("127.0.0.1", probes->readiness_probe->http_path,
                                   probes->readiness_probe->http_port,
                                   probes->readiness_probe->timeout_seconds);
    } else if (strcmp(probes->readiness_probe->handler_type, "tcp") == 0) {
        result = execute_tcp_probe("127.0.0.1", probes->readiness_probe->tcp_port,
                                  probes->readiness_probe->timeout_seconds);
    } else if (strcmp(probes->readiness_probe->handler_type, "exec") == 0) {
        result = execute_exec_probe(probes->readiness_probe->exec_command,
                                   probes->readiness_probe->timeout_seconds);
    }
    
    if (result == PROBE_SUCCESS) {
        probe_status_record_success(probes->readiness_status);
        if (probes->readiness_status->success_count >= probes->readiness_probe->success_threshold) {
            probes->is_ready = true;
        }
    } else if (result == PROBE_FAILURE) {
        probe_status_record_failure(probes->readiness_status, "Readiness probe failed");
        probes->is_ready = false;
    }
    
    return 0;
}

int pod_probe_liveness(pod_probes_t* probes) {
    if (!probes || !probes->liveness_probe || !probes->liveness_status) return -1;
    
    probe_result_t result = PROBE_UNKNOWN;
    
    if (strcmp(probes->liveness_probe->handler_type, "http") == 0) {
        result = execute_http_probe("127.0.0.1", probes->liveness_probe->http_path,
                                   probes->liveness_probe->http_port,
                                   probes->liveness_probe->timeout_seconds);
    } else if (strcmp(probes->liveness_probe->handler_type, "tcp") == 0) {
        result = execute_tcp_probe("127.0.0.1", probes->liveness_probe->tcp_port,
                                  probes->liveness_probe->timeout_seconds);
    } else if (strcmp(probes->liveness_probe->handler_type, "exec") == 0) {
        result = execute_exec_probe(probes->liveness_probe->exec_command,
                                   probes->liveness_probe->timeout_seconds);
    }
    
    if (result == PROBE_SUCCESS) {
        probe_status_record_success(probes->liveness_status);
        probes->is_alive = true;
    } else if (result == PROBE_FAILURE) {
        probe_status_record_failure(probes->liveness_status, "Liveness probe failed");
        if (probes->liveness_status->failure_count >= probes->liveness_probe->failure_threshold) {
            probes->is_alive = false;  // Mark for restart
        }
    }
    
    return 0;
}

bool pod_startup_complete(pod_probes_t* probes) {
    if (!probes) return true;  // No probes = auto complete
    if (!probes->startup_probe) return true;
    return probes->startup_complete;
}

bool pod_is_ready(pod_probes_t* probes) {
    if (!probes) return true;
    if (!probes->readiness_probe) return true;
    return probes->is_ready;
}

bool pod_is_alive(pod_probes_t* probes) {
    if (!probes) return true;
    if (!probes->liveness_probe) return true;
    return probes->is_alive;
}
