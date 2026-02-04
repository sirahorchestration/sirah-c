/*
 * StatefulSet API Header - Kubernetes v1.28
 * 
 * Manages StatefulSet resources with ordered pod creation/deletion,
 * persistent volume claims per pod, pod identity stability,
 * and headless service integration.
 *
 * Key Features:
 * - Pod ordinal naming scheme (pod-0, pod-1, pod-2, ...)
 * - Ordered pod creation and deletion
 * - Per-pod persistent volume claims (volumeClaimTemplates)
 * - Headless service for stable DNS
 * - Rolling update strategy
 * - Pod affinity and anti-affinity support
 * - Status tracking with replica counts
 *
 * Thread Safety: All operations protected by mutex lock
 * Max Resources: 500 StatefulSets per cluster
 */

#ifndef SIRAH_STATEFULSET_H
#define SIRAH_STATEFULSET_H

#include <pthread.h>
#include <time.h>

/* StatefulSet Pod Management Strategies */
typedef enum {
    STATEFULSET_STRATEGY_ORDERED_READY,    /* Create pods in order, wait for Ready */
    STATEFULSET_STRATEGY_PARALLEL,          /* Create all pods in parallel */
    STATEFULSET_STRATEGY_ROLLING_UPDATE     /* Rolling update with replicas */
} statefulset_pod_management_policy_t;

/* StatefulSet Update Strategy */
typedef enum {
    STATEFULSET_UPDATE_ROLLING,             /* Rolling update (default) */
    STATEFULSET_UPDATE_ON_DELETE,           /* Only update when pod deleted */
    STATEFULSET_UPDATE_PARTITION            /* Partition-based canary updates */
} statefulset_update_strategy_t;

/* Pod Ordinal State */
typedef enum {
    ORDINAL_STATE_PENDING,                  /* Waiting to be created */
    ORDINAL_STATE_CREATING,                 /* Pod creation in progress */
    ORDINAL_STATE_RUNNING,                  /* Pod is running */
    ORDINAL_STATE_READY,                    /* Pod is ready */
    ORDINAL_STATE_TERMINATING,              /* Pod deletion in progress */
    ORDINAL_STATE_TERMINATED,               /* Pod deleted */
    ORDINAL_STATE_FAILED                    /* Pod failed unexpectedly */
} pod_ordinal_state_t;

/* Pod Ordinal Tracking */
typedef struct {
    int32_t ordinal;                        /* Pod ordinal (0-indexed) */
    char pod_name[256];                     /* Stable pod name: statefulset-{ordinal} */
    pod_ordinal_state_t state;              /* Current ordinal state */
    time_t created_at;                      /* When pod was created */
    int32_t restart_count;                  /* Container restart count */
    int ready;                              /* 1 if pod is ready, 0 otherwise */
    char pvc_names[10][256];                /* Volume claim names for this pod */
    int pvc_count;                          /* Number of PVCs */
} pod_ordinal_t;

/* Volume Claim Template */
typedef struct {
    char name[256];                         /* PVC name suffix (e.g., "data") */
    char storage_class_name[256];           /* StorageClass for dynamic provisioning */
    uint64_t storage_bytes;                 /* Requested storage capacity */
    int access_modes;                       /* Bitmask: 1=RWO, 2=ROX, 4=RWX */
} volume_claim_template_t;

/* Selector for Pod Matching */
typedef struct {
    char key[128];                          /* Label key */
    char value[256];                        /* Label value */
} label_selector_t;

/* StatefulSet Specification */
typedef struct {
    char name[256];                         /* StatefulSet name */
    char namespace[128];                    /* Namespace */
    int32_t replicas;                       /* Desired pod count (0-1000) */
    int32_t partition;                      /* Update partition for canary (rolling) */
    char service_name[256];                 /* Headless Service name (REQUIRED) */
    char pod_template_name[256];            /* Pod template name */
    statefulset_pod_management_policy_t pod_management_policy;  /* Creation strategy */
    statefulset_update_strategy_t update_strategy;              /* Update strategy */
    int32_t revision_history_limit;         /* Keep last N revisions (default 10) */
    int32_t termination_grace_period_seconds;  /* Grace period for termination (default 30) */
    
    /* Pod template */
    char container_image[512];              /* Container image for pods */
    int32_t container_port;                 /* Port pods listen on */
    
    /* Volume claim templates */
    volume_claim_template_t volume_templates[10];
    int volume_template_count;
    
    /* Pod selectors */
    label_selector_t selectors[20];
    int selector_count;
    
    /* Timestamps */
    time_t created_at;
    time_t updated_at;
    uint32_t generation;                    /* Kubernetes generation counter */
} statefulset_spec_t;

