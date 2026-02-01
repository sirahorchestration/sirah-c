#ifndef SIRAH_APISERVER_ENDPOINTS_H
#define SIRAH_APISERVER_ENDPOINTS_H

#include "../storage/store.h"
#include "pod_logs.h"
#include "pod_exec.h"

int endpoint_list_pods(const char* namespace, char* response_buffer, int* response_code);
int endpoint_get_pod(const char* namespace, const char* name,
                     char* response_buffer, int* response_code);
int endpoint_create_pod(const char* namespace, const char* body,
                        char* response_buffer, int* response_code);
int endpoint_delete_pod(const char* namespace, const char* name,
                        char* response_buffer, int* response_code);
int endpoint_bind_pod(const char* namespace, const char* pod_name, const char* body,
                     char* response_buffer, int* response_code);int endpoint_get_pod_logs(const char* namespace, const char* pod_name,
                         const char* container_name, log_query_params_t* params,
                         char* response_buffer, int* response_code);
int endpoint_exec_pod(const char* namespace, const char* pod_name,
                     const char* container_name, const char* command,
                     exec_response_t* response);int endpoint_list_nodes(char* response_buffer, int* response_code);
int endpoint_get_node(const char* node_name, char* response_buffer, int* response_code);
int endpoint_register_node(const char* body, char* response_buffer, int* response_code);
int endpoint_node_heartbeat(const char* node_name, const char* body,
                           char* response_buffer, int* response_code);
int endpoint_pod_status(const char* namespace, const char* pod_name, const char* body,
                       char* response_buffer, int* response_code);
int endpoint_pod_events(const char* namespace, const char* pod_name,
                       char* response_buffer, int* response_code);
int endpoint_list_services(const char* namespace, char* response_buffer, int* response_code);
int endpoint_get_service(const char* namespace, const char* name,
                        char* response_buffer, int* response_code);
int endpoint_create_service(const char* namespace, const char* body,
                           char* response_buffer, int* response_code);
int endpoint_delete_service(const char* namespace, const char* name,
                           char* response_buffer, int* response_code);

// ConfigMap endpoints
int endpoint_list_configmaps(const char* namespace, char* response_buffer, int* response_code);
int endpoint_get_configmap(const char* namespace, const char* name,
                          char* response_buffer, int* response_code);
int endpoint_create_configmap(const char* namespace, const char* body,
                             char* response_buffer, int* response_code);
int endpoint_delete_configmap(const char* namespace, const char* name,
                             char* response_buffer, int* response_code);

// Secret endpoints
int endpoint_list_secrets(const char* namespace, char* response_buffer, int* response_code);
int endpoint_get_secret(const char* namespace, const char* name,
                       char* response_buffer, int* response_code);
int endpoint_create_secret(const char* namespace, const char* body,
                          char* response_buffer, int* response_code);
int endpoint_delete_secret(const char* namespace, const char* name,
                          char* response_buffer, int* response_code);

// PersistentVolume endpoints
int endpoint_list_pv(char* response_buffer, int* response_code);
int endpoint_get_pv(const char* name, char* response_buffer, int* response_code);
int endpoint_create_pv(const char* body, char* response_buffer, int* response_code);
int endpoint_delete_pv(const char* name, char* response_buffer, int* response_code);

// PersistentVolumeClaim endpoints
int endpoint_list_pvc(const char* namespace, char* response_buffer, int* response_code);
int endpoint_get_pvc(const char* namespace, const char* name,
                    char* response_buffer, int* response_code);
int endpoint_create_pvc(const char* namespace, const char* body,
                       char* response_buffer, int* response_code);
int endpoint_delete_pvc(const char* namespace, const char* name,
                       char* response_buffer, int* response_code);

// StatefulSet endpoints
int endpoint_list_statefulsets(const char* namespace, char* response_buffer, int* response_code);
int endpoint_get_statefulset(const char* namespace, const char* name,
                            char* response_buffer, int* response_code);
int endpoint_create_statefulset(const char* namespace, const char* body,
                               char* response_buffer, int* response_code);
int endpoint_delete_statefulset(const char* namespace, const char* name,
                               char* response_buffer, int* response_code);

// Deployment endpoints
int endpoint_list_deployments(const char* namespace, char* response_buffer, int* response_code);
int endpoint_get_deployment(const char* namespace, const char* name,
                           char* response_buffer, int* response_code);
