// internal/apiserver/probe_parser.c
// Parse health probes from pod JSON specs

#include "probe_parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static probe_spec_t* parse_probe_spec(json_object* probe_obj, probe_type_t type) {
    if (!probe_obj) return NULL;
    
    probe_spec_t* spec = probe_spec_new(type);
    if (!spec) return NULL;
    
    // Parse handler type (http, tcp, exec)
    json_object* http_obj = NULL;
    json_object* tcp_obj = NULL;
    json_object* exec_obj = NULL;
    
    if (json_object_object_get_ex(probe_obj, "httpGet", &http_obj) && http_obj) {
        // HTTP handler
        const char* path = "/";
        int port = 8080;
        
        json_object* path_obj = NULL;
        if (json_object_object_get_ex(http_obj, "path", &path_obj)) {
            path = json_object_get_string(path_obj);
        }
        
        json_object* port_obj = NULL;
        if (json_object_object_get_ex(http_obj, "port", &port_obj)) {
            port = json_object_get_int(port_obj);
        }
        
        probe_spec_set_http_handler(spec, path ? path : "/", port);
    } else if (json_object_object_get_ex(probe_obj, "tcpSocket", &tcp_obj) && tcp_obj) {
        // TCP handler
        int port = 8080;
        json_object* port_obj = NULL;
        if (json_object_object_get_ex(tcp_obj, "port", &port_obj)) {
            port = json_object_get_int(port_obj);
        }
        probe_spec_set_tcp_handler(spec, port);
    } else if (json_object_object_get_ex(probe_obj, "exec", &exec_obj) && exec_obj) {
        // Exec handler
        json_object* cmd_obj = NULL;
        if (json_object_object_get_ex(exec_obj, "command", &cmd_obj)) {
            // Command is array - join with spaces
            char cmd_str[512] = "";
            int len = json_object_array_length(cmd_obj);
            for (int i = 0; i < len && strlen(cmd_str) < 500; i++) {
                json_object* item = json_object_array_get_idx(cmd_obj, i);
                if (item) {
                    if (i > 0) strcat(cmd_str, " ");
                    strcat(cmd_str, json_object_get_string(item));
                }
            }
            probe_spec_set_exec_handler(spec, cmd_str[0] ? cmd_str : "true");
        }
    }
    
    // Parse timing parameters
    int initial_delay = 0;
    int timeout = 1;
    int period = 10;
    int success_threshold = 1;
    int failure_threshold = 3;
    
    json_object* id_obj = NULL;
    if (json_object_object_get_ex(probe_obj, "initialDelaySeconds", &id_obj)) {
        initial_delay = json_object_get_int(id_obj);
    }
    
    json_object* timeout_obj = NULL;
    if (json_object_object_get_ex(probe_obj, "timeoutSeconds", &timeout_obj)) {
        timeout = json_object_get_int(timeout_obj);
    }
    
    json_object* period_obj = NULL;
    if (json_object_object_get_ex(probe_obj, "periodSeconds", &period_obj)) {
        period = json_object_get_int(period_obj);
    }
    
    json_object* success_obj = NULL;
    if (json_object_object_get_ex(probe_obj, "successThreshold", &success_obj)) {
        success_threshold = json_object_get_int(success_obj);
    }
    
    json_object* failure_obj = NULL;
    if (json_object_object_get_ex(probe_obj, "failureThreshold", &failure_obj)) {
        failure_threshold = json_object_get_int(failure_obj);
    }
    
    probe_spec_set_timing(spec, initial_delay, timeout, period, success_threshold, failure_threshold);
    
    return spec;
}

probe_spec_t* parse_startup_probe(json_object* container) {
    if (!container) return NULL;
    
    json_object* probe_obj = NULL;
    if (json_object_object_get_ex(container, "startupProbe", &probe_obj)) {
        return parse_probe_spec(probe_obj, PROBE_STARTUP);
    }
    return NULL;
}

probe_spec_t* parse_readiness_probe(json_object* container) {
    if (!container) return NULL;
    
    json_object* probe_obj = NULL;
    if (json_object_object_get_ex(container, "readinessProbe", &probe_obj)) {
        return parse_probe_spec(probe_obj, PROBE_READINESS);
    }
    return NULL;
}

probe_spec_t* parse_liveness_probe(json_object* container) {
    if (!container) return NULL;
    
    json_object* probe_obj = NULL;
    if (json_object_object_get_ex(container, "livenessProbe", &probe_obj)) {
        return parse_probe_spec(probe_obj, PROBE_LIVENESS);
    }
    return NULL;
}
