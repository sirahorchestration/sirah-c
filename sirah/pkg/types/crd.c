#include <types/crd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// ============ Helper Functions ============

static int add_string_to_array(char* array[], int* count, int max, const char* str) {
    if (!str || !array || !count) return -1;
    if (*count >= max) return -1;
    array[*count] = strdup(str);
    if (!array[*count]) return -1;
    (*count)++;
    return 0;
}

// ============ CRD Names Functions ============

k8s_crd_names_t* k8s_crd_names_new(const char* plural, const char* singular, const char* kind) {
    k8s_crd_names_t* names = calloc(1, sizeof(k8s_crd_names_t));
    if (!names) return NULL;
    
    names->plural = strdup(plural);
    names->singular = strdup(singular);
    names->kind = strdup(kind);
    
    // Generate list kind (e.g., "DatabaseList")
    char list_kind[256];
    snprintf(list_kind, sizeof(list_kind), "%sList", kind);
    names->list_kind = strdup(list_kind);
    
    return names;
}

static void k8s_crd_names_free(k8s_crd_names_t* names) {
    if (!names) return;
    if (names->plural) free(names->plural);
    if (names->singular) free(names->singular);
    if (names->kind) free(names->kind);
    if (names->list_kind) free(names->list_kind);
    free(names);
}

// ============ CRD Condition Functions ============

static void k8s_crd_condition_free(k8s_crd_condition_t* condition) {
    if (!condition) return;
    if (condition->type) free(condition->type);
    if (condition->status) free(condition->status);
    if (condition->reason) free(condition->reason);
    if (condition->message) free(condition->message);
    if (condition->last_update) free(condition->last_update);
    free(condition);
}

// ============ CRD Specification Functions ============

static k8s_crd_spec_t* k8s_crd_spec_new() {
    k8s_crd_spec_t* spec = calloc(1, sizeof(k8s_crd_spec_t));
    if (!spec) return NULL;
    spec->scope = K8S_CRD_SCOPE_NAMESPACED;
    return spec;
}

static void k8s_crd_spec_free(k8s_crd_spec_t* spec) {
    if (!spec) return;
    if (spec->group) free(spec->group);
    if (spec->names) k8s_crd_names_free(spec->names);
    
    for (int i = 0; i < spec->num_versions; i++) {
        if (spec->versions[i]) free(spec->versions[i]);
    }
    
    if (spec->storage_version) free(spec->storage_version);
    if (spec->validation_schema_json) free(spec->validation_schema_json);
    
    for (int i = 0; i < spec->subresources.num_custom; i++) {
        if (spec->subresources.custom[i]) free(spec->subresources.custom[i]);
    }
    
    free(spec);
}

// ============ CRD Status Functions ============

static k8s_crd_status_t* k8s_crd_status_new() {
    return calloc(1, sizeof(k8s_crd_status_t));
}

static void k8s_crd_status_free(k8s_crd_status_t* status) {
    if (!status) return;
    
    for (int i = 0; i < status->num_conditions; i++) {
        if (status->conditions[i]) {
            k8s_crd_condition_free(status->conditions[i]);
        }
    }
    
    free(status);
}

// ============ CRD Lifecycle Functions ============

k8s_crd_t* k8s_crd_new(const char* name, const char* group) {
    if (!name || !group) return NULL;
    
    k8s_crd_t* crd = calloc(1, sizeof(k8s_crd_t));
    if (!crd) return NULL;
    
    crd->metadata = k8s_metadata_new(name, "");
    if (!crd->metadata) {
        free(crd);
        return NULL;
    }
    
    crd->spec = k8s_crd_spec_new();
    if (!crd->spec) {
        k8s_metadata_free(crd->metadata);
        free(crd);
        return NULL;
    }
    
    crd->spec->group = strdup(group);
    
    crd->status = k8s_crd_status_new();
    if (!crd->status) {
        k8s_crd_spec_free(crd->spec);
        k8s_metadata_free(crd->metadata);
        free(crd);
        return NULL;
    }
    
    return crd;
}

