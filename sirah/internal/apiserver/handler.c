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

// Health check endpoint
static int handle_healthz(char* response_buffer, int* response_code) {
    const char* response = "{\"status\":\"ok\"}";
    strcpy(response_buffer, response);
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
            endpoint_bind_pod(namespace, pod_name, body, response_buffer, response_code);
            return 0;
        }

        // Check for log endpoint
        if (strcmp(method, "GET") == 0 && strstr(path, "/log")) {
            // GET /pods/{name}/log
            log_query_params_t params = {0};
            parse_log_params(query_string, &params);
            endpoint_get_pod_logs(namespace, pod_name, "", &params, response_buffer, response_code);
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
            
            // Find the pod
            int found = 0;
            for (int i = 0; i < pod_store.count; i++) {
                if (strcmp(pod_store.pods[i]->metadata.name, pod_name) == 0 &&
                    strcmp(pod_store.pods[i]->metadata.namespace, namespace) == 0) {
                    found = 1;
                    
                    // Extract status object from patch
                    json_object* status_obj = NULL;
                    if (json_object_object_get_ex(patch_obj, "status", &status_obj)) {
                        // Update phase if provided
                        json_object* phase_obj = NULL;
                        if (json_object_object_get_ex(status_obj, "phase", &phase_obj)) {
                            const char* new_phase = json_object_get_string(phase_obj);
                            if (new_phase) {
                                // Convert phase string to enum
                                k8s_phase_t phase_enum = PHASE_PENDING;
                                if (strcmp(new_phase, "Running") == 0) {
                                    phase_enum = PHASE_RUNNING;
                                } else if (strcmp(new_phase, "Succeeded") == 0) {
                                    phase_enum = PHASE_SUCCEEDED;
                                } else if (strcmp(new_phase, "Failed") == 0) {
                                    phase_enum = PHASE_FAILED;
                                }
                                
                                // Update pod phase
                                pod_store.pods[i]->status.phase = phase_enum;
                                
                                // Also update container state to match pod phase
                                for (int c = 0; c < pod_store.pods[i]->status.num_container_statuses; c++) {
                                    pod_store.pods[i]->status.container_statuses[c].state = phase_enum;
                                    
                                    // Update reason based on phase transition
                                    if (phase_enum == PHASE_RUNNING) {
                                        if (pod_store.pods[i]->status.container_statuses[c].reason) {
                                            free(pod_store.pods[i]->status.container_statuses[c].reason);
                                        }
                                        pod_store.pods[i]->status.container_statuses[c].reason = NULL;
                                    }
                                }
                                
                                fprintf(stderr, "[API] Updated pod %s/%s status to %s (containers updated too)\n", 
                                    namespace, pod_name, new_phase);
                                fflush(stderr);
                            }
                        }
                    }
                    
                    // Build response with updated pod (same format as GET)
                    json_object* response_pod = json_object_new_object();
                    json_object_object_add(response_pod, "apiVersion", json_object_new_string("v1"));
                    json_object_object_add(response_pod, "kind", json_object_new_string("Pod"));
                    
                    // Metadata
                    json_object* metadata = json_object_new_object();
                    json_object_object_add(metadata, "name", json_object_new_string(pod_store.pods[i]->metadata.name));
                    json_object_object_add(metadata, "namespace", json_object_new_string(pod_store.pods[i]->metadata.namespace));
                    json_object_object_add(response_pod, "metadata", metadata);
                    
                    // Status
                    json_object* response_status = json_object_new_object();
                    
                    // Convert phase enum to string
                    const char* phase_str = "Unknown";
                    if (pod_store.pods[i]->status.phase == PHASE_PENDING) {
                        phase_str = "Pending";
                    } else if (pod_store.pods[i]->status.phase == PHASE_RUNNING) {
                        phase_str = "Running";
                    } else if (pod_store.pods[i]->status.phase == PHASE_SUCCEEDED) {
                        phase_str = "Succeeded";
                    } else if (pod_store.pods[i]->status.phase == PHASE_FAILED) {
                        phase_str = "Failed";
                    }
                    
                    json_object_object_add(response_status, "phase", 
                        json_object_new_string(phase_str));
                    
                    json_object* container_statuses = json_object_new_array();
                    for (int c = 0; c < pod_store.pods[i]->status.num_container_statuses; c++) {
                        json_object* cs = json_object_new_object();
                        json_object_object_add(cs, "name",
                            json_object_new_string(pod_store.pods[i]->spec.containers[c].name));
                        json_object_object_add(cs, "ready", json_object_new_boolean(
                            pod_store.pods[i]->status.container_statuses[c].state == PHASE_RUNNING ? 1 : 0));
                        json_object_object_add(cs, "restartCount", json_object_new_int(0));
                        
                        json_object* state = json_object_new_object();
                        if (pod_store.pods[i]->status.container_statuses[c].state == PHASE_RUNNING) {
                            json_object_object_add(state, "running", json_object_new_object());
                        } else {
                            json_object* waiting = json_object_new_object();
                            const char* reason = pod_store.pods[i]->status.container_statuses[c].reason ? 
                                                pod_store.pods[i]->status.container_statuses[c].reason : 
                                                "ContainerCreating";
                            json_object_object_add(waiting, "reason", json_object_new_string(reason));
                            json_object_object_add(state, "waiting", waiting);
                        }
                        json_object_object_add(cs, "state", state);
                        json_object_array_add(container_statuses, cs);
                    }
                    json_object_object_add(response_status, "containerStatuses", container_statuses);
                    json_object_object_add(response_pod, "status", response_status);
                    
                    const char* json_str = json_object_to_json_string(response_pod);
                    strcpy(response_buffer, json_str);
                    *response_code = 200;
                    json_object_put(response_pod);
                    json_object_put(patch_obj);
                    return 0;
                }
            }
            
            if (!found) {
                *response_code = 404;
                strcpy(response_buffer, "{\"error\":\"pod not found\"}");
            }
            json_object_put(patch_obj);
            return -1;
        }

        // Watch endpoint
        if (strcmp(method, "GET") == 0 && strstr(query_string, "watch=true")) {
            endpoint_watch_pods(namespace, query_string, response_buffer, response_code);
            return 0;
        }

        if (strcmp(method, "GET") == 0) {
            if (strlen(pod_name) > 0) {
                // GET /pods/{name}
                endpoint_get_pod(namespace, pod_name, response_buffer, response_code);
            } else {
                // GET /pods (list) - with filtering support
                endpoint_list_pods(namespace, response_buffer, response_code);
            }
            return 0;
        }

        if (strcmp(method, "POST") == 0) {
            // POST /pods (create)
            endpoint_create_pod(namespace, body, response_buffer, response_code);
            return 0;
        }

        if (strcmp(method, "PATCH") == 0 && strlen(pod_name) > 0) {
            // PATCH /pods/{name}
            const char* content_type = "";  // Would be extracted from headers in real implementation
            endpoint_patch_pod(namespace, pod_name, body, content_type, response_buffer, response_code);
            return 0;
        }

        if (strcmp(method, "DELETE") == 0 && strlen(pod_name) > 0) {
            // DELETE /pods/{name}
            endpoint_delete_pod(namespace, pod_name, response_buffer, response_code);
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
                endpoint_get_service(namespace, name, response_buffer, response_code);
            } else {
                endpoint_list_services(namespace, response_buffer, response_code);
            }
            return 0;
        }
        
        if (strcmp(method, "POST") == 0) {
            endpoint_create_service(namespace, body, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "PATCH") == 0 && strlen(name) > 0) {
            const char* content_type = "";
            endpoint_patch_service(namespace, name, body, content_type, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "DELETE") == 0 && strlen(name) > 0) {
            endpoint_delete_service(namespace, name, response_buffer, response_code);
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
                endpoint_get_deployment(namespace, name, response_buffer, response_code);
            } else {
                endpoint_list_deployments(namespace, response_buffer, response_code);
            }
            return 0;
        }
        
        if (strcmp(method, "POST") == 0) {
            endpoint_create_deployment(namespace, body, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "PUT") == 0 && strlen(name) > 0) {
            endpoint_update_deployment(namespace, name, body, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "PATCH") == 0 && strlen(name) > 0) {
            const char* content_type = "";
            endpoint_patch_deployment(namespace, name, body, content_type, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "DELETE") == 0 && strlen(name) > 0) {
            endpoint_delete_deployment(namespace, name, response_buffer, response_code);
            return 0;
        }
    }

    // Namespace endpoints
    if (strstr(path, "/api/v1") && strstr(path, "/namespaces")) {
        char name[256] = {0};
        
        const char* ns_part = strstr(path, "/namespaces/");
        if (ns_part && strlen(ns_part) > strlen("/namespaces/")) {
            const char* name_start = ns_part + strlen("/namespaces/");
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
                endpoint_get_configmap(namespace, name, response_buffer, response_code);
            } else {
                endpoint_list_configmaps(namespace, response_buffer, response_code);
            }
            return 0;
        }
        
        if (strcmp(method, "POST") == 0) {
            endpoint_create_configmap(namespace, body, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "PATCH") == 0 && strlen(name) > 0) {
            const char* content_type = "";
            endpoint_patch_configmap(namespace, name, body, content_type, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "DELETE") == 0 && strlen(name) > 0) {
            endpoint_delete_configmap(namespace, name, response_buffer, response_code);
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
                endpoint_get_secret(namespace, name, response_buffer, response_code);
            } else {
                endpoint_list_secrets(namespace, response_buffer, response_code);
            }
            return 0;
        }
        
        if (strcmp(method, "POST") == 0) {
            endpoint_create_secret(namespace, body, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "PATCH") == 0 && strlen(name) > 0) {
            const char* content_type = "";
            endpoint_patch_secret(namespace, name, body, content_type, response_buffer, response_code);
            return 0;
        }
        
        if (strcmp(method, "DELETE") == 0 && strlen(name) > 0) {
            endpoint_delete_secret(namespace, name, response_buffer, response_code);
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
