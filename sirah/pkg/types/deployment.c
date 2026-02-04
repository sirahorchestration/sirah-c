#include "deployment.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uuid/uuid.h>

/* === Creation and Cleanup === */

k8s_deployment_t* k8s_deployment_create(const char *name, const char *namespace) {
  if (name == NULL || namespace == NULL) {
    return NULL;
  }
  
  k8s_deployment_t *deployment = calloc(1, sizeof(k8s_deployment_t));
  if (deployment == NULL) {
    return NULL;
  }
  
  /* Set metadata */
  deployment->metadata.name = strdup(name);
  deployment->metadata.namespace = strdup(namespace);
  deployment->metadata.resource_version = 1;
  deployment->metadata.generation = 1;
  
  /* Generate UID */
  uuid_t bin_uuid;
  uuid_generate(bin_uuid);
  uuid_t uuid;
  uuid_unparse(bin_uuid, (char *)uuid);
  deployment->metadata.uid = (int64_t)time(NULL) * 1000000; /* Simplified UID */
  
  /* Set creation timestamp */
  time_t now = time(NULL);
  struct tm *timeinfo = gmtime(&now);
  deployment->metadata.creation_timestamp = malloc(32);
  strftime(deployment->metadata.creation_timestamp, 32, "%Y-%m-%dT%H:%M:%SZ", timeinfo);
  
  /* Initialize empty labels and annotations */
  deployment->metadata.labels = json_object_new_object();
  deployment->metadata.annotations = json_object_new_object();
  
  /* Default spec */
  deployment->spec.replicas = 1;
  deployment->spec.revision_history_limit = 10;
  deployment->spec.progress_deadline_seconds = 600;
  deployment->spec.strategy = strdup("RollingUpdate");
  deployment->spec.selector = json_object_new_object();
  deployment->spec.template = json_object_new_object();
  
  /* Default status */
  deployment->status.replicas = 0;
  deployment->status.updated_replicas = 0;
  deployment->status.ready_replicas = 0;
  deployment->status.available_replicas = 0;
  deployment->status.observed_generation = 1;
  
  return deployment;
}

void k8s_deployment_free(k8s_deployment_t *deployment) {
  if (deployment == NULL) {
    return;
  }
  
  free(deployment->metadata.name);
  free(deployment->metadata.namespace);
  free(deployment->metadata.creation_timestamp);
  if (deployment->metadata.deletion_timestamp != NULL) {
    free(deployment->metadata.deletion_timestamp);
  }
  
  if (deployment->metadata.labels != NULL) {
    json_object_put(deployment->metadata.labels);
  }
  if (deployment->metadata.annotations != NULL) {
    json_object_put(deployment->metadata.annotations);
  }
  
  free(deployment->spec.strategy);
  if (deployment->spec.selector != NULL) {
    json_object_put(deployment->spec.selector);
  }
  if (deployment->spec.template != NULL) {
    json_object_put(deployment->spec.template);
  }
  
  free(deployment);
}

/* === Serialization === */