void k8s_crd_free(k8s_crd_t* crd) {
    if (!crd) return;
    
    if (crd->metadata) k8s_metadata_free(crd->metadata);
    if (crd->spec) k8s_crd_spec_free(crd->spec);
    if (crd->status) k8s_crd_status_free(crd->status);
    
    free(crd);
}

// ============ CRD Serialization ============

char* k8s_crd_to_json(k8s_crd_t* crd) {
    if (!crd || !crd->metadata || !crd->spec) return NULL;
    
    static char json[16384];
    int offset = 0;
    
    offset += snprintf(json + offset, sizeof(json) - offset,
        "{\"apiVersion\":\"apiextensions.k8s.io/v1\","
        "\"kind\":\"CustomResourceDefinition\","
        "\"metadata\":{\"name\":\"%s\",\"namespace\":\"%s\"},",
        crd->metadata->name ? crd->metadata->name : "",
        crd->metadata->namespace ? crd->metadata->namespace : "");
    
    // Spec section
    offset += snprintf(json + offset, sizeof(json) - offset,
        "\"spec\":{"
        "\"group\":\"%s\","
        "\"names\":{\"plural\":\"%s\",\"singular\":\"%s\",\"kind\":\"%s\"},",
        crd->spec->group ? crd->spec->group : "",
        crd->spec->names && crd->spec->names->plural ? crd->spec->names->plural : "",
        crd->spec->names && crd->spec->names->singular ? crd->spec->names->singular : "",
        crd->spec->names && crd->spec->names->kind ? crd->spec->names->kind : "");
    
    // Versions
    offset += snprintf(json + offset, sizeof(json) - offset, "\"versions\":[");
    for (int i = 0; i < crd->spec->num_versions; i++) {
        if (i > 0) offset += snprintf(json + offset, sizeof(json) - offset, ",");
        offset += snprintf(json + offset, sizeof(json) - offset,
            "{\"name\":\"%s\",\"served\":true,\"storage\":%s}",
            crd->spec->versions[i],
            (crd->spec->storage_version && strcmp(crd->spec->storage_version, crd->spec->versions[i]) == 0) ? "true" : "false");
    }
    offset += snprintf(json + offset, sizeof(json) - offset, "],");
    
    // Scope
    offset += snprintf(json + offset, sizeof(json) - offset,
        "\"scope\":\"%s\"}",
        crd->spec->scope == K8S_CRD_SCOPE_CLUSTER ? "Cluster" : "Namespaced");
    
    // Status section
    offset += snprintf(json + offset, sizeof(json) - offset,
        ",\"status\":{\"conditions\":[");
    for (int i = 0; i < crd->status->num_conditions; i++) {
        if (i > 0) offset += snprintf(json + offset, sizeof(json) - offset, ",");
        k8s_crd_condition_t* cond = crd->status->conditions[i];
        offset += snprintf(json + offset, sizeof(json) - offset,
            "{\"type\":\"%s\",\"status\":\"%s\",\"reason\":\"%s\"}",
            cond->type ? cond->type : "",
            cond->status ? cond->status : "Unknown",
            cond->reason ? cond->reason : "");
    }
    offset += snprintf(json + offset, sizeof(json) - offset, "]}");
    
    offset += snprintf(json + offset, sizeof(json) - offset, "}");
    
    return json;
}

k8s_crd_t* k8s_crd_from_json(const char* json) {
    (void)json; // Unused parameter
    // TODO: Implement JSON parsing
    return NULL;
}

// ============ CRD Specification Setters ============

int k8s_crd_add_version(k8s_crd_t* crd, const char* version) {
    if (!crd || !crd->spec || !version) return -1;
    return add_string_to_array(crd->spec->versions, &crd->spec->num_versions, K8S_MAX_CRD_VERSIONS, version);
}

int k8s_crd_set_storage_version(k8s_crd_t* crd, const char* version) {
    if (!crd || !crd->spec || !version) return -1;
    if (crd->spec->storage_version) free(crd->spec->storage_version);
    crd->spec->storage_version = strdup(version);
    return crd->spec->storage_version ? 0 : -1;
}