/* StatefulSet Status */
typedef struct {
    int32_t replicas;                       /* Current pod count */
    int32_t ready_replicas;                 /* Pods in Ready state */
    int32_t current_replicas;               /* Pods with current spec */
    int32_t updated_replicas;               /* Pods updated during rolling update */
    
    uint32_t current_revision;              /* Current pod spec revision */
    uint32_t update_revision;               /* Revision being rolled out */
    
    time_t last_update_time;                /* When last update started */
    int update_in_progress;                 /* 1 if rolling update in progress */
    
    /* Pod ordinals and their states */
    pod_ordinal_t pod_ordinals[1000];       /* Track all ordinals up to max replicas */
    int32_t max_ordinal;                    /* Highest ordinal created */
    int32_t ready_ordinal;                  /* Highest ready ordinal */
    
    /* Conditions */
    char conditions[5][256];                /* Status conditions */
    int condition_count;
} statefulset_status_t;

/* StatefulSet Object */
typedef struct {
    statefulset_spec_t spec;
    statefulset_status_t status;
} statefulset_t;

/* StatefulSet Manager */
typedef struct {
    pthread_mutex_t lock;
    int initialized;
    statefulset_t statefulsets[500];        /* Max 500 StatefulSets */
    int count;
} statefulset_manager_t;

/* ============================================================================
 * Lifecycle Management
 * ========================================================================== */

/**
 * Initialize the StatefulSet manager
 * @return 0 on success, -1 on error
 */
int statefulset_manager_init(void);

/**
 * Shutdown the StatefulSet manager
 * @return 0 on success, -1 on error
 */
int statefulset_manager_shutdown(void);

/* ============================================================================
 * CRUD Operations (Namespace-Scoped)
 * ========================================================================== */

/**
 * Create a new StatefulSet
 * @param namespace StatefulSet namespace
 * @param name StatefulSet name
 * @param spec StatefulSet specification
 * @return 0 on success, -1 on duplicate/error, -2 if invalid spec
 */
int statefulset_create(const char *namespace, const char *name, const statefulset_spec_t *spec);

/**
 * Get a StatefulSet by name
 * @param namespace StatefulSet namespace
 * @param name StatefulSet name
 * @param out_spec Pointer to store StatefulSet data
 * @return 0 on success, -1 if not found
 */
int statefulset_get(const char *namespace, const char *name, statefulset_t *out_statefulset);

/**
 * Update a StatefulSet
 * @param namespace StatefulSet namespace
 * @param name StatefulSet name
 * @param spec New specification (preserves created_at, updates updated_at)
 * @return 0 on success, -1 if not found, -2 if invalid
 */
int statefulset_update(const char *namespace, const char *name, const statefulset_spec_t *spec);

/**
 * Delete a StatefulSet (and cascade delete pods)
 * @param namespace StatefulSet namespace
 * @param name StatefulSet name
 * @param propagation_policy DeletePropagation (0=Foreground, 1=Background, 2=Orphan)
 * @return 0 on success, -1 if not found
 */
int statefulset_delete(const char *namespace, const char *name, int propagation_policy);

/**
 * List all StatefulSets in namespace
 * @param namespace Namespace filter (NULL for all)
 * @param out_sets Output array (caller allocated, size >= 500)
 * @return Number of StatefulSets listed, -1 on error
 */
int statefulset_list(const char *namespace, statefulset_t *out_sets);

/**
 * List count of StatefulSets
 * @param namespace Namespace filter (NULL for all)
 * @return Count of StatefulSets
 */
int statefulset_list_count(const char *namespace);

/* ============================================================================
 * Pod Ordinal Management
 * ========================================================================== */

/**
 * Get pod name for ordinal
 * @param statefulset_name StatefulSet name
 * @param ordinal Pod ordinal (0-indexed)
 * @param out_name Output pod name buffer (caller allocated, >= 256 bytes)
 * @return 0 on success, -1 on error
 */
int statefulset_get_pod_name(const char *statefulset_name, int32_t ordinal, char *out_name);

/**
 * Get ordinal from pod name
 * @param pod_name Pod name (e.g., "mysql-0")
 * @param out_ordinal Pointer to store ordinal
 * @return 0 on success, -1 if invalid format
 */
int statefulset_extract_ordinal(const char *pod_name, int32_t *out_ordinal);

