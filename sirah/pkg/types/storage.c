#include "storage.h"
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include <time.h>

// ============ PersistentVolume ==============

k8s_persistent_volume_t* k8s_pv_new(const char* name) {
    if (!name) return NULL;
    
    k8s_persistent_volume_t* pv = (k8s_persistent_volume_t*)malloc(sizeof(k8s_persistent_volume_t));
    
    pv->metadata = (k8s_metadata_t){
        .name = strdup(name),
        .namespace = strdup(""),  // PVs are cluster-scoped
        .uid = strdup(""),
        .resource_version = strdup("1"),
        .creation_timestamp = time(NULL),
        .deletion_timestamp = 0,
        .owner_references = NULL,
        .num_owners = 0,
        .finalizers = NULL,
        .num_finalizers = 0
    };
    
    pv->spec = (k8s_pv_spec_t){
        .storage_class = strdup(""),
        .capacity_bytes = 1024*1024*1024,  // 1GB default
        .access_modes = {ACCESS_MODE_READ_WRITE_ONCE},
        .num_access_modes = 1,
        .type = strdup("hostPath"),
        .path = strdup("")
    };
    
    pv->status = VOLUME_STATUS_AVAILABLE;
    pv->claim_ref = NULL;
    
    return pv;
}

void k8s_pv_free(k8s_persistent_volume_t* pv) {
    if (!pv) return;
    
    free(pv->metadata.name);
    free(pv->metadata.namespace);
    free(pv->metadata.uid);
    free(pv->metadata.resource_version);
    free(pv->spec.storage_class);
    free(pv->spec.type);
    free(pv->spec.path);
    if (pv->claim_ref) free(pv->claim_ref);
    
    free(pv);
}

char* k8s_pv_to_json(k8s_persistent_volume_t* pv) {
    if (!pv) return strdup("{}");
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("PersistentVolume"));
    
    json_object* meta = json_object_new_object();
    json_object_object_add(meta, "name", json_object_new_string(pv->metadata.name));
    json_object_object_add(root, "metadata", meta);
    
    json_object* spec = json_object_new_object();
    json_object_object_add(spec, "capacity", json_object_new_int(pv->spec.capacity_bytes));
    json_object_object_add(spec, "type", json_object_new_string(pv->spec.type));
    json_object_object_add(spec, "path", json_object_new_string(pv->spec.path));
    json_object_object_add(root, "spec", spec);
    
    json_object* status = json_object_new_object();
    const char* status_str = "Available";
    if (pv->status == VOLUME_STATUS_BOUND) status_str = "Bound";
    else if (pv->status == VOLUME_STATUS_RELEASED) status_str = "Released";
    else if (pv->status == VOLUME_STATUS_FAILED) status_str = "Failed";
    
    json_object_object_add(status, "phase", json_object_new_string(status_str));
    json_object_object_add(root, "status", status);
    
    const char* json_str = json_object_to_json_string(root);
    char* result = strdup(json_str);
    json_object_put(root);
    
    return result;
}

// ============ PersistentVolumeClaim ==============

