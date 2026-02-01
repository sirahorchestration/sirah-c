#ifndef K8S_CRD_H
#define K8S_CRD_H

#include <stdbool.h>
#include <types/common.h>

#define K8S_MAX_CRD_VERSIONS 5
#define K8S_MAX_SUBRESOURCES 5
#define K8S_MAX_CRD_CONDITIONS 10
#define K8S_MAX_CRD_STORED 100

// CRD scope
typedef enum {
    K8S_CRD_SCOPE_NAMESPACED,
    K8S_CRD_SCOPE_CLUSTER
} k8s_crd_scope_t;

// Name format for CRD
typedef struct {
    char* plural;       // e.g., "databases"
    char* singular;     // e.g., "database"
    char* kind;         // e.g., "Database"
    char* list_kind;    // e.g., "DatabaseList"
} k8s_crd_names_t;

// CRD specification
typedef struct {
    char* group;                    // e.g., "myapp.example.com"
    k8s_crd_names_t* names;
    
    char* versions[K8S_MAX_CRD_VERSIONS];
    int num_versions;
    char* storage_version;          // Which version is stored in etcd
    
    k8s_crd_scope_t scope;
    
    // JSON schema for validation (simplified)
    char* validation_schema_json;   // OpenAPI v3 schema
    
    // Sub-resources like /status, /scale
    struct {
        bool has_status;
        bool has_scale;
        char* custom[K8S_MAX_SUBRESOURCES];
        int num_custom;
    } subresources;
} k8s_crd_spec_t;

// CRD condition
typedef struct {
    char* type;          // e.g., "Established", "NamesAccepted"
    char* status;        // "True", "False", "Unknown"
    char* reason;
    char* message;
    char* last_update;
} k8s_crd_condition_t;

// CRD status
typedef struct {
    int accepted_names_plural;  // Whether names accepted (1=yes, 0=no)
    
    k8s_crd_condition_t* conditions[K8S_MAX_CRD_CONDITIONS];
    int num_conditions;
} k8s_crd_status_t;

// Custom Resource Definition
typedef struct {
    k8s_metadata_t* metadata;
    k8s_crd_spec_t* spec;
    k8s_crd_status_t* status;
} k8s_crd_t;

// Instance of a custom resource
typedef struct {
    k8s_metadata_t* metadata;
    char* kind;                 // Reference to CRD kind
    char* api_version;          // Reference to group/version
    char* spec_json;            // Arbitrary JSON spec
    char* status_json;          // Arbitrary JSON status
} k8s_custom_resource_t;

// ============ CRD Functions ============

// Lifecycle
k8s_crd_t* k8s_crd_new(const char* name, const char* group);
void k8s_crd_free(k8s_crd_t* crd);

// Serialization
char* k8s_crd_to_json(k8s_crd_t* crd);
k8s_crd_t* k8s_crd_from_json(const char* json);

// Specification
int k8s_crd_add_version(k8s_crd_t* crd, const char* version);
int k8s_crd_set_storage_version(k8s_crd_t* crd, const char* version);
int k8s_crd_set_scope(k8s_crd_t* crd, k8s_crd_scope_t scope);
int k8s_crd_set_validation_schema(k8s_crd_t* crd, const char* schema_json);
int k8s_crd_add_subresource(k8s_crd_t* crd, const char* name);

// Custom Resource Functions
k8s_custom_resource_t* k8s_custom_resource_new(const char* name, const char* namespace);
void k8s_custom_resource_free(k8s_custom_resource_t* resource);
char* k8s_custom_resource_to_json(k8s_custom_resource_t* resource);
k8s_custom_resource_t* k8s_custom_resource_from_json(const char* json);

int k8s_custom_resource_set_kind(k8s_custom_resource_t* resource, const char* kind);
int k8s_custom_resource_set_api_version(k8s_custom_resource_t* resource, const char* api_version);
int k8s_custom_resource_set_spec(k8s_custom_resource_t* resource, const char* spec_json);
int k8s_custom_resource_set_status(k8s_custom_resource_t* resource, const char* status_json);

#endif // K8S_CRD_H
