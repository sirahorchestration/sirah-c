/**
 * ResourceQuota Manager - Kubernetes v1.28 Quota Enforcement
 * 
 * Implements namespace-scoped resource quota enforcement with:
 * - CPU and memory quota per namespace
 * - Pod count quotas
 * - Service count quotas
 * - Real-time usage tracking
 * - Admission controller integration
 * - Status reporting
 */

#ifndef SIRAH_RESOURCE_QUOTA_H
#define SIRAH_RESOURCE_QUOTA_H

#include <pthread.h>
#include <time.h>

/* ============================================================================
   Resource Quota Types
   ============================================================================ */

/**
 * Resource quantity in millicores/bytes
 * For CPU: 1000 millicores = 1 core
 * For memory: 1 byte unit
 */
typedef struct {
    long long value;  /* Quantity in smallest unit (millicores or bytes) */
    char unit[32];    /* Unit string: "m", "Mi", "Gi", etc */
} resource_quantity_t;

/**
 * Hard limits for a ResourceQuota
 * Defines maximum allowed resource consumption in a namespace
 */
typedef struct {
    long long cpu_millicores;      /* Max total CPU (millicores) */
    long long memory_bytes;         /* Max total memory (bytes) */
    int pods;                       /* Max pod count */
    int services;                   /* Max service count */
    int configmaps;                 /* Max ConfigMap count */
    int secrets;                    /* Max Secret count */
} quota_limits_t;

/**
 * Current resource usage in a namespace
 * Tracks actual consumption against hard limits
 */
typedef struct {
    long long cpu_used_millicores;
    long long memory_used_bytes;
    int pods_used;
    int services_used;
    int configmaps_used;
    int secrets_used;
} quota_usage_t;

/**
 * ResourceQuota object (namespace-scoped)
 * Defines and tracks resource quotas for a namespace
 */
typedef struct {
    char namespace[64];
    char name[256];
    
    quota_limits_t hard;   /* Hard limits */
    quota_usage_t used;    /* Current usage */
    
    time_t created_at;
    time_t last_updated;
    
    char created_by[256];  /* User who created this quota */
} resource_quota_t;

/**
 * Result of quota admission check
 * Indicates whether a new resource can be admitted
 */
typedef enum {
    QUOTA_ALLOWED = 0,     /* Resource creation allowed */
    QUOTA_EXCEEDED = 1,    /* Would exceed quota limit */
    QUOTA_NOT_FOUND = 2,   /* No quota defined for namespace */
    QUOTA_ERROR = 3        /* Error checking quota */
} quota_decision_t;

/**
 * Detailed admission result
 */
typedef struct {
    quota_decision_t decision;
    char reason[512];
    
    /* Which resource would be exceeded */
    char exceeded_resource[64];
    long long requested;
    long long limit;
    long long current;
} quota_admission_result_t;

/**
 * Status of a ResourceQuota
 * Returned when querying quota status
 */
typedef struct {
    resource_quota_t spec;
    quota_usage_t status;
    time_t status_updated_at;
} resource_quota_status_t;

/* ============================================================================
   ResourceQuota Manager API
   ============================================================================ */

/**
 * Initialize ResourceQuota manager
 * Sets up storage and synchronization primitives
 * 
 * @return 0 on success, -1 on failure
 */
int resource_quota_manager_init(void);

/**
 * Shutdown ResourceQuota manager
 * Cleans up resources and finalizes quotas
 * 
 * @return 0 on success, -1 on failure
 */
int resource_quota_manager_shutdown(void);

/* ============================================================================
   ResourceQuota CRUD Operations
   ============================================================================ */

/**
 * Create a new ResourceQuota in a namespace
 * Defines quota limits for the namespace
 * 
 * @param quota - Quota definition with hard limits
 * @return 0 on success, -1 on error (quota exists, invalid, etc)
 */
int resource_quota_create(const resource_quota_t* quota);

/**
 * Get a ResourceQuota by name
 * 
 * @param namespace - Target namespace
 * @param name - Quota name
 * @param quota_out - OUT: Quota object
 * @return 0 on success, -1 on not found/error
 */
int resource_quota_get(const char* namespace, const char* name,
                      resource_quota_t* quota_out);

/**
 * Update a ResourceQuota (hard limits only)
 * Modifies the quota limits in place
 * 
 * @param quota - Updated quota object (namespace and name must match existing)
 * @return 0 on success, -1 on not found/error
 */
int resource_quota_update(const resource_quota_t* quota);

/**
 * Delete a ResourceQuota
 * Removes quota enforcement for the namespace
 * 
 * @param namespace - Target namespace
 * @param name - Quota name
 * @return 0 on success, -1 on not found/error
 */
int resource_quota_delete(const char* namespace, const char* name);

/**
 * List all ResourceQuotas in a namespace
 * 
 * @param namespace - Target namespace
 * @param quotas_out - OUT: Array of quotas (up to 100)
 * @param count_out - OUT: Number of quotas returned
 * @return 0 on success, -1 on error
 */
int resource_quota_list(const char* namespace,
                       resource_quota_t* quotas_out,
                       int* count_out);

/* ============================================================================
   Quota Usage Tracking
   ============================================================================ */

/**
 * Update usage after pod creation
 * Increases usage counters when a pod is created
 * 
 * @param namespace - Pod namespace
 * @param cpu_millicores - Pod CPU request
 * @param memory_bytes - Pod memory request
 * @return 0 on success, -1 on error
 */
