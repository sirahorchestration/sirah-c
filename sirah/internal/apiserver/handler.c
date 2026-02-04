// internal/apiserver/handler.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include "handler.h"
#include "endpoints.h"
#include "patch_handler.h"
#include "query_parser.h"
#include "watch.h"
#include "pod_logs.h"
#include "pod_exec.h"
#include "validation.h"
#include "rbac_middleware.h"
#include "endpoints_etcd_integration.h"
#include "controllers.h"
#include "scheduler_integration.h"


// NOTE: Accept header validation for protobuf compatibility is handled at the HTTP layer
// in server.c (request_callback function). This ensures all API requests properly
// return 406 Not Acceptable if the client requests application/vnd.kubernetes.protobuf encoding.
// See: https://github.com/kubernetes/enhancements/tree/master/keps/sig-api-machinery/555-server-side-apply

// Health check endpoint
static int handle_healthz(char* response_buffer, int* response_code) {
    const char* response = "{\"status\":\"ok\"}";
    strcpy(response_buffer, response);
    *response_code = 200;
    return 0;
}

// Scheduler status endpoint
static int handle_scheduler_status(char* response_buffer, int* response_code) {
    scheduler_stats_t stats = scheduler_get_stats();
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "status", json_object_new_string("active"));
    json_object_object_add(root, "total_scheduled", json_object_new_int(stats.total_pods_scheduled));
    json_object_object_add(root, "total_errors", json_object_new_int(stats.total_scheduling_errors));
    json_object_object_add(root, "last_sync", json_object_new_int(stats.last_sync_time));
    
    const char* json_str = json_object_to_json_string(root);
    strncpy(response_buffer, json_str, 16384 - 1);
    json_object_put(root);
    
    *response_code = 200;
    return 0;
}

// Parse namespace and name from path
static int parse_path(const char* path, char* namespace, char* name) {
    // Parse paths like: /api/v1/namespaces/default/pods/mypod
    // or /api/v1/namespaces/default/pods (create - no name in URL)
    // or /api/v1/pods (all namespaces - no namespace in URL)
    memset(namespace, 0, 256);
    memset(name, 0, 256);

    const char* ns_part = strstr(path, "/namespaces/");
    const char* pod_part = strstr(path, "/pods");

    if (!pod_part) {
        strcpy(namespace, "default");
        return 0;
    }

    // Extract namespace if present
    if (ns_part) {
        const char* ns_start = ns_part + strlen("/namespaces/");
        const char* ns_end = strstr(ns_start, "/");
        if (ns_end) {
            int len = ns_end - ns_start;
            strncpy(namespace, ns_start, len > 255 ? 255 : len);
        }
    } else {
        strcpy(namespace, "default");
    }

    // Extract pod name if present (only if /pods/ has a trailing slash with content)
    const char* name_start = pod_part + strlen("/pods");
    if (name_start[0] == '/' && name_start[1] != '\0') {
        // We have /pods/{name}
        name_start++;  // Skip the initial /
        const char* name_end = strchr(name_start, '/');
        if (name_end) {
            int len = name_end - name_start;
            strncpy(name, name_start, len > 255 ? 255 : len);
        } else {
            // Also check for query string
            const char* query_start = strchr(name_start, '?');
            if (query_start) {
                int len = query_start - name_start;
                strncpy(name, name_start, len > 255 ? 255 : len);
            } else {
                strncpy(name, name_start, 255);
            }
        }
    }
    // Otherwise no specific name (for list/create operations)

    return 0;
}

// Extract query string from path (after ?)
static int extract_query_string(const char* path, char* query_string, int query_len) {
    const char* q = strchr(path, '?');
    if (q) {
        strncpy(query_string, q + 1, query_len - 1);
        query_string[query_len - 1] = '\0';
    } else {
        query_string[0] = '\0';
    }
    return 0;
}