k8s_persistent_volume_claim_t* k8s_pvc_new(const char* name, const char* namespace) {
    if (!name || !namespace) return NULL;
    
    k8s_persistent_volume_claim_t* pvc = (k8s_persistent_volume_claim_t*)malloc(sizeof(k8s_persistent_volume_claim_t));
    
    pvc->metadata = (k8s_metadata_t){
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
    
    pvc->spec.access_modes[0] = ACCESS_MODE_READ_WRITE_ONCE;
    pvc->spec.num_access_modes = 1;
    pvc->spec.storage_bytes = 1024*1024*1024;
    pvc->spec.storage_class = strdup("");
    
    pvc->status.phase = VOLUME_STATUS_AVAILABLE;
    pvc->status.volume_name = NULL;
    
    return pvc;
}

void k8s_pvc_free(k8s_persistent_volume_claim_t* pvc) {
    if (!pvc) return;
    
    free(pvc->metadata.name);
    free(pvc->metadata.namespace);
    free(pvc->metadata.uid);
    free(pvc->metadata.resource_version);
    free(pvc->spec.storage_class);
    if (pvc->status.volume_name) free(pvc->status.volume_name);
    
    free(pvc);
}

char* k8s_pvc_to_json(k8s_persistent_volume_claim_t* pvc) {
    if (!pvc) return strdup("{}");
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("PersistentVolumeClaim"));
    
    json_object* meta = json_object_new_object();
    json_object_object_add(meta, "name", json_object_new_string(pvc->metadata.name));
    json_object_object_add(meta, "namespace", json_object_new_string(pvc->metadata.namespace));
    json_object_object_add(root, "metadata", meta);
    
    json_object* spec = json_object_new_object();
    json_object_object_add(spec, "storage", json_object_new_int(pvc->spec.storage_bytes));
    json_object_object_add(root, "spec", spec);
    
    json_object* status = json_object_new_object();
    const char* status_str = "Available";
    if (pvc->status.phase == VOLUME_STATUS_BOUND) status_str = "Bound";
    json_object_object_add(status, "phase", json_object_new_string(status_str));
    if (pvc->status.volume_name) {
        json_object_object_add(status, "volumeName", json_object_new_string(pvc->status.volume_name));
    }
    json_object_object_add(root, "status", status);
    
    const char* json_str = json_object_to_json_string(root);
    char* result = strdup(json_str);
    json_object_put(root);
    
    return result;
}

// ============ StatefulSet ==============

k8s_statefulset_t* k8s_statefulset_new(const char* name, const char* namespace) {
    if (!name || !namespace) return NULL;
    
    k8s_statefulset_t* sts = (k8s_statefulset_t*)malloc(sizeof(k8s_statefulset_t));
    
    sts->metadata = (k8s_metadata_t){
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
    
    sts->spec.replicas = 1;
    sts->spec.service_name = strdup("");
    sts->spec.labels = NULL;
    sts->spec.num_labels = 0;
    
    sts->status.ready_replicas = 0;
    sts->status.updated_replicas = 0;
    
    return sts;
}

void k8s_statefulset_free(k8s_statefulset_t* sts) {
    if (!sts) return;
    
    free(sts->metadata.name);
    free(sts->metadata.namespace);
    free(sts->metadata.uid);
    free(sts->metadata.resource_version);
    free(sts->spec.service_name);
    free(sts->spec.labels);
    
    free(sts);
}

char* k8s_statefulset_to_json(k8s_statefulset_t* sts) {
    if (!sts) return strdup("{}");
    
    json_object* root = json_object_new_object();
    json_object_object_add(root, "apiVersion", json_object_new_string("apps/v1"));
    json_object_object_add(root, "kind", json_object_new_string("StatefulSet"));
    
    json_object* meta = json_object_new_object();
    json_object_object_add(meta, "name", json_object_new_string(sts->metadata.name));
    json_object_object_add(meta, "namespace", json_object_new_string(sts->metadata.namespace));
    json_object_object_add(root, "metadata", meta);
    
    json_object* spec = json_object_new_object();
    json_object_object_add(spec, "replicas", json_object_new_int(sts->spec.replicas));
    json_object_object_add(spec, "serviceName", json_object_new_string(sts->spec.service_name));
    json_object_object_add(root, "spec", spec);
    
    json_object* status = json_object_new_object();
    json_object_object_add(status, "readyReplicas", json_object_new_int(sts->status.ready_replicas));
    json_object_object_add(status, "updatedReplicas", json_object_new_int(sts->status.updated_replicas));
    json_object_object_add(root, "status", status);
    
    const char* json_str = json_object_to_json_string(root);
    char* result = strdup(json_str);
    json_object_put(root);
    
    return result;
}
