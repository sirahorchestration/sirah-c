// pkg/types/pod.c
#include "pod.h"
#include <stdlib.h>
#include <string.h>

k8s_pod_t* k8s_pod_new(const char* name, const char* namespace) {
    k8s_pod_t* pod = malloc(sizeof(*pod));
    if (!pod) return NULL;
    
    pod->metadata = *k8s_metadata_new(name, namespace);
    pod->spec.containers = NULL;
    pod->spec.num_containers = 0;
    
    // Initialize volumes
    pod->spec.volumes = (k8s_volume_t*)malloc(sizeof(k8s_volume_t) * 32);
    pod->spec.num_volumes = 0;
    
    pod->spec.node_name = NULL;
    pod->spec.service_account = strdup("default");
    pod->spec.restart_policy = strdup("Always");
    pod->spec.termination_grace_period_seconds = 30;
    pod->spec.dns_policy = strdup("ClusterFirst");
    
    // Initialize affinity and tolerations
    pod->spec.affinity = NULL;
    pod->spec.tolerations = NULL;
    pod->spec.num_tolerations = 0;
    
    pod->status.phase = PHASE_PENDING;
    pod->status.host_ip = NULL;
    pod->status.pod_ip = NULL;
    pod->status.conditions = NULL;
    pod->status.num_conditions = 0;
    pod->status.num_container_statuses = 0;
    pod->status.start_time = 0;
    
    return pod;
}

void k8s_pod_free(k8s_pod_t* pod) {
    if (!pod) return;
    k8s_metadata_free(&pod->metadata);
    free(pod->spec.node_name);
    free(pod->spec.service_account);
    free(pod->spec.restart_policy);
    free(pod->spec.dns_policy);
    
    // Free affinity and tolerations (if they were included)
    // Note: These will be freed by affinity cleanup functions when implemented
    // For now, we just NULL them to avoid double-free
    pod->spec.affinity = NULL;
    pod->spec.tolerations = NULL;
    
    free(pod);
}

char* k8s_pod_to_json(k8s_pod_t* pod) {
    if (!pod) return strdup("{}");
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("Pod"));
    
    // Metadata
    json_object* meta = json_object_new_object();
    json_object_object_add(meta, "name", json_object_new_string(pod->metadata.name));
    json_object_object_add(meta, "namespace", json_object_new_string(pod->metadata.namespace));
    json_object_object_add(meta, "uid", json_object_new_string(pod->metadata.uid ? pod->metadata.uid : ""));
    json_object_object_add(meta, "resourceVersion", json_object_new_string(pod->metadata.resource_version ? pod->metadata.resource_version : ""));
    json_object_object_add(root, "metadata", meta);
    
    // Spec
    json_object* spec = json_object_new_object();
    
    // Add containers array to spec
    json_object* containers = json_object_new_array();
    for (int i = 0; i < pod->spec.num_containers; i++) {
        json_object* container = json_object_new_object();
        json_object_object_add(container, "name", json_object_new_string(pod->spec.containers[i].name));
        json_object_object_add(container, "image", json_object_new_string(pod->spec.containers[i].image));
        if (pod->spec.containers[i].image_pull_policy) {
            json_object_object_add(container, "imagePullPolicy", json_object_new_string(pod->spec.containers[i].image_pull_policy));
        }
        json_object_array_add(containers, container);
    }
    json_object_object_add(spec, "containers", containers);
    
    json_object_object_add(spec, "nodeName", json_object_new_string(pod->spec.node_name ? pod->spec.node_name : ""));
    json_object_object_add(spec, "restartPolicy", json_object_new_string(pod->spec.restart_policy));
    json_object_object_add(root, "spec", spec);
    
    // Status with pod phase
    json_object* status = json_object_new_object();
    const char* phase_str = "Pending";
    if (pod->status.phase == PHASE_RUNNING) phase_str = "Running";
    else if (pod->status.phase == PHASE_SUCCEEDED) phase_str = "Succeeded";
    else if (pod->status.phase == PHASE_FAILED) phase_str = "Failed";
    else if (pod->status.phase == PHASE_TERMINATING) phase_str = "Terminating";
    
    json_object_object_add(status, "phase", json_object_new_string(phase_str));
    json_object_object_add(status, "hostIP", json_object_new_string(pod->status.host_ip ? pod->status.host_ip : ""));
    json_object_object_add(status, "podIP", json_object_new_string(pod->status.pod_ip ? pod->status.pod_ip : ""));
    
    // Add containerStatuses to status
    json_object* container_statuses = json_object_new_array();
    for (int i = 0; i < pod->status.num_container_statuses; i++) {
        json_object* cs = json_object_new_object();
        json_object_object_add(cs, "name", json_object_new_string(pod->spec.containers[i].name));
        json_object_object_add(cs, "ready", json_object_new_boolean(pod->status.container_statuses[i].state == PHASE_RUNNING));
        json_object_object_add(cs, "restartCount", json_object_new_int(0));
        
        // Properly structured state object
        json_object* state = json_object_new_object();
        if (pod->status.container_statuses[i].state == PHASE_RUNNING) {
            // Running state
            json_object_object_add(state, "running", json_object_new_object());
        } else {
            // Waiting state with reason
            json_object* waiting_obj = json_object_new_object();
            json_object_object_add(waiting_obj, "reason", json_object_new_string("ContainerCreating"));
            json_object_object_add(state, "waiting", waiting_obj);
        }
        json_object_object_add(cs, "state", state);
        
        json_object_array_add(container_statuses, cs);
    }
    json_object_object_add(status, "containerStatuses", container_statuses);
    
    json_object_object_add(root, "status", status);
    
    char* result = strdup(json_object_to_json_string(root));
    json_object_put(root);
    return result;
}