// Route requests to appropriate handlers
int api_handle_request(const char* method, const char* path, const char* body,
                       char* response_buffer, int* response_code) {
    memset(response_buffer, 0, 16384);
    *response_code = 404;

    // Health check
    if (strcmp(path, "/healthz") == 0) {
        return handle_healthz(response_buffer, response_code);
    }
    
    // Scheduler status endpoint
    if (strcmp(path, "/scheduler/status") == 0) {
        return handle_scheduler_status(response_buffer, response_code);
    }

    // API discovery endpoints
    if (strcmp(path, "/api") == 0) {
        const char* resp = "{\"kind\":\"APIVersions\",\"versions\":[\"v1\"],\"serverAddressByClientCIDRs\":[{\"clientCIDR\":\"0.0.0.0/0\",\"serverAddress\":\"localhost:6443\"}]}";
        strcpy(response_buffer, resp);
        *response_code = 200;
        return 0;
    }
    
    // API groups endpoint
    if (strcmp(path, "/apis") == 0) {
        json_object* root = json_object_new_object();
        json_object_object_add(root, "kind", json_object_new_string("APIGroupList"));
        json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
        
        json_object* groups = json_object_new_array();
        
        // apps group
        json_object* apps_group = json_object_new_object();
        json_object_object_add(apps_group, "name", json_object_new_string("apps"));
        json_object* apps_versions = json_object_new_array();
        json_object* apps_v = json_object_new_object();
        json_object_object_add(apps_v, "groupVersion", json_object_new_string("apps/v1"));
        json_object_object_add(apps_v, "version", json_object_new_string("v1"));
        json_object_array_add(apps_versions, apps_v);
        json_object_object_add(apps_group, "versions", apps_versions);
        json_object* apps_pref = json_object_new_object();
        json_object_object_add(apps_pref, "groupVersion", json_object_new_string("apps/v1"));
        json_object_object_add(apps_pref, "version", json_object_new_string("v1"));
        json_object_object_add(apps_group, "preferredVersion", apps_pref);
        json_object_array_add(groups, apps_group);
        
        // batch group
        json_object* batch_group = json_object_new_object();
        json_object_object_add(batch_group, "name", json_object_new_string("batch"));
        json_object* batch_versions = json_object_new_array();
        json_object* batch_v = json_object_new_object();
        json_object_object_add(batch_v, "groupVersion", json_object_new_string("batch/v1"));
        json_object_object_add(batch_v, "version", json_object_new_string("v1"));
        json_object_array_add(batch_versions, batch_v);
        json_object_object_add(batch_group, "versions", batch_versions);
        json_object* batch_pref = json_object_new_object();
        json_object_object_add(batch_pref, "groupVersion", json_object_new_string("batch/v1"));
        json_object_object_add(batch_pref, "version", json_object_new_string("v1"));
        json_object_object_add(batch_group, "preferredVersion", batch_pref);
        json_object_array_add(groups, batch_group);
        
        // autoscaling group
        json_object* auto_group = json_object_new_object();
        json_object_object_add(auto_group, "name", json_object_new_string("autoscaling"));
        json_object* auto_versions = json_object_new_array();
        json_object* auto_v = json_object_new_object();
        json_object_object_add(auto_v, "groupVersion", json_object_new_string("autoscaling/v2"));
        json_object_object_add(auto_v, "version", json_object_new_string("v2"));
        json_object_array_add(auto_versions, auto_v);
        json_object_object_add(auto_group, "versions", auto_versions);
        json_object* auto_pref = json_object_new_object();
        json_object_object_add(auto_pref, "groupVersion", json_object_new_string("autoscaling/v2"));
        json_object_object_add(auto_pref, "version", json_object_new_string("v2"));
        json_object_object_add(auto_group, "preferredVersion", auto_pref);
        json_object_array_add(groups, auto_group);
        
        // rbac group
        json_object* rbac_group = json_object_new_object();
        json_object_object_add(rbac_group, "name", json_object_new_string("rbac.authorization.k8s.io"));
        json_object* rbac_versions = json_object_new_array();
        json_object* rbac_v = json_object_new_object();
        json_object_object_add(rbac_v, "groupVersion", json_object_new_string("rbac.authorization.k8s.io/v1"));
        json_object_object_add(rbac_v, "version", json_object_new_string("v1"));
        json_object_array_add(rbac_versions, rbac_v);
        json_object_object_add(rbac_group, "versions", rbac_versions);
        json_object* rbac_pref = json_object_new_object();
        json_object_object_add(rbac_pref, "groupVersion", json_object_new_string("rbac.authorization.k8s.io/v1"));
        json_object_object_add(rbac_pref, "version", json_object_new_string("v1"));
        json_object_object_add(rbac_group, "preferredVersion", rbac_pref);
        json_object_array_add(groups, rbac_group);
        
        json_object_object_add(root, "groups", groups);
        
        const char* json_str = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
        strncpy(response_buffer, json_str, 16384 - 1);
        json_object_put(root);
        *response_code = 200;
        return 0;
    }
    
    // Version endpoint - standard Kubernetes endpoint (no /api/v1 prefix)
    if (strcmp(path, "/version") == 0) {
        json_object* root = json_object_new_object();
        json_object_object_add(root, "major", json_object_new_string("1"));
        json_object_object_add(root, "minor", json_object_new_string("28"));
        json_object_object_add(root, "gitVersion", json_object_new_string("v1.28.0-sirah"));
        json_object_object_add(root, "gitCommit", json_object_new_string("sirah-custom-build"));
        json_object_object_add(root, "gitTreeState", json_object_new_string("clean"));
        json_object_object_add(root, "buildDate", json_object_new_string("2026-01-31T00:00:00Z"));
        json_object_object_add(root, "goVersion", json_object_new_string("go1.21"));
        json_object_object_add(root, "compiler", json_object_new_string("gc"));
        json_object_object_add(root, "platform", json_object_new_string("linux/amd64"));
        
        const char* json_str = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
        strncpy(response_buffer, json_str, 16384 - 1);
        json_object_put(root);
        *response_code = 200;
        return 0;
    }
    
    // Legacy /api/v1/version endpoint for backward compatibility
    if (strcmp(path, "/api/v1/version") == 0) {
        json_object* root = json_object_new_object();
        json_object_object_add(root, "major", json_object_new_string("1"));
        json_object_object_add(root, "minor", json_object_new_string("28"));
        json_object_object_add(root, "gitVersion", json_object_new_string("v1.28.0-sirah"));
        json_object_object_add(root, "gitCommit", json_object_new_string("sirah-custom-build"));
        json_object_object_add(root, "gitTreeState", json_object_new_string("clean"));
        json_object_object_add(root, "buildDate", json_object_new_string("2026-01-31T00:00:00Z"));
        json_object_object_add(root, "goVersion", json_object_new_string("go1.21"));
        json_object_object_add(root, "compiler", json_object_new_string("gc"));
        json_object_object_add(root, "platform", json_object_new_string("linux/amd64"));
        
        const char* json_str = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
        strncpy(response_buffer, json_str, 16384 - 1);
        json_object_put(root);
        *response_code = 200;
        return 0;
    }
    
    // OpenAPI and Swagger documentation endpoints
    if (strncmp(path, "/openapi", 8) == 0 || strncmp(path, "/docs", 5) == 0) {
        extern int openapi_handle_request(const char* path, char* response_buffer, int* response_code);
        return openapi_handle_request(path, response_buffer, response_code);
    }
    
    // Dashboard endpoints (currently disabled due to build issues)
    if (strncmp(path, "/dashboard", 10) == 0 || strncmp(path, "/api/dashboard", 14) == 0) {
        const char* response = "{\"status\":\"dashboard_disabled\"}";
        strcpy(response_buffer, response);
        *response_code = 200;
        return 0;
    }
    
    // Metrics endpoint (Prometheus format)
    if (strcmp(path, "/metrics") == 0) {
        char metrics[8192] = "";
        snprintf(metrics, sizeof(metrics),
            "# HELP sirah_pods_total Total pods in cluster\n"
            "# TYPE sirah_pods_total gauge\n"
            "sirah_pods_total %d\n"
            "# HELP sirah_api_requests_total Total API requests\n"
            "# TYPE sirah_api_requests_total counter\n"
            "sirah_api_requests_total 0\n"
            "# HELP sirah_pod_creation_duration_seconds Pod creation latency\n"
            "# TYPE sirah_pod_creation_duration_seconds histogram\n"
            "sirah_pod_creation_duration_seconds_bucket{le=\"0.1\"} 0\n"
            "sirah_pod_creation_duration_seconds_bucket{le=\"0.5\"} 0\n"
            "sirah_pod_creation_duration_seconds_bucket{le=\"1.0\"} 0\n"
            "sirah_pod_creation_duration_seconds_bucket{le=\"+Inf\"} 0\n",
            0);  // Pod count from etcd - not implemented yet
        
        strncpy(response_buffer, metrics, 16383);
        *response_code = 200;
        return 0;
    }

    if (strcmp(path, "/api/v1") == 0) {
        // Comprehensive resource list for kubectl api-resources
        const char* resp = "{"
            "\"kind\":\"APIResourceList\","
            "\"apiVersion\":\"v1\","
            "\"groupVersion\":\"v1\","
            "\"resources\":["
                "{\"name\":\"pods\",\"singularName\":\"pod\",\"namespaced\":true,\"kind\":\"Pod\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\",\"patch\",\"update\"]},"
                "{\"name\":\"nodes\",\"singularName\":\"node\",\"namespaced\":false,\"kind\":\"Node\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]},"
                "{\"name\":\"services\",\"singularName\":\"service\",\"namespaced\":true,\"kind\":\"Service\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]},"
                "{\"name\":\"configmaps\",\"singularName\":\"configmap\",\"namespaced\":true,\"kind\":\"ConfigMap\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]},"
                "{\"name\":\"secrets\",\"singularName\":\"secret\",\"namespaced\":true,\"kind\":\"Secret\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]},"
                "{\"name\":\"persistentvolumes\",\"singularName\":\"persistentvolume\",\"namespaced\":false,\"kind\":\"PersistentVolume\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]},"
                "{\"name\":\"persistentvolumeclaims\",\"singularName\":\"persistentvolumeclaim\",\"namespaced\":true,\"kind\":\"PersistentVolumeClaim\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]},"
                "{\"name\":\"namespaces\",\"singularName\":\"namespace\",\"namespaced\":false,\"kind\":\"Namespace\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]},"
                "{\"name\":\"events\",\"singularName\":\"event\",\"namespaced\":true,\"kind\":\"Event\",\"verbs\":[\"get\",\"list\"]},"
                "{\"name\":\"deployments\",\"singularName\":\"deployment\",\"namespaced\":true,\"kind\":\"Deployment\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]},"
                "{\"name\":\"statefulsets\",\"singularName\":\"statefulset\",\"namespaced\":true,\"kind\":\"StatefulSet\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]},"
                "{\"name\":\"daemonsets\",\"singularName\":\"daemonset\",\"namespaced\":true,\"kind\":\"DaemonSet\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]},"
                "{\"name\":\"jobs\",\"singularName\":\"job\",\"namespaced\":true,\"kind\":\"Job\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]},"
                "{\"name\":\"cronjobs\",\"singularName\":\"cronjob\",\"namespaced\":true,\"kind\":\"CronJob\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]}"
            "]"
        "}";
        strcpy(response_buffer, resp);
        *response_code = 200;
        return 0;
    }

    // Apps API group discovery
    if (strcmp(path, "/apis/apps/v1") == 0) {
        const char* resp = "{"
            "\"kind\":\"APIResourceList\","
            "\"apiVersion\":\"v1\","
            "\"groupVersion\":\"apps/v1\","
            "\"resources\":["
                "{\"name\":\"deployments\",\"singularName\":\"deployment\",\"namespaced\":true,\"kind\":\"Deployment\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]},"
                "{\"name\":\"statefulsets\",\"singularName\":\"statefulset\",\"namespaced\":true,\"kind\":\"StatefulSet\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]},"
                "{\"name\":\"daemonsets\",\"singularName\":\"daemonset\",\"namespaced\":true,\"kind\":\"DaemonSet\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]}"
            "]"
        "}";
        strcpy(response_buffer, resp);
        *response_code = 200;
        return 0;
    }

    // Batch API group discovery
    if (strcmp(path, "/apis/batch/v1") == 0) {
        const char* resp = "{"
            "\"kind\":\"APIResourceList\","
            "\"apiVersion\":\"v1\","
            "\"groupVersion\":\"batch/v1\","
            "\"resources\":["
                "{\"name\":\"jobs\",\"singularName\":\"job\",\"namespaced\":true,\"kind\":\"Job\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]},"
                "{\"name\":\"cronjobs\",\"singularName\":\"cronjob\",\"namespaced\":true,\"kind\":\"CronJob\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]}"
            "]"
        "}";
        strcpy(response_buffer, resp);
        *response_code = 200;
        return 0;
    }

    // Autoscaling API group discovery
    if (strcmp(path, "/apis/autoscaling/v2") == 0) {
        const char* resp = "{"
            "\"kind\":\"APIResourceList\","
            "\"apiVersion\":\"v1\","
            "\"groupVersion\":\"autoscaling/v2\","
            "\"resources\":["
                "{\"name\":\"horizontalpodautoscalers\",\"singularName\":\"horizontalpodautoscaler\",\"namespaced\":true,\"kind\":\"HorizontalPodAutoscaler\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]}"
            "]"
        "}";
        strcpy(response_buffer, resp);
        *response_code = 200;
        return 0;
    }

    // RBAC API group discovery
    if (strcmp(path, "/apis/rbac.authorization.k8s.io/v1") == 0) {
        const char* resp = "{"
            "\"kind\":\"APIResourceList\","
            "\"apiVersion\":\"v1\","
            "\"groupVersion\":\"rbac.authorization.k8s.io/v1\","
            "\"resources\":["
                "{\"name\":\"roles\",\"singularName\":\"role\",\"namespaced\":true,\"kind\":\"Role\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]},"
                "{\"name\":\"rolebindings\",\"singularName\":\"rolebinding\",\"namespaced\":true,\"kind\":\"RoleBinding\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]},"
                "{\"name\":\"clusterroles\",\"singularName\":\"clusterrole\",\"namespaced\":false,\"kind\":\"ClusterRole\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]},"
                "{\"name\":\"clusterrolebindings\",\"singularName\":\"clusterrolebinding\",\"namespaced\":false,\"kind\":\"ClusterRoleBinding\",\"verbs\":[\"create\",\"delete\",\"get\",\"list\"]}"
            "]"
        "}";
        strcpy(response_buffer, resp);
        *response_code = 200;
        return 0;
    }

    // Pods endpoints
    if (strstr(path, "/api/v1/") && strstr(path, "/pods")) {
        char namespace[256] = {0};
        char pod_name[256] = {0};
        char query_string[512] = {0};
        parse_path(path, namespace, pod_name);
        extract_query_string(path, query_string, sizeof(query_string));

        // Check for bind endpoint
        if (strcmp(method, "POST") == 0 && strstr(path, "/bind")) {
            // POST /pods/{name}/bind (scheduler binding)
            // Use etcd-backed patch endpoint for pod binding
            const char* content_type = "application/json-patch+json";
            endpoint_patch_pod_etcd(namespace, pod_name, body, content_type, response_buffer, response_code);
            return 0;
        }

        // Check for log endpoint
        if (strcmp(method, "GET") == 0 && strstr(path, "/log")) {
            // GET /pods/{name}/log
            log_query_params_t params = {0};
            parse_log_params(query_string, &params);
            
            // Extract container name from query params or use first container
            char container_name[256] = {0};
            
            // Check for container=name query parameter
            const char* container_param = strstr(query_string, "container=");
            if (container_param) {
                const char* value = container_param + strlen("container=");
                const char* end = strchr(value, '&');
                int len = end ? (end - value) : strlen(value);
                strncpy(container_name, value, len > 255 ? 255 : len);
            } else {
                // Try to get first container from pod spec - fetch from etcd
                int found = 0;
                
                // Fetch from etcd to get pod details
                char pod_response[16384] = {0};
                int dummy_code = 0;
                endpoint_get_pod_etcd(namespace, pod_name, pod_response, &dummy_code);
                    
                if (dummy_code == 200 && strlen(pod_response) > 0) {
                    json_object* pod_obj = json_tokener_parse(pod_response);
                    if (pod_obj) {
                        json_object* spec_obj = json_object_object_get(pod_obj, "spec");
                        if (spec_obj) {
                            json_object* containers_obj = json_object_object_get(spec_obj, "containers");
                            if (containers_obj && json_object_is_type(containers_obj, json_type_array)) {
                                if (json_object_array_length(containers_obj) > 0) {
                                    json_object* first_container = json_object_array_get_idx(containers_obj, 0);
                                    if (first_container) {
                                        json_object* name_obj = json_object_object_get(first_container, "name");
                                        if (name_obj) {
                                            const char* name = json_object_get_string(name_obj);
                                            if (name) {
                                                strcpy(container_name, name);
                                                found = 1;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        json_object_put(pod_obj);
                    }
                }
            }
            
            // If still no container name, default to "app" (common Sirah convention)
            if (strlen(container_name) == 0) {
                strcpy(container_name, "app");
            }
            
            endpoint_get_pod_logs(namespace, pod_name, container_name, &params, response_buffer, response_code);
            return 0;
        }

        // Check for exec endpoint
        if (strcmp(method, "POST") == 0 && strstr(path, "/exec")) {
            // POST /pods/{name}/exec
            exec_request_t exec_req = {0};
            parse_exec_request(body, &exec_req);
            strcpy(exec_req.namespace, namespace);
            strcpy(exec_req.pod_name, pod_name);
            
            exec_response_t exec_resp = {0};
            endpoint_exec_pod(namespace, pod_name, exec_req.container_name, exec_req.command, &exec_resp);
            build_exec_response(&exec_resp, response_buffer);
            *response_code = 200;
            return 0;
        }

        // Check for status subresource - PATCH /pods/{name}/status
        if (strcmp(method, "PATCH") == 0 && strstr(path, "/status") && strlen(pod_name) > 0) {
            // PATCH /pods/{name}/status - Update pod status
            // Parse the JSON body for status fields
            fprintf(stderr, "[API] PATCH /status received: pod=%s, body_len=%zu, body=%s\n",
                pod_name, body ? strlen(body) : 0, body ? body : "(null)");
            fflush(stderr);
            
            if (!body || strlen(body) == 0) {
                *response_code = 400;
                strcpy(response_buffer, "{\"error\":\"empty body\"}");
                return -1;
            }
            
            json_object* patch_obj = json_tokener_parse(body);
            if (!patch_obj) {
                *response_code = 400;
                strcpy(response_buffer, "{\"error\":\"invalid JSON\"}");
                fprintf(stderr, "[API] Failed to parse JSON body for PATCH /status\n");
                fflush(stderr);
                return -1;
            }
            
            // Use etcd-backed PATCH for status updates
            // Status subresource updates go through etcd
            const char* content_type = "application/json-patch+json";
            endpoint_patch_pod_etcd(namespace, pod_name, body, content_type, response_buffer, response_code);
            json_object_put(patch_obj);
            return (*response_code == 200) ? 0 : -1;
        }

        // Watch endpoint
        if (strcmp(method, "GET") == 0 && strstr(query_string, "watch=true")) {
            endpoint_watch_pods(namespace, query_string, response_buffer, response_code);
            return 0;
        }

        if (strcmp(method, "GET") == 0) {
            if (strlen(pod_name) > 0) {
                // GET /pods/{name}
                endpoint_get_pod_etcd(namespace, pod_name, response_buffer, response_code);
            } else {
                // GET /pods (list) - with filtering support
                endpoint_list_pods_etcd(namespace, response_buffer, response_code);
            }
            return 0;
        }

        if (strcmp(method, "POST") == 0) {
            // POST /pods (create)
            // Check RBAC authorization first (disabled for testing)
            /*
            rbac_policy_decision_t rbac_decision;
            const char* auth_header = "";  // Would be extracted from headers in real implementation
            int auth_result = rbac_middleware_check_request(
                auth_header, method, path, "", "pods", "", namespace, &rbac_decision);
            
            if (auth_result != 0 && rbac_decision.decision == RBAC_DENY) {
                // Authorization denied
                char reason[512];
                snprintf(reason, sizeof(reason), "User cannot create pods in namespace '%s': %s",
                         namespace, rbac_decision.reason);
                rbac_middleware_format_denial(auth_header, "pods", namespace, "create",
                                            reason, response_buffer, response_code);
                return 0;
            }
            */
            
            // Validate pod spec
            json_object* pod_spec = json_tokener_parse(body);
            if (pod_spec) {
                json_object* metadata = json_object_object_get(pod_spec, "metadata");
                json_object* spec = json_object_object_get(pod_spec, "spec");
                
                // Validate metadata
                validation_result_t* meta_result = validate_pod_metadata(metadata);
                // Validate spec
                validation_result_t* spec_result = validate_pod_spec(spec);
                
                if (!meta_result->valid || !spec_result->valid) {
                    // Return validation errors
                    json_object* error_resp = json_object_new_object();
                    json_object* errors_array = json_object_new_array();
                    
                    for (int i = 0; i < meta_result->error_count; i++) {
                        json_object* err = json_object_new_object();
                        json_object_object_add(err, "field", json_object_new_string(meta_result->errors[i]->field));
                        json_object_object_add(err, "message", json_object_new_string(meta_result->errors[i]->message));
                        json_object_array_add(errors_array, err);
                    }
                    for (int i = 0; i < spec_result->error_count; i++) {
                        json_object* err = json_object_new_object();
                        json_object_object_add(err, "field", json_object_new_string(spec_result->errors[i]->field));
                        json_object_object_add(err, "message", json_object_new_string(spec_result->errors[i]->message));
                        json_object_array_add(errors_array, err);
                    }
                    
                    json_object_object_add(error_resp, "valid", json_object_new_boolean(0));
                    json_object_object_add(error_resp, "errors", errors_array);
                    
                    const char* json_str = json_object_to_json_string_ext(error_resp, JSON_C_TO_STRING_PLAIN);
                    strncpy(response_buffer, json_str, 16383);
                    json_object_put(error_resp);
                    validation_result_free(meta_result);
                    validation_result_free(spec_result);
                    json_object_put(pod_spec);
                    *response_code = 400;
                    return 0;
                }
                
                validation_result_free(meta_result);
                validation_result_free(spec_result);
                json_object_put(pod_spec);
            }
            
            endpoint_create_pod_etcd(namespace, body, response_buffer, response_code);
            return 0;
        }

        if (strcmp(method, "PATCH") == 0 && strlen(pod_name) > 0) {
            // PATCH /pods/{name}
            // Check RBAC authorization first (disabled for testing)
            /*
            rbac_policy_decision_t rbac_decision;
            const char* auth_header = "";  // Would be extracted from headers in real implementation
            int auth_result = rbac_middleware_check_request(
                auth_header, method, path, "", "pods", pod_name, namespace, &rbac_decision);
            
            if (auth_result != 0 && rbac_decision.decision == RBAC_DENY) {
                // Authorization denied
                char reason[512];
                snprintf(reason, sizeof(reason), "User cannot patch pod '%s' in namespace '%s': %s",
                         pod_name, namespace, rbac_decision.reason);
                rbac_middleware_format_denial(auth_header, "pods", namespace, "patch",
                                            reason, response_buffer, response_code);
                return 0;
            }
            */
            
            const char* content_type = "";  // Would be extracted from headers in real implementation
            endpoint_patch_pod_etcd(namespace, pod_name, body, content_type, response_buffer, response_code);
            return 0;
        }

        if (strcmp(method, "DELETE") == 0 && strlen(pod_name) > 0) {
            // DELETE /pods/{name}
            // Check RBAC authorization first (disabled for testing)
            /*
            rbac_policy_decision_t rbac_decision;
            const char* auth_header = "";  // Would be extracted from headers in real implementation
            int auth_result = rbac_middleware_check_request(
                auth_header, method, path, "", "pods", pod_name, namespace, &rbac_decision);
            
            if (auth_result != 0 && rbac_decision.decision == RBAC_DENY) {
                // Authorization denied
                char reason[512];
                snprintf(reason, sizeof(reason), "User cannot delete pod '%s' in namespace '%s': %s",
                         pod_name, namespace, rbac_decision.reason);
                rbac_middleware_format_denial(auth_header, "pods", namespace, "delete",
                                            reason, response_buffer, response_code);
                return 0;
            }
            */
            
            endpoint_delete_pod_etcd(namespace, pod_name, response_buffer, response_code);
            return 0;
        }
    }

    // Nodes endpoints
    if (strstr(path, "/api/v1/") && strstr(path, "/nodes")) {
        if (strcmp(method, "GET") == 0) {
            if (strstr(path, "/nodes/") && !strstr(path, "/nodes/")) {
                // GET /nodes/{name}
                const char* node_name = strstr(path, "/nodes/") + strlen("/nodes/");
                endpoint_get_node(node_name, response_buffer, response_code);
            } else {
                // GET /nodes (list)
                endpoint_list_nodes(response_buffer, response_code);
            }
            return 0;
        }
        
        if (strcmp(method, "POST") == 0 && strstr(path, "/nodes/register")) {
            // POST /api/v1/nodes/register (register new node)
            endpoint_register_node(body, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "POST") == 0 && strstr(path, "/nodes/") && strstr(path, "/heartbeat")) {
            // POST /api/v1/nodes/{name}/heartbeat
            const char* node_name = strstr(path, "/nodes/") + strlen("/nodes/");
            char* slash = strchr(node_name, '/');
            char name_buf[256] = {0};
            if (slash) {
                strncpy(name_buf, node_name, slash - node_name);
            } else {
                strcpy(name_buf, node_name);
            }
            endpoint_node_heartbeat(name_buf, body, response_buffer, response_code);
            return 0;
        }
    }

    // Services endpoints
    if (strstr(path, "/api/v1/") && strstr(path, "/services")) {
        char namespace[256] = {0};
        char name[256] = {0};
        char query_string[512] = {0};
        
        // Parse namespace and name
        const char* ns_part = strstr(path, "/namespaces/");
        if (ns_part) {
            const char* ns_start = ns_part + strlen("/namespaces/");
            const char* ns_end = strchr(ns_start, '/');
            if (ns_end) {
                int len = ns_end - ns_start;
                strncpy(namespace, ns_start, len > 255 ? 255 : len);
            }
        } else {
            strcpy(namespace, "default");
        }
        
        extract_query_string(path, query_string, sizeof(query_string));
        
        const char* svc_part = strstr(path, "/services/");
        if (svc_part && strlen(svc_part) > strlen("/services/")) {
            const char* name_start = svc_part + strlen("/services/");
            const char* name_end = strchr(name_start, '?');
            if (name_end) {
                int len = name_end - name_start;
                strncpy(name, name_start, len > 255 ? 255 : len);
            } else {
                strncpy(name, name_start, 255);
            }
        }
        
        // Watch endpoint
        if (strcmp(method, "GET") == 0 && strstr(query_string, "watch=true")) {
            endpoint_watch_services(namespace, query_string, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "GET") == 0) {
            if (strlen(name) > 0) {
                endpoint_get_service_etcd(namespace, name, response_buffer, response_code);
            } else {
                endpoint_list_services_etcd(namespace, response_buffer, response_code);
            }
            return 0;
        }
        
        if (strcmp(method, "POST") == 0) {
            endpoint_create_service_etcd(namespace, body, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "PATCH") == 0 && strlen(name) > 0) {
            // PATCH /services/{name}
            const char* content_type = "";  // Would be extracted from headers in real implementation
            endpoint_patch_service_etcd(namespace, name, body, content_type, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "DELETE") == 0 && strlen(name) > 0) {
            endpoint_delete_service_etcd(namespace, name, response_buffer, response_code);
            return 0;
        }
    }

    // Deployments endpoints (apps/v1)
    if (strstr(path, "/apis/apps/v1") && strstr(path, "/deployments")) {
        char namespace[256] = {0};
        char name[256] = {0};
        char query_string[512] = {0};
        
        const char* ns_part = strstr(path, "/namespaces/");
        if (ns_part) {
            const char* ns_start = ns_part + strlen("/namespaces/");
            const char* ns_end = strchr(ns_start, '/');
            if (ns_end) {
                int len = ns_end - ns_start;
                strncpy(namespace, ns_start, len > 255 ? 255 : len);
            }
        } else {
            strcpy(namespace, "default");
        }
        
        extract_query_string(path, query_string, sizeof(query_string));
        
        const char* deploy_part = strstr(path, "/deployments/");
        if (deploy_part && strlen(deploy_part) > strlen("/deployments/")) {
            const char* name_start = deploy_part + strlen("/deployments/");
            const char* name_end = strchr(name_start, '?');
            if (name_end) {
                int len = name_end - name_start;
                strncpy(name, name_start, len > 255 ? 255 : len);
            } else {
                strncpy(name, name_start, 255);
            }
        }
        
        // Watch endpoint
        if (strcmp(method, "GET") == 0 && strstr(query_string, "watch=true")) {
            endpoint_watch_deployments(namespace, query_string, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "GET") == 0) {
            if (strlen(name) > 0) {
                endpoint_get_deployment_etcd(namespace, name, response_buffer, response_code);
            } else {
                endpoint_list_deployments_etcd(namespace, response_buffer, response_code);
            }
            return 0;
        }
        
        if (strcmp(method, "POST") == 0) {
            endpoint_create_deployment_etcd(namespace, body, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "PUT") == 0 && strlen(name) > 0) {
            endpoint_update_deployment(namespace, name, body, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "PATCH") == 0 && strlen(name) > 0) {
            const char* content_type = "";
            endpoint_patch_deployment_etcd(namespace, name, body, content_type, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "DELETE") == 0 && strlen(name) > 0) {
            endpoint_delete_deployment_etcd(namespace, name, response_buffer, response_code);
            return 0;
        }
    }

    // StatefulSet endpoints (apps/v1)
    if (strstr(path, "/apis/apps/v1") && strstr(path, "/statefulsets")) {
        char namespace[256] = {0};
        char name[256] = {0};
        
        const char* ns_part = strstr(path, "/namespaces/");
        if (ns_part) {
            const char* ns_start = ns_part + strlen("/namespaces/");
            const char* ns_end = strchr(ns_start, '/');
            if (ns_end) {
                int len = ns_end - ns_start;
                strncpy(namespace, ns_start, len > 255 ? 255 : len);
            }
        } else {
            strcpy(namespace, "default");
        }
        
        const char* ss_part = strstr(path, "/statefulsets/");
        if (ss_part && strlen(ss_part) > strlen("/statefulsets/")) {
            const char* name_start = ss_part + strlen("/statefulsets/");
            const char* name_end = strchr(name_start, '?');
            if (name_end) {
                int len = name_end - name_start;
                strncpy(name, name_start, len > 255 ? 255 : len);
            } else {
                strncpy(name, name_start, 255);
            }
        }
        
        if (strcmp(method, "GET") == 0) {
            if (strlen(name) > 0) {
                endpoint_get_statefulset_etcd(namespace, name, response_buffer, response_code);
            } else {
                endpoint_list_statefulsets_etcd(namespace, response_buffer, response_code);
            }
            return 0;
        }
        
        if (strcmp(method, "POST") == 0) {
            endpoint_create_statefulset_etcd(namespace, body, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "DELETE") == 0 && strlen(name) > 0) {
            endpoint_delete_statefulset_etcd(namespace, name, response_buffer, response_code);
            return 0;
        }
    }

    // Job endpoints (batch/v1)
    if (strstr(path, "/apis/batch/v1") && strstr(path, "/jobs")) {
        char namespace[256] = {0};
        char name[256] = {0};
        
        const char* ns_part = strstr(path, "/namespaces/");
        if (ns_part) {
            const char* ns_start = ns_part + strlen("/namespaces/");
            const char* ns_end = strchr(ns_start, '/');
            if (ns_end) {
                int len = ns_end - ns_start;
                strncpy(namespace, ns_start, len > 255 ? 255 : len);
            }
        } else {
            strcpy(namespace, "default");
        }
        
        const char* job_part = strstr(path, "/jobs/");
        if (job_part && strlen(job_part) > strlen("/jobs/")) {
            const char* name_start = job_part + strlen("/jobs/");
            const char* name_end = strchr(name_start, '?');
            if (name_end) {
                int len = name_end - name_start;
                strncpy(name, name_start, len > 255 ? 255 : len);
            } else {
                strncpy(name, name_start, 255);
            }
        }
        
        if (strcmp(method, "GET") == 0) {
            if (strlen(name) > 0) {
                endpoint_get_job_etcd(namespace, name, response_buffer, response_code);
            } else {
                endpoint_list_jobs_etcd(namespace, response_buffer, response_code);
            }
            return 0;
        }
        
        if (strcmp(method, "POST") == 0) {
            endpoint_create_job_etcd(namespace, body, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "DELETE") == 0 && strlen(name) > 0) {
            endpoint_delete_job_etcd(namespace, name, response_buffer, response_code);
            return 0;
        }
    }

    // Namespace endpoints - MUST NOT match paths that have resources WITHIN a namespace
    // Only match: /api/v1/namespaces or /api/v1/namespaces/{name}
    // Do NOT match: /api/v1/namespaces/{name}/pods, /api/v1/namespaces/{name}/configmaps, etc.
    if (strstr(path, "/api/v1") && strstr(path, "/namespaces")) {
        // Check if this is a namespace-scoped resource (not the namespace endpoint itself)
        // These patterns indicate a resource within a namespace:
        // - /namespaces/{ns}/pods
        // - /namespaces/{ns}/configmaps
        // - /namespaces/{ns}/secrets
        // - /namespaces/{ns}/services
        // - etc.
        if (strstr(path, "/namespaces/") && 
            (strstr(path, "/pods") || strstr(path, "/configmaps") || strstr(path, "/secrets") ||
             strstr(path, "/services") || strstr(path, "/deployments") || strstr(path, "/statefulsets") ||
             strstr(path, "/jobs") || strstr(path, "/events"))) {
            // This is a resource within a namespace, NOT a namespace endpoint
            // Skip this handler and let the specific resource handler process it
        } else {
            // This is a namespace endpoint
            char name[256] = {0};
            
            const char* ns_part = strstr(path, "/namespaces/");
            if (ns_part && strlen(ns_part) > strlen("/namespaces/")) {
                const char* name_start = ns_part + strlen("/namespaces/");
                const char* name_end = strchr(name_start, '?');
                if (name_end) {
                    int len = name_end - name_start;
                    strncpy(name, name_start, len > 255 ? 255 : len);
                } else {
                    // Also stop at next slash (in case of /namespaces/default/)
                    const char* slash = strchr(name_start, '/');
                    if (slash) {
                        int len = slash - name_start;
                        strncpy(name, name_start, len > 255 ? 255 : len);
                    } else {
                        strncpy(name, name_start, 255);
                    }
                }
            }
            
            if (strcmp(method, "GET") == 0) {
                if (strlen(name) > 0) {
                    endpoint_get_namespace(name, response_buffer, response_code);
                } else {
                    endpoint_list_namespaces(response_buffer, response_code);
                }
                return 0;
            }
            
            if (strcmp(method, "POST") == 0) {
                endpoint_create_namespace(body, response_buffer, response_code);
                return 0;
            }
            
            if (strcmp(method, "DELETE") == 0 && strlen(name) > 0) {
                endpoint_delete_namespace(name, response_buffer, response_code);
                return 0;
            }
        }
    }

    // ConfigMap endpoints
    if (strstr(path, "/api/v1") && strstr(path, "/configmaps")) {
        char namespace[256] = {0};
        char name[256] = {0};
        char query_string[512] = {0};
        
        const char* ns_part = strstr(path, "/namespaces/");
        if (ns_part) {
            const char* ns_start = ns_part + strlen("/namespaces/");
            const char* ns_end = strchr(ns_start, '/');
            if (ns_end) {
                int len = ns_end - ns_start;
                strncpy(namespace, ns_start, len > 255 ? 255 : len);
            }
        } else {
            strcpy(namespace, "default");
        }
        
        extract_query_string(path, query_string, sizeof(query_string));
        
        const char* cm_part = strstr(path, "/configmaps/");
        if (cm_part && strlen(cm_part) > strlen("/configmaps/")) {
            const char* name_start = cm_part + strlen("/configmaps/");
            const char* name_end = strchr(name_start, '?');
            if (name_end) {
                int len = name_end - name_start;
                strncpy(name, name_start, len > 255 ? 255 : len);
            } else {
                strncpy(name, name_start, 255);
            }
        }
        
        if (strcmp(method, "GET") == 0) {
            if (strlen(name) > 0) {
                endpoint_get_configmap_etcd(namespace, name, response_buffer, response_code);
            } else {
                endpoint_list_configmaps_etcd(namespace, response_buffer, response_code);
            }
            return 0;
        }
        
        if (strcmp(method, "POST") == 0) {
            endpoint_create_configmap_etcd(namespace, body, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "PATCH") == 0 && strlen(name) > 0) {
            // PATCH /configmaps/{name}
            const char* content_type = "";  // Would be extracted from headers in real implementation
            endpoint_patch_configmap_etcd(namespace, name, body, content_type, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "DELETE") == 0 && strlen(name) > 0) {
            endpoint_delete_configmap_etcd(namespace, name, response_buffer, response_code);
            return 0;
        }
    }

    // Secret endpoints
    if (strstr(path, "/api/v1") && strstr(path, "/secrets")) {
        char namespace[256] = {0};
        char name[256] = {0};
        char query_string[512] = {0};
        
        const char* ns_part = strstr(path, "/namespaces/");
        if (ns_part) {
            const char* ns_start = ns_part + strlen("/namespaces/");
            const char* ns_end = strchr(ns_start, '/');
            if (ns_end) {
                int len = ns_end - ns_start;
                strncpy(namespace, ns_start, len > 255 ? 255 : len);
            }
        } else {
            strcpy(namespace, "default");
        }
        
        extract_query_string(path, query_string, sizeof(query_string));
        
        const char* sec_part = strstr(path, "/secrets/");
        if (sec_part && strlen(sec_part) > strlen("/secrets/")) {
            const char* name_start = sec_part + strlen("/secrets/");
            const char* name_end = strchr(name_start, '?');
            if (name_end) {
                int len = name_end - name_start;
                strncpy(name, name_start, len > 255 ? 255 : len);
            } else {
                strncpy(name, name_start, 255);
            }
        }
        
        if (strcmp(method, "GET") == 0) {
            if (strlen(name) > 0) {
                endpoint_get_secret_etcd(namespace, name, response_buffer, response_code);
            } else {
                endpoint_list_secrets_etcd(namespace, response_buffer, response_code);
            }
            return 0;
        }
        
        if (strcmp(method, "POST") == 0) {
            endpoint_create_secret_etcd(namespace, body, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "PATCH") == 0 && strlen(name) > 0) {
            // PATCH /secrets/{name}
            const char* content_type = "";  // Would be extracted from headers in real implementation
            endpoint_patch_secret_etcd(namespace, name, body, content_type, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "DELETE") == 0 && strlen(name) > 0) {
            endpoint_delete_secret_etcd(namespace, name, response_buffer, response_code);
            return 0;
        }
    }


    // PersistentVolume endpoints
    if (strstr(path, "/api/v1") && strstr(path, "/persistentvolumes")) {
        char name[256] = {0};
        
        const char* pv_part = strstr(path, "/persistentvolumes/");
        if (pv_part && strlen(pv_part) > strlen("/persistentvolumes/")) {
            const char* name_start = pv_part + strlen("/persistentvolumes/");
            const char* name_end = strchr(name_start, '?');
            if (name_end) {
                int len = name_end - name_start;
                strncpy(name, name_start, len > 255 ? 255 : len);
            } else {
                strncpy(name, name_start, 255);
            }
        }
        
        if (strcmp(method, "GET") == 0) {
            if (strlen(name) > 0) {
                endpoint_get_pv_etcd(name, response_buffer, response_code);
            } else {
                endpoint_list_pv_etcd(response_buffer, response_code);
            }
            return 0;
        }
        
        if (strcmp(method, "POST") == 0) {
            endpoint_create_pv_etcd(body, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "PATCH") == 0 && strlen(name) > 0) {
            // PATCH /persistentvolumes/{name}
            const char* content_type = "";  // Would be extracted from headers in real implementation
            endpoint_patch_pv_etcd(name, body, content_type, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "DELETE") == 0 && strlen(name) > 0) {
            endpoint_delete_pv_etcd(name, response_buffer, response_code);
            return 0;
        }
    }

    // PersistentVolumeClaim endpoints
    if (strstr(path, "/api/v1") && strstr(path, "/persistentvolumeclaims")) {
        char namespace[256] = {0};
        char name[256] = {0};
        
        const char* ns_part = strstr(path, "/namespaces/");
        if (ns_part) {
            const char* ns_start = ns_part + strlen("/namespaces/");
            const char* ns_end = strchr(ns_start, '/');
            if (ns_end) {
                int len = ns_end - ns_start;
                strncpy(namespace, ns_start, len > 255 ? 255 : len);
            }
        } else {
            strcpy(namespace, "default");
        }
        
        const char* pvc_part = strstr(path, "/persistentvolumeclaims/");
        if (pvc_part && strlen(pvc_part) > strlen("/persistentvolumeclaims/")) {
            const char* name_start = pvc_part + strlen("/persistentvolumeclaims/");
            const char* name_end = strchr(name_start, '?');
            if (name_end) {
                int len = name_end - name_start;
                strncpy(name, name_start, len > 255 ? 255 : len);
            } else {
                strncpy(name, name_start, 255);
            }
        }
        
        if (strcmp(method, "GET") == 0) {
            if (strlen(name) > 0) {
                endpoint_get_pvc_etcd(namespace, name, response_buffer, response_code);
            } else {
                endpoint_list_pvc_etcd(namespace, response_buffer, response_code);
            }
            return 0;
        }
        
        if (strcmp(method, "POST") == 0) {
            endpoint_create_pvc_etcd(namespace, body, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "PATCH") == 0 && strlen(name) > 0) {
            // PATCH /persistentvolumeclaims/{name}
            const char* content_type = "";  // Would be extracted from headers in real implementation
            endpoint_patch_pvc_etcd(namespace, name, body, content_type, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "DELETE") == 0 && strlen(name) > 0) {
            endpoint_delete_pvc_etcd(namespace, name, response_buffer, response_code);
            return 0;
        }
    }

    // Event endpoints
    if (strstr(path, "/api/v1") && strstr(path, "/events")) {
        char namespace[256] = {0};
        char name[256] = {0};
        
        const char* ns_part = strstr(path, "/namespaces/");
        if (ns_part) {
            const char* ns_start = ns_part + strlen("/namespaces/");
            const char* ns_end = strchr(ns_start, '/');
            if (ns_end) {
                int len = ns_end - ns_start;
                strncpy(namespace, ns_start, len > 255 ? 255 : len);
            }
        } else {
            strcpy(namespace, "default");
        }
        
        const char* ev_part = strstr(path, "/events/");
        if (ev_part && strlen(ev_part) > strlen("/events/")) {
            const char* name_start = ev_part + strlen("/events/");
            const char* name_end = strchr(name_start, '?');
            if (name_end) {
                int len = name_end - name_start;
                strncpy(name, name_start, len > 255 ? 255 : len);
            } else {
                strncpy(name, name_start, 255);
            }
        }
        
        if (strcmp(method, "GET") == 0) {
            if (strlen(name) > 0) {
                endpoint_get_event(namespace, name, response_buffer, response_code);
            } else {
                endpoint_list_events(namespace, response_buffer, response_code);
            }
            return 0;
        }
        
        if (strcmp(method, "POST") == 0) {
            endpoint_create_event(namespace, body, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "DELETE") == 0 && strlen(name) > 0) {
            endpoint_delete_event(namespace, name, response_buffer, response_code);
            return 0;
        }
    }

    // Default 404
    const char* not_found = "{\"error\":\"not found\"}";
    strcpy(response_buffer, not_found);
    *response_code = 404;

    return 0;
}
