#include "service.h"
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>

k8s_service_t* k8s_service_new(const char* name, const char* namespace) {
    if (!name || !namespace) return NULL;
    
    k8s_service_t* svc = (k8s_service_t*)malloc(sizeof(k8s_service_t));
    
    svc->metadata = (k8s_metadata_t){
        .name = strdup(name),
        .namespace = strdup(namespace),
        .uid = strdup(""),
        .resource_version = strdup("1"),
        .creation_timestamp = time(NULL),
        .deletion_timestamp = 0,
        .owner_references = NULL,
        .num_owners = 0,
        .finalizers = NULL,
        .num_finalizers = 0
    };
    
    svc->spec = (k8s_service_spec_t){
        .type = SERVICE_CLUSTER_IP,
        .cluster_ip = strdup(""),
        .external_ip = NULL,
        .ports = (k8s_service_port_t*)malloc(sizeof(k8s_service_port_t) * 10),
        .num_ports = 0,
        .selectors = (char**)malloc(sizeof(char*) * 20),
        .num_selectors = 0,
        .session_affinity = strdup("None"),
        .session_timeout = 10800
    };
    
    svc->status = (k8s_service_status_t){
        .endpoints = (k8s_endpoint_t*)malloc(sizeof(k8s_endpoint_t) * 100),
        .num_endpoints = 0,
        .conditions = NULL,
        .num_conditions = 0
    };
    
    return svc;
}

void k8s_service_free(k8s_service_t* svc) {
    if (!svc) return;
    
    free(svc->metadata.name);
    free(svc->metadata.namespace);
    free(svc->metadata.uid);
    free(svc->metadata.resource_version);
    
    free(svc->spec.cluster_ip);
    if (svc->spec.external_ip) free(svc->spec.external_ip);
    free(svc->spec.ports);
    free(svc->spec.selectors);
    free(svc->spec.session_affinity);
    
    for (int i = 0; i < svc->status.num_endpoints; i++) {
        free(svc->status.endpoints[i].pod_ip);
        free(svc->status.endpoints[i].pod_name);
        free(svc->status.endpoints[i].node_name);
        if (svc->status.endpoints[i].hostname) free(svc->status.endpoints[i].hostname);
    }
    free(svc->status.endpoints);
    free(svc->status.conditions);
    
    free(svc);
}

char* k8s_service_to_json(k8s_service_t* svc) {
    if (!svc) return strdup("{}");
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("Service"));
    
    // Metadata
    json_object* meta = json_object_new_object();
    json_object_object_add(meta, "name", json_object_new_string(svc->metadata.name));
    json_object_object_add(meta, "namespace", json_object_new_string(svc->metadata.namespace));
    json_object_object_add(root, "metadata", meta);
    
    // Spec
    json_object* spec = json_object_new_object();
    const char* type_str = "ClusterIP";
    if (svc->spec.type == SERVICE_NODE_PORT) type_str = "NodePort";
    else if (svc->spec.type == SERVICE_LOAD_BALANCER) type_str = "LoadBalancer";
    else if (svc->spec.type == SERVICE_EXTERNAL_NAME) type_str = "ExternalName";
    
    json_object_object_add(spec, "type", json_object_new_string(type_str));
    json_object_object_add(spec, "clusterIP", json_object_new_string(svc->spec.cluster_ip));
    
    // Ports
    json_object* ports = json_object_new_array();
    for (int i = 0; i < svc->spec.num_ports; i++) {
        json_object* port = json_object_new_object();
        json_object_object_add(port, "port", json_object_new_int(svc->spec.ports[i].port));
        json_object_object_add(port, "targetPort", json_object_new_int(svc->spec.ports[i].target_port));
        json_object_array_add(ports, port);
    }
    json_object_object_add(spec, "ports", ports);
    
    // Selectors
    json_object* selectors = json_object_new_object();
    for (int i = 0; i < svc->spec.num_selectors; i += 2) {
        if (i + 1 < svc->spec.num_selectors) {
            json_object_object_add(selectors, svc->spec.selectors[i],
                                 json_object_new_string(svc->spec.selectors[i + 1]));
        }
    }
    json_object_object_add(spec, "selector", selectors);
    json_object_object_add(root, "spec", spec);
    
    // Status
    json_object* status = json_object_new_object();
    json_object* endpoints = json_object_new_array();
    for (int i = 0; i < svc->status.num_endpoints; i++) {
        json_object* ep = json_object_new_object();
        json_object_object_add(ep, "ip", json_object_new_string(svc->status.endpoints[i].pod_ip));
        json_object_object_add(ep, "podName", json_object_new_string(svc->status.endpoints[i].pod_name));
        json_object_array_add(endpoints, ep);
    }
    json_object_object_add(status, "endpoints", endpoints);
    json_object_object_add(root, "status", status);
    
    const char* json_str = json_object_to_json_string(root);
    char* result = strdup(json_str);
    json_object_put(root);
    
    return result;
}

k8s_service_t* k8s_service_from_json(const char* json_str) {
    if (!json_str) return NULL;
    
    json_object* root = json_tokener_parse(json_str);
    if (!root) return NULL;
    
    json_object* meta = json_object_object_get(root, "metadata");
    const char* name = json_object_get_string(json_object_object_get(meta, "name"));
    const char* ns = json_object_get_string(json_object_object_get(meta, "namespace"));
    
    k8s_service_t* svc = k8s_service_new(name, ns);
    
    json_object_put(root);
    return svc;
}

int k8s_service_add_port(k8s_service_t* svc, int port, int target_port) {
    if (!svc || svc->spec.num_ports >= 10) return -1;
    
    svc->spec.ports[svc->spec.num_ports].port = port;
    svc->spec.ports[svc->spec.num_ports].target_port = target_port;
    svc->spec.ports[svc->spec.num_ports].protocol = strdup("TCP");
    svc->spec.num_ports++;
    
    return 0;
}

int k8s_service_add_selector(k8s_service_t* svc, const char* key, const char* value) {
    if (!svc || !key || !value) return -1;
    if (svc->spec.num_selectors + 2 >= 20) return -1;
    
    svc->spec.selectors[svc->spec.num_selectors++] = strdup(key);
    svc->spec.selectors[svc->spec.num_selectors++] = strdup(value);
    
    return 0;
}

int k8s_service_add_endpoint(k8s_service_t* svc, const char* pod_ip, const char* pod_name) {
    if (!svc || !pod_ip || !pod_name) return -1;
    if (svc->status.num_endpoints >= 100) return -1;
    
    svc->status.endpoints[svc->status.num_endpoints].pod_ip = strdup(pod_ip);
    svc->status.endpoints[svc->status.num_endpoints].pod_name = strdup(pod_name);
    svc->status.endpoints[svc->status.num_endpoints].ready = 1;
    svc->status.num_endpoints++;
    
    return 0;
}
