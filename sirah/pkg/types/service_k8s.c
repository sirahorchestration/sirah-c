#include "service_k8s.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* === Creation and Cleanup === */

k8s_service_k8s_t* k8s_service_create(const char *name, const char *namespace) {
  if (name == NULL || namespace == NULL) {
    return NULL;
  }
  
  k8s_service_k8s_t *service = calloc(1, sizeof(k8s_service_k8s_t));
  if (service == NULL) {
    return NULL;
  }
  
  /* Set metadata */
  service->metadata.name = strdup(name);
  service->metadata.namespace = strdup(namespace);
  service->metadata.resource_version = 1;
  service->metadata.generation = 1;
  
  /* Set UID (simplified) */
  service->metadata.uid = (int64_t)time(NULL) * 1000000;
  
  /* Set creation timestamp */
  time_t now = time(NULL);
  struct tm *timeinfo = gmtime(&now);
  service->metadata.creation_timestamp = malloc(32);
  strftime(service->metadata.creation_timestamp, 32, "%Y-%m-%dT%H:%M:%SZ", timeinfo);
  
  /* Initialize empty labels and annotations */
  service->metadata.labels = json_object_new_object();
  service->metadata.annotations = json_object_new_object();
  
  /* Default spec */
  service->spec.service_type = strdup("ClusterIP");
  service->spec.cluster_ip = strdup("10.0.0.1"); /* Will be assigned by IPAM */
  service->spec.selector = json_object_new_object();
  service->spec.ports = calloc(10, sizeof(k8s_service_port_t));
  service->spec.port_count = 0;
  service->spec.session_affinity_timeout = 10800;
  
  /* Default status */
  service->status.ready_replicas = 0;
  service->status.available_replicas = 0;
  
  return service;
}

void k8s_service_free(k8s_service_k8s_t *service) {
  if (service == NULL) {
    return;
  }
  
  free(service->metadata.name);
  free(service->metadata.namespace);
  free(service->metadata.creation_timestamp);
  if (service->metadata.deletion_timestamp != NULL) {
    free(service->metadata.deletion_timestamp);
  }
  
  if (service->metadata.labels != NULL) {
    json_object_put(service->metadata.labels);
  }
  if (service->metadata.annotations != NULL) {
    json_object_put(service->metadata.annotations);
  }
  
  free(service->spec.service_type);
  free(service->spec.cluster_ip);
  if (service->spec.external_name != NULL) {
    free(service->spec.external_name);
  }
  if (service->spec.selector != NULL) {
    json_object_put(service->spec.selector);
  }
  
  for (int i = 0; i < service->spec.port_count; i++) {
    free(service->spec.ports[i].protocol);
  }
  free(service->spec.ports);
  
  free(service);
}

/* === Serialization === */

json_object* k8s_service_to_json(const k8s_service_k8s_t *service) {
  if (service == NULL) {
    return NULL;
  }
  
  json_object *root = json_object_new_object();
  
  json_object_object_add(root, "apiVersion",
                         json_object_new_string("v1"));
  json_object_object_add(root, "kind",
                         json_object_new_string("Service"));
  
  /* Metadata */
  json_object *metadata = json_object_new_object();
  json_object_object_add(metadata, "name",
                         json_object_new_string(service->metadata.name));
  json_object_object_add(metadata, "namespace",
                         json_object_new_string(service->metadata.namespace));
  json_object_object_add(metadata, "uid",
                         json_object_new_int64(service->metadata.uid));
  json_object_object_add(metadata, "resourceVersion",
                         json_object_new_int64(service->metadata.resource_version));
  json_object_object_add(metadata, "generation",
                         json_object_new_int64(service->metadata.generation));
  json_object_object_add(metadata, "creationTimestamp",
                         json_object_new_string(service->metadata.creation_timestamp));
  
  if (service->metadata.deletion_timestamp != NULL) {
    json_object_object_add(metadata, "deletionTimestamp",
                           json_object_new_string(service->metadata.deletion_timestamp));
  }
  
  json_object_object_add(metadata, "labels",
                         json_object_get(service->metadata.labels));
  json_object_object_add(metadata, "annotations",
                         json_object_get(service->metadata.annotations));
  
  json_object_object_add(root, "metadata", metadata);
  
  /* Spec */
  json_object *spec = json_object_new_object();
  json_object_object_add(spec, "type",
                         json_object_new_string(service->spec.service_type));
  json_object_object_add(spec, "clusterIP",
                         json_object_new_string(service->spec.cluster_ip));
  json_object_object_add(spec, "selector",
                         json_object_get(service->spec.selector));
  
  /* Ports array */
  json_object *ports_array = json_object_new_array();
  for (int i = 0; i < service->spec.port_count; i++) {
    json_object *port = json_object_new_object();
    json_object_object_add(port, "port",
                           json_object_new_int(service->spec.ports[i].port));
    json_object_object_add(port, "protocol",
                           json_object_new_string(service->spec.ports[i].protocol));
    json_object_object_add(port, "targetPort",
                           json_object_new_int(service->spec.ports[i].target_port));
    if (service->spec.ports[i].node_port > 0) {
      json_object_object_add(port, "nodePort",
                             json_object_new_int(service->spec.ports[i].node_port));
    }
    json_object_array_add(ports_array, port);
  }
  json_object_object_add(spec, "ports", ports_array);
  
  json_object_object_add(spec, "sessionAffinityTimeout",
                         json_object_new_int(service->spec.session_affinity_timeout));
  
  if (service->spec.external_name != NULL) {
    json_object_object_add(spec, "externalName",
                           json_object_new_string(service->spec.external_name));
  }
  
  json_object_object_add(root, "spec", spec);
  
  /* Status */
  json_object *status = json_object_new_object();
  json_object_object_add(status, "readyReplicas",
                         json_object_new_int(service->status.ready_replicas));
  json_object_object_add(status, "availableReplicas",
                         json_object_new_int(service->status.available_replicas));
  
  json_object_object_add(root, "status", status);
  
  return root;
}