int resource_quota_add_pod(const char* namespace,
                          long long cpu_millicores,
                          long long memory_bytes);

/**
 * Update usage after pod deletion
 * Decreases usage counters when a pod is deleted
 * 
 * @param namespace - Pod namespace
 * @param cpu_millicores - Pod CPU request
 * @param memory_bytes - Pod memory request
 * @return 0 on success, -1 on error
 */
int resource_quota_remove_pod(const char* namespace,
                             long long cpu_millicores,
                             long long memory_bytes);

/**
 * Update usage after service creation
 * Increases service count
 * 
 * @param namespace - Service namespace
 * @return 0 on success, -1 on error
 */
int resource_quota_add_service(const char* namespace);

/**
 * Update usage after service deletion
 * Decreases service count
 * 
 * @param namespace - Service namespace
 * @return 0 on success, -1 on error
 */
int resource_quota_remove_service(const char* namespace);

/**
 * Update usage after ConfigMap creation
 * Increases ConfigMap count
 * 
 * @param namespace - ConfigMap namespace
 * @return 0 on success, -1 on error
 */
int resource_quota_add_configmap(const char* namespace);

/**
 * Update usage after ConfigMap deletion
 * Decreases ConfigMap count
 * 
 * @param namespace - ConfigMap namespace
 * @return 0 on success, -1 on error
 */
int resource_quota_remove_configmap(const char* namespace);

/**
 * Update usage after Secret creation
 * Increases Secret count
 * 
 * @param namespace - Secret namespace
 * @return 0 on success, -1 on error
 */
int resource_quota_add_secret(const char* namespace);

/**
 * Update usage after Secret deletion
 * Decreases Secret count
 * 
 * @param namespace - Secret namespace
 * @return 0 on success, -1 on error
 */
int resource_quota_remove_secret(const char* namespace);

/* ============================================================================
   Admission Control
   ============================================================================ */

/**
 * Check if a pod can be created in a namespace
 * Main admission check for pod creation
 * 
 * @param namespace - Target namespace
 * @param cpu_millicores - Requested CPU (millicores)
 * @param memory_bytes - Requested memory (bytes)
 * @param result_out - OUT: Detailed admission result
 * @return 0 if allowed, -1 if denied, 1 if no quota
 */
int resource_quota_can_create_pod(const char* namespace,
                                 long long cpu_millicores,
                                 long long memory_bytes,
                                 quota_admission_result_t* result_out);

/**
 * Check if a service can be created in a namespace
 * 
 * @param namespace - Target namespace
 * @param result_out - OUT: Detailed admission result
 * @return 0 if allowed, -1 if denied, 1 if no quota
 */
int resource_quota_can_create_service(const char* namespace,
                                     quota_admission_result_t* result_out);

/**
 * Check if a ConfigMap can be created in a namespace
 * 
 * @param namespace - Target namespace
 * @param result_out - OUT: Detailed admission result
 * @return 0 if allowed, -1 if denied, 1 if no quota
 */
int resource_quota_can_create_configmap(const char* namespace,
                                       quota_admission_result_t* result_out);

/**
 * Check if a Secret can be created in a namespace
 * 
 * @param namespace - Target namespace
 * @param result_out - OUT: Detailed admission result
 * @return 0 if allowed, -1 if denied, 1 if no quota
 */
int resource_quota_can_create_secret(const char* namespace,
                                    quota_admission_result_t* result_out);

/* ============================================================================
   Status & Reporting
   ============================================================================ */

/**
 * Get current status of a ResourceQuota
 * Returns quota limits and current usage
 * 
 * @param namespace - Target namespace
 * @param name - Quota name
 * @param status_out - OUT: Quota status
 * @return 0 on success, -1 on not found/error
 */
int resource_quota_get_status(const char* namespace,
                             const char* name,
                             resource_quota_status_t* status_out);

/**
 * Get current usage for a namespace
 * Useful for monitoring and reporting
 * 
 * @param namespace - Target namespace
 * @param usage_out - OUT: Current usage
 * @return 0 on success, -1 on error
 */
int resource_quota_get_usage(const char* namespace,
                            quota_usage_t* usage_out);

/**
 * Format quota exceeded error for HTTP response
 * Creates Kubernetes-standard 429 Too Many Requests response
 * 
 * @param result - Admission result with denial reason
 * @param response_buffer - OUT: JSON error response
 * @param response_code - OUT: HTTP status code (429)
 * @return 0 on success
 */
int resource_quota_format_quota_exceeded(const quota_admission_result_t* result,
                                        char* response_buffer,
                                        int* response_code);

/* ============================================================================
   Constants & Limits
   ============================================================================ */

#define MAX_RESOURCE_QUOTAS 500       /* Max quotas per cluster */
#define MAX_QUOTA_NAME 256
#define MAX_NAMESPACE_NAME 64

/* Standard resource limits */
#define DEFAULT_QUOTA_CPU_MILLICORES 4000      /* 4 CPU */
#define DEFAULT_QUOTA_MEMORY_BYTES (8589934592LL)  /* 8 GB */
#define DEFAULT_QUOTA_PODS 100
#define DEFAULT_QUOTA_SERVICES 10

#endif /* SIRAH_RESOURCE_QUOTA_H */
