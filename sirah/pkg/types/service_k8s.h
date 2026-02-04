#ifndef SERVICE_K8S_H
#define SERVICE_K8S_H

#include <stdint.h>
#include <json-c/json.h>

/*
 * Kubernetes Service Type
 * 
 * Represents a Kubernetes Service with:
 * - Port mapping and service discovery
 * - Selector-based endpoint grouping
 * - Service type (ClusterIP, NodePort, LoadBalancer)
 * - Resource versioning for optimistic locking
 */

typedef struct {
  char *name;
  char *namespace;
  int64_t uid;
  int64_t resource_version;    /* For optimistic locking (CAS) */
  int64_t generation;           /* Updated on spec changes */
  char *creation_timestamp;
  char *deletion_timestamp;
  json_object *labels;
  json_object *annotations;
} k8s_svc_metadata_t;

typedef struct {
  int port;
  char *protocol;              /* TCP, UDP */
  int target_port;             /* Pod port */
  int node_port;               /* Only for NodePort type */
} k8s_service_port_t;

typedef struct {
  char *service_type;          /* ClusterIP, NodePort, LoadBalancer, ExternalName */
  char *cluster_ip;
  json_object *selector;       /* Labels to select backend pods */
  k8s_service_port_t *ports;
  int port_count;
  int session_affinity_timeout;
  char *external_name;         /* For ExternalName type */
} k8s_service_spec_t;

typedef struct {
  int ready_replicas;
  int available_replicas;
} k8s_service_status_t;

typedef struct {
  k8s_svc_metadata_t metadata;
  k8s_service_spec_t spec;
  k8s_service_status_t status;
} k8s_service_k8s_t;

/* === Creation and Cleanup === */

/**
 * Create a new service
 */
k8s_service_k8s_t* k8s_service_create(const char *name, const char *namespace);

/**
 * Free a service
 */
void k8s_service_free(k8s_service_k8s_t *service);

/* === Serialization === */

/**
 * Convert service to JSON
 * Includes resourceVersion and generation for CAS operations
 */
json_object* k8s_service_to_json(const k8s_service_k8s_t *service);

/**
 * Parse service from JSON
 * Extracts resourceVersion and generation
 */
k8s_service_k8s_t* k8s_service_from_json(json_object *json);

/* === Port Management === */

/**
 * Add a port to the service
 */
int k8s_service_add_port(k8s_service_k8s_t *service, int port, const char *protocol,
                         int target_port);

/**
 * Remove a port from the service
 */
int k8s_service_remove_port(k8s_service_k8s_t *service, int port);

/* === Resource Versioning === */

/**
 * Get resource version (for etcd CAS)
 */
int64_t k8s_service_get_resource_version(const k8s_service_k8s_t *service);

/**
 * Set resource version (updated by etcd after storage)
 */
void k8s_service_set_resource_version(k8s_service_k8s_t *service,
                                      int64_t resource_version);

/**
 * Get generation (for tracking spec changes)
 */
int64_t k8s_service_get_generation(const k8s_service_k8s_t *service);

/**
 * Increment generation (call when spec is modified)
 */
void k8s_service_increment_generation(k8s_service_k8s_t *service);

#endif /* SERVICE_K8S_H */