k8s_service_k8s_t* k8s_service_from_json(json_object *json) {
  if (json == NULL) {
    return NULL;
  }
  
  json_object *metadata_obj = json_object_object_get(json, "metadata");
  if (metadata_obj == NULL) {
    return NULL;
  }
  
  const char *name = json_object_get_string(json_object_object_get(metadata_obj, "name"));
  const char *namespace = json_object_get_string(json_object_object_get(metadata_obj, "namespace"));
  
  if (name == NULL || namespace == NULL) {
    return NULL;
  }
  
  k8s_service_k8s_t *service = k8s_service_create(name, namespace);
  
  /* Parse metadata fields */
  json_object *rv_obj = json_object_object_get(metadata_obj, "resourceVersion");
  if (rv_obj != NULL) {
    service->metadata.resource_version = json_object_get_int64(rv_obj);
  }
  
  json_object *gen_obj = json_object_object_get(metadata_obj, "generation");
  if (gen_obj != NULL) {
    service->metadata.generation = json_object_get_int64(gen_obj);
  }
  
  /* Parse spec */
  json_object *spec_obj = json_object_object_get(json, "spec");
  if (spec_obj != NULL) {
    const char *type_str = json_object_get_string(json_object_object_get(spec_obj, "type"));
    if (type_str != NULL) {
      free(service->spec.service_type);
      service->spec.service_type = strdup(type_str);
    }
    
    json_object *selector_obj = json_object_object_get(spec_obj, "selector");
    if (selector_obj != NULL) {
      json_object_put(service->spec.selector);
      service->spec.selector = json_object_get(selector_obj);
    }
  }
  
  return service;
}

/* === Port Management === */

int k8s_service_add_port(k8s_service_k8s_t *service, int port, const char *protocol,
                         int target_port) {
  if (service == NULL || protocol == NULL) {
    return -1;
  }
  
  if (service->spec.port_count >= 10) {
    return -1; /* Max ports reached */
  }
  
  k8s_service_port_t *port_spec = &service->spec.ports[service->spec.port_count++];
  port_spec->port = port;
  port_spec->protocol = strdup(protocol);
  port_spec->target_port = target_port;
  port_spec->node_port = 0;
  
  return 0;
}

int k8s_service_remove_port(k8s_service_k8s_t *service, int port) {
  if (service == NULL) {
    return -1;
  }
  
  for (int i = 0; i < service->spec.port_count; i++) {
    if (service->spec.ports[i].port == port) {
      free(service->spec.ports[i].protocol);
      
      /* Shift remaining ports */
      for (int j = i; j < service->spec.port_count - 1; j++) {
        service->spec.ports[j] = service->spec.ports[j + 1];
      }
      service->spec.port_count--;
      return 0;
    }
  }
  
  return -1; /* Port not found */
}

/* === Resource Versioning === */

int64_t k8s_service_get_resource_version(const k8s_service_k8s_t *service) {
  if (service == NULL) {
    return 0;
  }
  return service->metadata.resource_version;
}

void k8s_service_set_resource_version(k8s_service_k8s_t *service,
                                      int64_t resource_version) {
  if (service != NULL) {
    service->metadata.resource_version = resource_version;
  }
}

int64_t k8s_service_get_generation(const k8s_service_k8s_t *service) {
  if (service == NULL) {
    return 0;
  }
  return service->metadata.generation;
}

void k8s_service_increment_generation(k8s_service_k8s_t *service) {
  if (service != NULL) {
    service->metadata.generation++;
  }
}