int k8s_crd_set_scope(k8s_crd_t* crd, k8s_crd_scope_t scope) {
    if (!crd || !crd->spec) return -1;
    crd->spec->scope = scope;
    return 0;
}

int k8s_crd_set_validation_schema(k8s_crd_t* crd, const char* schema_json) {
    if (!crd || !crd->spec || !schema_json) return -1;
    if (crd->spec->validation_schema_json) free(crd->spec->validation_schema_json);
    crd->spec->validation_schema_json = strdup(schema_json);
    return crd->spec->validation_schema_json ? 0 : -1;
}

int k8s_crd_add_subresource(k8s_crd_t* crd, const char* name) {
    if (!crd || !crd->spec || !name) return -1;
    
    // Handle built-in subresources
    if (strcmp(name, "status") == 0) {
        crd->spec->subresources.has_status = true;
        return 0;
    }
    if (strcmp(name, "scale") == 0) {
        crd->spec->subresources.has_scale = true;
        return 0;
    }
    
    // Handle custom subresources
    return add_string_to_array(crd->spec->subresources.custom, &crd->spec->subresources.num_custom, K8S_MAX_SUBRESOURCES, name);
}

// ============ Custom Resource Functions ============

k8s_custom_resource_t* k8s_custom_resource_new(const char* name, const char* namespace) {
    if (!name) return NULL;
    
    k8s_custom_resource_t* resource = calloc(1, sizeof(k8s_custom_resource_t));
    if (!resource) return NULL;
    
    resource->metadata = k8s_metadata_new(name, namespace ? namespace : "");
    if (!resource->metadata) {
        free(resource);
        return NULL;
    }
    
    return resource;
}

void k8s_custom_resource_free(k8s_custom_resource_t* resource) {
    if (!resource) return;
    if (resource->metadata) k8s_metadata_free(resource->metadata);
    if (resource->kind) free(resource->kind);
    if (resource->api_version) free(resource->api_version);
    if (resource->spec_json) free(resource->spec_json);
    if (resource->status_json) free(resource->status_json);
    free(resource);
}

char* k8s_custom_resource_to_json(k8s_custom_resource_t* resource) {
    if (!resource || !resource->metadata) return NULL;
    
    static char json[8192];
    snprintf(json, sizeof(json),
        "{\"apiVersion\":\"%s\","
        "\"kind\":\"%s\","
        "\"metadata\":{\"name\":\"%s\",\"namespace\":\"%s\"},"
        "\"spec\":%s,"
        "\"status\":%s}",
        resource->api_version ? resource->api_version : "v1",
        resource->kind ? resource->kind : "CustomResource",
        resource->metadata->name ? resource->metadata->name : "",
        resource->metadata->namespace ? resource->metadata->namespace : "default",
        resource->spec_json ? resource->spec_json : "{}",
        resource->status_json ? resource->status_json : "{}");
    
    return json;
}

k8s_custom_resource_t* k8s_custom_resource_from_json(const char* json) {
    (void)json; // Unused parameter
    // TODO: Implement JSON parsing
    return NULL;
}

int k8s_custom_resource_set_kind(k8s_custom_resource_t* resource, const char* kind) {
    if (!resource || !kind) return -1;
    if (resource->kind) free(resource->kind);
    resource->kind = strdup(kind);
    return resource->kind ? 0 : -1;
}

int k8s_custom_resource_set_api_version(k8s_custom_resource_t* resource, const char* api_version) {
    if (!resource || !api_version) return -1;
    if (resource->api_version) free(resource->api_version);
    resource->api_version = strdup(api_version);
    return resource->api_version ? 0 : -1;
}

int k8s_custom_resource_set_spec(k8s_custom_resource_t* resource, const char* spec_json) {
    if (!resource || !spec_json) return -1;
    if (resource->spec_json) free(resource->spec_json);
    resource->spec_json = strdup(spec_json);
    return resource->spec_json ? 0 : -1;
}

int k8s_custom_resource_set_status(k8s_custom_resource_t* resource, const char* status_json) {
    if (!resource || !status_json) return -1;
    if (resource->status_json) free(resource->status_json);
    resource->status_json = strdup(status_json);
    return resource->status_json ? 0 : -1;
}