/**
 * Get pod ordinal state
 * @param namespace StatefulSet namespace
 * @param statefulset_name StatefulSet name
 * @param ordinal Pod ordinal
 * @param out_state Pointer to store pod ordinal state
 * @return 0 on success, -1 if not found
 */
int statefulset_get_ordinal_state(const char *namespace, const char *statefulset_name, 
                                   int32_t ordinal, pod_ordinal_t *out_state);

/**
 * Set pod ordinal state
 * @param namespace StatefulSet namespace
 * @param statefulset_name StatefulSet name
 * @param ordinal Pod ordinal
 * @param state New state
 * @return 0 on success, -1 if not found
 */
int statefulset_set_ordinal_state(const char *namespace, const char *statefulset_name,
                                   int32_t ordinal, pod_ordinal_state_t state);

/**
 * Get highest ready ordinal (for ordered pod management)
 * @param namespace StatefulSet namespace
 * @param statefulset_name StatefulSet name
 * @param out_ordinal Pointer to store ordinal (-1 if none ready)
 * @return 0 on success, -1 if StatefulSet not found
 */
int statefulset_get_ready_ordinal(const char *namespace, const char *statefulset_name,
                                   int32_t *out_ordinal);

/* ============================================================================
 * Volume Claim Template Management
 * ========================================================================== */

/**
 * Generate PVC name for pod ordinal
 * @param statefulset_name StatefulSet name
 * @param template_name Volume claim template name (e.g., "data")
 * @param ordinal Pod ordinal
 * @param out_pvc_name Output PVC name (caller allocated, >= 256)
 * @return 0 on success, -1 on error
 */
int statefulset_generate_pvc_name(const char *statefulset_name, const char *template_name,
                                   int32_t ordinal, char *out_pvc_name);

/**
 * Get volume claim templates for StatefulSet
 * @param namespace StatefulSet namespace
 * @param statefulset_name StatefulSet name
 * @param out_templates Output template array (caller allocated)
 * @param max_templates Maximum templates to return
 * @return Number of templates returned, -1 on error
 */
int statefulset_get_volume_templates(const char *namespace, const char *statefulset_name,
                                      volume_claim_template_t *out_templates, int max_templates);

/**
 * Create PVCs for pod ordinal based on volume claim templates
 * @param namespace StatefulSet namespace
 * @param statefulset_name StatefulSet name
 * @param ordinal Pod ordinal
 * @return Number of PVCs created, -1 on error
 */
int statefulset_create_pvcs_for_ordinal(const char *namespace, const char *statefulset_name,
                                         int32_t ordinal);

/**
 * Delete PVCs for pod ordinal
 * @param namespace StatefulSet namespace
 * @param statefulset_name StatefulSet name
 * @param ordinal Pod ordinal
 * @return Number of PVCs deleted, -1 on error
 */
int statefulset_delete_pvcs_for_ordinal(const char *namespace, const char *statefulset_name,
                                         int32_t ordinal);

/* ============================================================================
 * Pod Lifecycle & Ordering
 * ========================================================================== */

/**
 * Get next ordinal to create (respects pod management policy)
 * @param namespace StatefulSet namespace
 * @param statefulset_name StatefulSet name
 * @param out_ordinal Pointer to store next ordinal (-1 if none to create)
 * @return 0 on success, -1 if StatefulSet not found
 */
int statefulset_get_next_ordinal_to_create(const char *namespace, const char *statefulset_name,
                                            int32_t *out_ordinal);

/**
 * Get next ordinal to delete (reverse order for graceful shutdown)
 * @param namespace StatefulSet namespace
 * @param statefulset_name StatefulSet name
 * @param out_ordinal Pointer to store next ordinal (-1 if none to delete)
 * @return 0 on success, -1 if StatefulSet not found
 */
int statefulset_get_next_ordinal_to_delete(const char *namespace, const char *statefulset_name,
                                            int32_t *out_ordinal);

/**
 * Check if pod is ready (blocks further pod creation in OrderedReady mode)
 * @param namespace StatefulSet namespace
 * @param statefulset_name StatefulSet name
 * @param ordinal Pod ordinal
 * @return 1 if ready, 0 if not ready
 */
int statefulset_is_ordinal_ready(const char *namespace, const char *statefulset_name,
                                  int32_t ordinal);

/**
 * Update pod ready status for ordinal
 * @param namespace StatefulSet namespace
 * @param statefulset_name StatefulSet name
 * @param ordinal Pod ordinal
 * @param ready 1 if ready, 0 if not
 * @return 0 on success, -1 if not found
 */