json_object* k8s_deployment_to_json(const k8s_deployment_t *deployment) {
  if (deployment == NULL) {
    return NULL;
  }
  
  json_object *root = json_object_new_object();
  
  json_object_object_add(root, "apiVersion", 
                         json_object_new_string("apps/v1"));
  json_object_object_add(root, "kind",
                         json_object_new_string("Deployment"));
  
  /* Metadata */
  json_object *metadata = json_object_new_object();
  json_object_object_add(metadata, "name",
                         json_object_new_string(deployment->metadata.name));
  json_object_object_add(metadata, "namespace",
                         json_object_new_string(deployment->metadata.namespace));
  json_object_object_add(metadata, "uid",
                         json_object_new_int64(deployment->metadata.uid));
  json_object_object_add(metadata, "resourceVersion",
                         json_object_new_int64(deployment->metadata.resource_version));
  json_object_object_add(metadata, "generation",
                         json_object_new_int64(deployment->metadata.generation));
  json_object_object_add(metadata, "creationTimestamp",
                         json_object_new_string(deployment->metadata.creation_timestamp));
  
  if (deployment->metadata.deletion_timestamp != NULL) {
    json_object_object_add(metadata, "deletionTimestamp",
                           json_object_new_string(deployment->metadata.deletion_timestamp));
  }
  
  json_object_object_add(metadata, "labels",
                         json_object_get(deployment->metadata.labels));
  json_object_object_add(metadata, "annotations",
                         json_object_get(deployment->metadata.annotations));
  
  json_object_object_add(root, "metadata", metadata);
  
  /* Spec */
  json_object *spec = json_object_new_object();
  json_object_object_add(spec, "replicas",
                         json_object_new_int(deployment->spec.replicas));
  json_object_object_add(spec, "selector",
                         json_object_get(deployment->spec.selector));
  json_object_object_add(spec, "template",
                         json_object_get(deployment->spec.template));
  
  json_object *strategy = json_object_new_object();
  json_object_object_add(strategy, "type",
                         json_object_new_string(deployment->spec.strategy));
  json_object_object_add(spec, "strategy", strategy);
  
  json_object_object_add(spec, "revisionHistoryLimit",
                         json_object_new_int(deployment->spec.revision_history_limit));
  json_object_object_add(spec, "progressDeadlineSeconds",
                         json_object_new_int(deployment->spec.progress_deadline_seconds));
  
  json_object_object_add(root, "spec", spec);
  
  /* Status */
  json_object *status = json_object_new_object();
  json_object_object_add(status, "replicas",
                         json_object_new_int(deployment->status.replicas));
  json_object_object_add(status, "updatedReplicas",
                         json_object_new_int(deployment->status.updated_replicas));
  json_object_object_add(status, "readyReplicas",
                         json_object_new_int(deployment->status.ready_replicas));
  json_object_object_add(status, "availableReplicas",
                         json_object_new_int(deployment->status.available_replicas));
  json_object_object_add(status, "observedGeneration",
                         json_object_new_int64(deployment->status.observed_generation));
  
  json_object_object_add(root, "status", status);
  
  return root;
}

k8s_deployment_t* k8s_deployment_from_json(json_object *json) {
  if (json == NULL) {
    return NULL;
  }
  
  /* Extract metadata */
  json_object *metadata_obj = json_object_object_get(json, "metadata");
  if (metadata_obj == NULL) {
    return NULL;
  }
  
  const char *name = json_object_get_string(json_object_object_get(metadata_obj, "name"));
  const char *namespace = json_object_get_string(json_object_object_get(metadata_obj, "namespace"));
  
  if (name == NULL || namespace == NULL) {
    return NULL;
  }
  
  k8s_deployment_t *deployment = k8s_deployment_create(name, namespace);
  
  /* Parse metadata fields */
  json_object *rv_obj = json_object_object_get(metadata_obj, "resourceVersion");
  if (rv_obj != NULL) {
    deployment->metadata.resource_version = json_object_get_int64(rv_obj);
  }
  
  json_object *gen_obj = json_object_object_get(metadata_obj, "generation");
  if (gen_obj != NULL) {
    deployment->metadata.generation = json_object_get_int64(gen_obj);
  }
  
  /* Parse spec */
  json_object *spec_obj = json_object_object_get(json, "spec");
  if (spec_obj != NULL) {
    json_object *replicas_obj = json_object_object_get(spec_obj, "replicas");
    if (replicas_obj != NULL) {
      deployment->spec.replicas = json_object_get_int(replicas_obj);
    }
    
    json_object *selector_obj = json_object_object_get(spec_obj, "selector");
    if (selector_obj != NULL) {
      json_object_put(deployment->spec.selector);
      deployment->spec.selector = json_object_get(selector_obj);
    }
  }
  
  return deployment;
}

/* === Resource Versioning === */

int64_t k8s_deployment_get_resource_version(const k8s_deployment_t *deployment) {
  if (deployment == NULL) {
    return 0;
  }
  return deployment->metadata.resource_version;
}

void k8s_deployment_set_resource_version(k8s_deployment_t *deployment,
                                          int64_t resource_version) {
  if (deployment != NULL) {
    deployment->metadata.resource_version = resource_version;
  }
}

int64_t k8s_deployment_get_generation(const k8s_deployment_t *deployment) {
  if (deployment == NULL) {
    return 0;
  }
  return deployment->metadata.generation;
}

void k8s_deployment_increment_generation(k8s_deployment_t *deployment) {
  if (deployment != NULL) {
    deployment->metadata.generation++;
  }
}
