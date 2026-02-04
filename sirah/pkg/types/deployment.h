#ifndef DEPLOYMENT_H
#define DEPLOYMENT_H

#include <stdint.h>
#include <json-c/json.h>

/*
 * Kubernetes Deployment Type
 * 
 * Represents a Kubernetes Deployment resource with:
 * - Pod template specification
 * - Replica management
 * - Revision history
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
} k8s_metadata_t;

typedef struct {
  int replicas;
  int updated_replicas;
  int ready_replicas;
  int available_replicas;
  int64_t observed_generation;
} k8s_deployment_status_t;

typedef struct {
  int32_t replicas;                    /* Desired number of replicas */
  json_object *selector;               /* Label selector for pods */
  json_object *template;               /* Pod template */
  int32_t revision_history_limit;      /* Number of old ReplicaSets to keep */
  int32_t progress_deadline_seconds;   /* Timeout for progress */
  char *strategy;                      /* RollingUpdate or Recreate */
} k8s_deployment_spec_t;

typedef struct {
  k8s_metadata_t metadata;
  k8s_deployment_spec_t spec;
  k8s_deployment_status_t status;
} k8s_deployment_t;

/* === Creation and Cleanup === */

/**
 * Create a new deployment
 */
k8s_deployment_t* k8s_deployment_create(const char *name, const char *namespace);

/**
 * Free a deployment
 */
void k8s_deployment_free(k8s_deployment_t *deployment);

/* === Serialization === */

/**
 * Convert deployment to JSON
 * Includes resourceVersion and generation for CAS operations
 */
json_object* k8s_deployment_to_json(const k8s_deployment_t *deployment);

/**
 * Parse deployment from JSON
 * Extracts resourceVersion and generation
 */
k8s_deployment_t* k8s_deployment_from_json(json_object *json);

/* === Resource Versioning === */

/**
 * Get resource version (for etcd CAS)
 */
int64_t k8s_deployment_get_resource_version(const k8s_deployment_t *deployment);

/**
 * Set resource version (updated by etcd after storage)
 */
void k8s_deployment_set_resource_version(k8s_deployment_t *deployment,
                                          int64_t resource_version);

/**
 * Get generation (for tracking spec changes)
 */
int64_t k8s_deployment_get_generation(const k8s_deployment_t *deployment);

/**
 * Increment generation (call when spec is modified)
 */
void k8s_deployment_increment_generation(k8s_deployment_t *deployment);

#endif /* DEPLOYMENT_H */