int endpoint_create_deployment(const char* namespace, const char* body,
                              char* response_buffer, int* response_code);
int endpoint_update_deployment(const char* namespace, const char* name, const char* body,
                              char* response_buffer, int* response_code);
int endpoint_patch_deployment(const char* namespace, const char* name, const char* body,
                             const char* content_type, char* response_buffer, int* response_code);
int endpoint_delete_deployment(const char* namespace, const char* name,
                              char* response_buffer, int* response_code);

// DaemonSet endpoints
int endpoint_list_daemonsets(const char* namespace, char* response_buffer, int* response_code);
int endpoint_get_daemonset(const char* namespace, const char* name,
                          char* response_buffer, int* response_code);
int endpoint_create_daemonset(const char* namespace, const char* body,
                             char* response_buffer, int* response_code);
int endpoint_update_daemonset(const char* namespace, const char* name, const char* body,
                             char* response_buffer, int* response_code);
int endpoint_patch_daemonset(const char* namespace, const char* name, const char* body,
                            const char* content_type, char* response_buffer, int* response_code);
int endpoint_delete_daemonset(const char* namespace, const char* name,
                             char* response_buffer, int* response_code);

// Job endpoints
int endpoint_list_jobs(const char* namespace, char* response_buffer, int* response_code);
int endpoint_get_job(const char* namespace, const char* name,
                    char* response_buffer, int* response_code);
int endpoint_create_job(const char* namespace, const char* body,
                       char* response_buffer, int* response_code);
int endpoint_update_job(const char* namespace, const char* name, const char* body,
                       char* response_buffer, int* response_code);
int endpoint_patch_job(const char* namespace, const char* name, const char* body,
                      const char* content_type, char* response_buffer, int* response_code);
int endpoint_delete_job(const char* namespace, const char* name,
                       char* response_buffer, int* response_code);

// CronJob endpoints
int endpoint_list_cronjobs(const char* namespace, char* response_buffer, int* response_code);
int endpoint_get_cronjob(const char* namespace, const char* name,
                        char* response_buffer, int* response_code);
int endpoint_create_cronjob(const char* namespace, const char* body,
                           char* response_buffer, int* response_code);
int endpoint_update_cronjob(const char* namespace, const char* name, const char* body,
                           char* response_buffer, int* response_code);
int endpoint_patch_cronjob(const char* namespace, const char* name, const char* body,
                          const char* content_type, char* response_buffer, int* response_code);
int endpoint_delete_cronjob(const char* namespace, const char* name,
                           char* response_buffer, int* response_code);

// Patch endpoints for existing resources
int endpoint_patch_pod(const char* namespace, const char* name, const char* body,
                      const char* content_type, char* response_buffer, int* response_code);
int endpoint_patch_service(const char* namespace, const char* name, const char* body,
                          const char* content_type, char* response_buffer, int* response_code);
int endpoint_patch_configmap(const char* namespace, const char* name, const char* body,
                            const char* content_type, char* response_buffer, int* response_code);
int endpoint_patch_secret(const char* namespace, const char* name, const char* body,
                         const char* content_type, char* response_buffer, int* response_code);

// Watch endpoints
int endpoint_watch_pods(const char* namespace, const char* query_string,
                       char* response_buffer, int* response_code);
int endpoint_watch_services(const char* namespace, const char* query_string,
                           char* response_buffer, int* response_code);
int endpoint_watch_deployments(const char* namespace, const char* query_string,
                              char* response_buffer, int* response_code);

// Namespace endpoints
int endpoint_list_namespaces(char* response_buffer, int* response_code);
int endpoint_get_namespace(const char* name, char* response_buffer, int* response_code);
int endpoint_create_namespace(const char* body, char* response_buffer, int* response_code);
int endpoint_delete_namespace(const char* name, char* response_buffer, int* response_code);

// Event endpoints
int endpoint_list_events(const char* namespace, char* response_buffer, int* response_code);
int endpoint_get_event(const char* namespace, const char* name, char* response_buffer, int* response_code);
int endpoint_create_event(const char* namespace, const char* body,
                         char* response_buffer, int* response_code);
int endpoint_delete_event(const char* namespace, const char* name, char* response_buffer, int* response_code);

#endif
