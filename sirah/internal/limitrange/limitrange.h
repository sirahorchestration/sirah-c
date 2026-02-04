/*
 * LimitRange API Header - Kubernetes v1.28
 *
 * Manages LimitRange resources for enforcing resource constraints
 * on containers and pods within a namespace.
 *
 * Key Features:
 * - Per-container min/max CPU and memory limits
 * - Pod-level aggregate resource limits
 * - Default resource requests and limits
 * - Ratio enforcement (max/min request)
 * - Namespace-scoped enforcement
 * - Admission control integration
 *
 * Thread Safety: All operations protected by mutex lock
 * Max Resources: 100 LimitRanges per cluster
 */

#ifndef SIRAH_LIMITRANGE_H
#define SIRAH_LIMITRANGE_H

#include <pthread.h>
#include <time.h>
#include <stdint.h>

/* LimitRange Item Type */
typedef enum {
    LIMITRANGE_TYPE_CONTAINER,          /* Per-container limits */
    LIMITRANGE_TYPE_POD,                /* Pod aggregate limits */
    LIMITRANGE_TYPE_PVC,                /* PersistentVolumeClaim limits */
    LIMITRANGE_TYPE_CUSTOM              /* Custom resource type */
} limitrange_item_type_t;

/* Resource Limit Definition */
typedef struct {
    uint64_t min_bytes;                 /* Minimum allowed (0 = no limit) */
    uint64_t max_bytes;                 /* Maximum allowed (0 = no limit) */
    uint64_t default_bytes;             /* Default if not specified */
    uint64_t default_request_bytes;     /* Default request if not specified */
    uint64_t max_limit_request_ratio;   /* Max(limit)/min(request) ratio * 1000 */
} resource_limit_t;

/* LimitRange Item */
typedef struct {
    limitrange_item_type_t type;        /* Container, Pod, PVC */
    char type_name[256];                /* Custom type name */
    
    /* CPU limits */
    resource_limit_t cpu;
    
    /* Memory limits */
    resource_limit_t memory;
    
    /* Storage limits */
    resource_limit_t storage;
} limitrange_item_t;

/* LimitRange Specification */
typedef struct {
    char name[256];                     /* LimitRange name */
    char namespace[128];                /* Namespace (REQUIRED) */
    
    /* Items defining limits */
    limitrange_item_t items[10];
    int item_count;
    
    /* Timestamps */
    time_t created_at;
    time_t updated_at;
    uint32_t generation;                /* Kubernetes generation counter */
} limitrange_spec_t;

/* LimitRange Status */
typedef struct {
    char message[256];                  /* Status message */
    int enforcing;                      /* 1 if actively enforced */
} limitrange_status_t;

/* LimitRange Object */
typedef struct {
    limitrange_spec_t spec;
    limitrange_status_t status;
} limitrange_t;

/* LimitRange Manager */
typedef struct {
    pthread_mutex_t lock;
    int initialized;
    limitrange_t limitranges[100];      /* Max 100 LimitRanges */
    int count;
} limitrange_manager_t;

/* ============================================================================
 * Lifecycle Management
 * ========================================================================== */

/**
 * Initialize the LimitRange manager
 * @return 0 on success, -1 on error
 */
int limitrange_manager_init(void);

/**
 * Shutdown the LimitRange manager
 * @return 0 on success, -1 on error
 */
int limitrange_manager_shutdown(void);

/* ============================================================================
 * CRUD Operations (Namespace-Scoped)
 * ========================================================================== */

/**
 * Create a new LimitRange
 * @param namespace LimitRange namespace (required)
 * @param name LimitRange name
 * @param spec LimitRange specification
 * @return 0 on success, -1 on duplicate/error, -2 if invalid spec
 */
int limitrange_create(const char *namespace, const char *name, const limitrange_spec_t *spec);

/**
 * Get a LimitRange by name
 * @param namespace LimitRange namespace
 * @param name LimitRange name
 * @param out_limitrange Pointer to store LimitRange data
 * @return 0 on success, -1 if not found
 */
