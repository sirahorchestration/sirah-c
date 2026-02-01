// pkg/types/common.c
#include "common.h"
#include <stdlib.h>
#include <string.h>

k8s_metadata_t* k8s_metadata_new(const char* name, const char* namespace) {
    k8s_metadata_t* meta = malloc(sizeof(*meta));
    if (!meta) return NULL;
    
    meta->name = strdup(name);
    meta->namespace = strdup(namespace);
    meta->creation_timestamp = time(NULL);
    meta->deletion_timestamp = 0;
    
    // Generate UUID
    uuid_t uuid;
    uuid_generate(uuid);
    meta->uid = malloc(37);
    uuid_unparse(uuid, meta->uid);
    
    meta->resource_version = strdup("0");
    meta->owner_references = NULL;
    meta->num_owners = 0;
    meta->finalizers = NULL;
    meta->num_finalizers = 0;
    
    return meta;
}

void k8s_metadata_free(k8s_metadata_t* meta) {
    if (!meta) return;
    free(meta->name);
    free(meta->namespace);
    free(meta->uid);
    free(meta->resource_version);
    free(meta);
}

char* k8s_metadata_to_json(k8s_metadata_t* meta) {
    // TODO: Implement JSON serialization
    return strdup("{}");
}

k8s_metadata_t* k8s_metadata_from_json(json_object* obj) {
    // TODO: Implement JSON deserialization
    return NULL;
}