int statefulset_set_ordinal_ready(const char *namespace, const char *statefulset_name,
                                   int32_t ordinal, int ready);

/* ============================================================================
 * Status Management
 * ========================================================================== */

/**
 * Update replica counts in status
 * @param namespace StatefulSet namespace
 * @param statefulset_name StatefulSet name
 * @param replicas Current pod count
 * @param ready_replicas Ready pod count
 * @param updated_replicas Updated pod count
 * @return 0 on success, -1 if not found
 */
int statefulset_update_status(const char *namespace, const char *statefulset_name,
                               int32_t replicas, int32_t ready_replicas, int32_t updated_replicas);

/**
 * Get StatefulSet status
 * @param namespace StatefulSet namespace
 * @param statefulset_name StatefulSet name
 * @param out_status Pointer to store status
 * @return 0 on success, -1 if not found
 */
int statefulset_get_status(const char *namespace, const char *statefulset_name,
                            statefulset_status_t *out_status);

/**
 * Check if StatefulSet is fully rolled out
 * @param namespace StatefulSet namespace
 * @param statefulset_name StatefulSet name
 * @return 1 if all replicas ready with current spec, 0 otherwise
 */
int statefulset_is_rolled_out(const char *namespace, const char *statefulset_name);

/**
 * Get JSON status for StatefulSet
 * @param namespace StatefulSet namespace
 * @param statefulset_name StatefulSet name
 * @param out_json Output JSON string (caller allocated, >= 2048)
 * @return 0 on success, -1 if not found
 */
int statefulset_get_status_json(const char *namespace, const char *statefulset_name, char *out_json);

/* ============================================================================
 * Validation & Admission Control
 * ========================================================================== */

/**
 * Validate StatefulSet specification
 * @param spec StatefulSet specification to validate
 * @param out_reason Output reason if invalid (caller allocated, >= 256)
 * @return 1 if valid, 0 if invalid
 */
int statefulset_validate_spec(const statefulset_spec_t *spec, char *out_reason);

/**
 * Check if StatefulSet can be created
 * @param namespace StatefulSet namespace
 * @param spec StatefulSet specification
 * @param out_reason Output reason if cannot create (caller allocated, >= 256)
 * @return 1 if can create, 0 if cannot
 */
int statefulset_can_create(const char *namespace, const statefulset_spec_t *spec, char *out_reason);

/* ============================================================================
 * Headless Service Integration
 * ========================================================================== */

/**
 * Get headless service name (required for pod DNS)
 * @param namespace StatefulSet namespace
 * @param statefulset_name StatefulSet name
 * @param out_service_name Output service name (caller allocated, >= 256)
 * @return 0 on success, -1 if not found
 */
int statefulset_get_service_name(const char *namespace, const char *statefulset_name,
                                  char *out_service_name);

/**
 * Verify headless service exists (validation)
 * @param namespace Service namespace
 * @param service_name Service name
 * @return 1 if service exists, 0 if not
 */
int statefulset_verify_headless_service(const char *namespace, const char *service_name);

/* ============================================================================
 * Rolling Update Management
 * ========================================================================== */

/**
 * Get current spec revision number
 * @param namespace StatefulSet namespace
 * @param statefulset_name StatefulSet name
 * @param out_revision Pointer to store revision
 * @return 0 on success, -1 if not found
 */
int statefulset_get_current_revision(const char *namespace, const char *statefulset_name,
                                      uint32_t *out_revision);

/**
 * Start rolling update (increments generation)
 * @param namespace StatefulSet namespace
 * @param statefulset_name StatefulSet name
 * @return 0 on success, -1 if not found
 */
int statefulset_start_rolling_update(const char *namespace, const char *statefulset_name);

/**
 * Set partition for canary update (only update ordinals >= partition)
 * @param namespace StatefulSet namespace
 * @param statefulset_name StatefulSet name
 * @param partition Partition value
 * @return 0 on success, -1 if not found
 */
int statefulset_set_update_partition(const char *namespace, const char *statefulset_name,
                                      int32_t partition);

/**
 * Check if ordinal needs update
 * @param namespace StatefulSet namespace
 * @param statefulset_name StatefulSet name
 * @param ordinal Pod ordinal
 * @return 1 if needs update, 0 if current
 */
int statefulset_ordinal_needs_update(const char *namespace, const char *statefulset_name,
                                      int32_t ordinal);

#endif /* SIRAH_STATEFULSET_H */