int limitrange_get(const char *namespace, const char *name, limitrange_t *out_limitrange);

/**
 * Update a LimitRange
 * @param namespace LimitRange namespace
 * @param name LimitRange name
 * @param spec New specification
 * @return 0 on success, -1 if not found, -2 if invalid
 */
int limitrange_update(const char *namespace, const char *name, const limitrange_spec_t *spec);

/**
 * Delete a LimitRange
 * @param namespace LimitRange namespace
 * @param name LimitRange name
 * @return 0 on success, -1 if not found
 */
int limitrange_delete(const char *namespace, const char *name);

/**
 * List all LimitRanges in namespace
 * @param namespace Namespace filter (required)
 * @param out_limitranges Output array (caller allocated, size >= 100)
 * @return Number of LimitRanges listed, -1 on error
 */
int limitrange_list(const char *namespace, limitrange_t *out_limitranges);

/**
 * List count of LimitRanges in namespace
 * @param namespace Namespace (required)
 * @return Count of LimitRanges
 */
int limitrange_list_count(const char *namespace);

/* ============================================================================
 * Container Validation
 * ========================================================================== */

/**
 * Validate container against LimitRange in namespace
 * @param namespace Namespace with LimitRanges
 * @param cpu_request CPU request in millicores (0 = unspecified)
 * @param cpu_limit CPU limit in millicores (0 = unspecified)
 * @param memory_request Memory request in bytes (0 = unspecified)
 * @param memory_limit Memory limit in bytes (0 = unspecified)
 * @param out_reason Output reason if validation fails (caller allocated, >= 256)
 * @return 1 if valid, 0 if violates LimitRange
 */
int limitrange_validate_container(const char *namespace,
                                   uint64_t cpu_request, uint64_t cpu_limit,
                                   uint64_t memory_request, uint64_t memory_limit,
                                   char *out_reason);

/**
 * Apply default requests to container (if not specified)
 * @param namespace Namespace with LimitRanges
 * @param in_cpu_request Container's CPU request (0 = not specified)
 * @param out_cpu_request Output CPU request after defaults applied
 * @param in_memory_request Container's memory request (0 = not specified)
 * @param out_memory_request Output memory request after defaults applied
 * @return 0 on success, -1 if namespace has no LimitRanges
 */
int limitrange_apply_container_defaults(const char *namespace,
                                         uint64_t in_cpu_request, uint64_t *out_cpu_request,
                                         uint64_t in_memory_request, uint64_t *out_memory_request);

/**
 * Apply default limits to container (if not specified)
 * @param namespace Namespace with LimitRanges
 * @param in_cpu_limit Container's CPU limit (0 = not specified)
 * @param out_cpu_limit Output CPU limit after defaults applied
 * @param in_memory_limit Container's memory limit (0 = not specified)
 * @param out_memory_limit Output memory limit after defaults applied
 * @return 0 on success, -1 if namespace has no LimitRanges
 */
int limitrange_apply_container_limit_defaults(const char *namespace,
                                               uint64_t in_cpu_limit, uint64_t *out_cpu_limit,
                                               uint64_t in_memory_limit, uint64_t *out_memory_limit);

/* ============================================================================
 * Pod-Level Validation
 * ========================================================================== */

/**
 * Validate pod aggregate resources against LimitRange
 * @param namespace Namespace with LimitRanges
 * @param total_cpu_request Total CPU request for all containers
 * @param total_cpu_limit Total CPU limit for all containers
 * @param total_memory_request Total memory request for all containers
 * @param total_memory_limit Total memory limit for all containers
 * @param container_count Number of containers in pod
 * @param out_reason Output reason if validation fails (caller allocated, >= 256)
 * @return 1 if valid, 0 if violates LimitRange
 */