k8s_pod_t* k8s_pod_from_json(const char* json_str) {
    if (!json_str) return NULL;
    
    json_object* root = json_tokener_parse(json_str);
    if (!root) return NULL;
    
    json_object* meta = json_object_object_get(root, "metadata");
    if (!meta) {
        json_object_put(root);
        return NULL;
    }
    
    const char* name = json_object_get_string(json_object_object_get(meta, "name"));
    const char* ns = json_object_get_string(json_object_object_get(meta, "namespace"));
    
    k8s_pod_t* pod = k8s_pod_new(name ? name : "unnamed", ns ? ns : "default");
    
    // Parse metadata fields
    const char* uid = json_object_get_string(json_object_object_get(meta, "uid"));
    if (uid) {
        free(pod->metadata.uid);
        pod->metadata.uid = strdup(uid);
    }
    
    const char* resource_version = json_object_get_string(json_object_object_get(meta, "resourceVersion"));
    if (resource_version) {
        free(pod->metadata.resource_version);
        pod->metadata.resource_version = strdup(resource_version);
    }
    
    json_object* spec = json_object_object_get(root, "spec");
    if (spec) {
        // Parse containers array
        json_object* containers_obj = json_object_object_get(spec, "containers");
        if (containers_obj && json_object_is_type(containers_obj, json_type_array)) {
            int num_containers = json_object_array_length(containers_obj);
            if (num_containers > 0) {
                // Allocate container array
                pod->spec.containers = (k8s_container_t*)malloc(sizeof(k8s_container_t) * num_containers);
                pod->spec.num_containers = 0;
                
                for (int i = 0; i < num_containers; i++) {
                    json_object* container_obj = json_object_array_get_idx(containers_obj, i);
                    if (container_obj) {
                        k8s_container_t* container = &pod->spec.containers[pod->spec.num_containers++];
                        
                        const char* cont_name = json_object_get_string(json_object_object_get(container_obj, "name"));
                        container->name = strdup(cont_name ? cont_name : "container");
                        
                        const char* image = json_object_get_string(json_object_object_get(container_obj, "image"));
                        container->image = strdup(image ? image : "");
                        
                        const char* pull_policy = json_object_get_string(json_object_object_get(container_obj, "imagePullPolicy"));
                        container->image_pull_policy = pull_policy ? strdup(pull_policy) : NULL;
                        
                        container->num_env_vars = 0;
                        container->env_names = NULL;
                        container->env_values = NULL;
                        container->num_command_args = 0;
                        container->command = NULL;
                        container->num_args = 0;
                        container->args = NULL;
                        container->num_ports = 0;
                        container->ports = NULL;
                        container->cpu_millicores = 0;
                        container->memory_bytes = 0;
                        container->storage_bytes = 0;
                        container->working_dir = NULL;
                        container->stdin_policy = NULL;
                        container->tty_policy = NULL;
                    }
                }
            }
        }
        
        const char* node_name = json_object_get_string(json_object_object_get(spec, "nodeName"));
        if (node_name) pod->spec.node_name = strdup(node_name);
        
        const char* restart_policy = json_object_get_string(json_object_object_get(spec, "restartPolicy"));
        if (restart_policy) {
            free(pod->spec.restart_policy);
            pod->spec.restart_policy = strdup(restart_policy);
        }
    }
    
    json_object_put(root);
    return pod;
}

int k8s_pod_add_container(k8s_pod_t* pod, k8s_container_t* container) {
    // TODO: Implement container addition
    return 0;
}

int k8s_pod_set_image(k8s_pod_t* pod, const char* image) {
    if (pod->spec.num_containers > 0) {
        pod->spec.containers[0].image = strdup(image);
        return 0;
    }
    return -1;
}

int k8s_pod_set_node(k8s_pod_t* pod, const char* node_name) {
    pod->spec.node_name = strdup(node_name);
    return 0;
}

// Volume operations

int k8s_pod_add_volume(k8s_pod_t* pod, const char* name, const char* type) {
    if (!pod || !name || !type || pod->spec.num_volumes >= 32) return -1;
    
    k8s_volume_t* vol = &pod->spec.volumes[pod->spec.num_volumes++];
    vol->name = strdup(name);
    vol->type = strdup(type);
    vol->path = NULL;
    vol->pvc_name = NULL;
    vol->storage_class = NULL;
    
    return 0;
}

int k8s_pod_add_pvc_volume(k8s_pod_t* pod, const char* name, const char* pvc_name) {
    if (!pod || !name || !pvc_name) return -1;
    
    if (k8s_pod_add_volume(pod, name, "persistentVolumeClaim") != 0) return -1;
    
    k8s_volume_t* vol = &pod->spec.volumes[pod->spec.num_volumes - 1];
    vol->pvc_name = strdup(pvc_name);
    
    return 0;
}

int k8s_pod_add_hostpath_volume(k8s_pod_t* pod, const char* name, const char* path) {
    if (!pod || !name || !path) return -1;
    
    if (k8s_pod_add_volume(pod, name, "hostPath") != 0) return -1;
    
    k8s_volume_t* vol = &pod->spec.volumes[pod->spec.num_volumes - 1];
    vol->path = strdup(path);
    
    return 0;
}

int k8s_pod_add_emptydir_volume(k8s_pod_t* pod, const char* name) {
    if (!pod || !name) return -1;
    
    return k8s_pod_add_volume(pod, name, "emptyDir");
}