int limitrange_validate_pod(const char *namespace,
                             uint64_t total_cpu_request, uint64_t total_cpu_limit,
                             uint64_t total_memory_request, uint64_t total_memory_limit,
                             int container_count,
                             char *out_reason);

/**
 * Apply default CPU limits to pod (if not specified)
 * @param namespace Namespace with LimitRanges
 * @param in_cpu_limit Pod's CPU limit (0 = not specified)
 * @param out_cpu_limit Output CPU limit after defaults applied
 * @return 0 on success, -1 if namespace has no LimitRanges
 */
int limitrange_apply_pod_limit_defaults(const char *namespace,
                                         uint64_t in_cpu_limit, uint64_t *out_cpu_limit);

/* ============================================================================
 * Storage Limit Validation (for PVC)
 * ========================================================================== */

/**
 * Validate storage request against LimitRange
 * @param namespace Namespace with LimitRanges
 * @param storage_bytes Storage request in bytes
 * @param out_reason Output reason if validation fails
 * @return 1 if valid, 0 if violates LimitRange
 */
int limitrange_validate_storage(const char *namespace, uint64_t storage_bytes, char *out_reason);

/* ============================================================================
 * Item Query Functions
 * ========================================================================== */

/**
 * Get container LimitRange item for namespace
 * @param namespace Namespace
 * @param out_item Output item
 * @return 0 on success, -1 if not found
 */
int limitrange_get_container_item(const char *namespace, limitrange_item_t *out_item);

/**
 * Get pod LimitRange item for namespace
 * @param namespace Namespace
 * @param out_item Output item
 * @return 0 on success, -1 if not found
 */
int limitrange_get_pod_item(const char *namespace, limitrange_item_t *out_item);

/**
 * Get PVC LimitRange item for namespace
 * @param namespace Namespace
 * @param out_item Output item
 * @return 0 on success, -1 if not found
 */
int limitrange_get_pvc_item(const char *namespace, limitrange_item_t *out_item);

/* ============================================================================
 * Status Query Functions
 * ========================================================================== */

/**
 * Get LimitRange status as JSON
 * @param namespace LimitRange namespace
 * @param name LimitRange name
 * @param out_json Output JSON string (caller allocated, >= 1024)
 * @return 0 on success, -1 if not found
 */
int limitrange_get_status_json(const char *namespace, const char *name, char *out_json);

/**
 * Check if LimitRange is actively enforcing limits
 * @param namespace LimitRange namespace
 * @param name LimitRange name
 * @return 1 if enforcing, 0 if not
 */
int limitrange_is_enforcing(const char *namespace, const char *name);

/* ============================================================================
 * Validation & Admission Control
 * ========================================================================== */

/**
 * Validate LimitRange specification
 * @param spec LimitRange specification to validate
 * @param out_reason Output reason if invalid (caller allocated, >= 256)
 * @return 1 if valid, 0 if invalid
 */
int limitrange_validate_spec(const limitrange_spec_t *spec, char *out_reason);

/**
 * Check if LimitRange can be created in namespace
 * @param namespace Namespace
 * @param spec LimitRange specification
 * @param out_reason Output reason if cannot create (caller allocated, >= 256)
 * @return 1 if can create, 0 if cannot
 */
int limitrange_can_create(const char *namespace, const limitrange_spec_t *spec, char *out_reason);

/* ============================================================================
 * Helper Functions
 * ========================================================================== */

/**
 * Convert resource bytes to human-readable format
 * @param bytes Resource bytes
 * @param out_str Output string (caller allocated, >= 32)
 * @return 0 on success
 */
int limitrange_format_resource(uint64_t bytes, char *out_str);

/**
 * Convert human-readable resource to bytes
 * @param resource_str String like "1Gi", "500m", "100Mi"
 * @param out_bytes Output bytes
 * @return 0 on success, -1 if invalid format
 */
int limitrange_parse_resource(const char *resource_str, uint64_t *out_bytes);

#endif /* SIRAH_LIMITRANGE_H */
