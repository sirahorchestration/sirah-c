// internal/apiserver/endpoints_controllers.h
// HTTP endpoint handlers for Phase 5 controllers
// StatefulSet, Job, and other workload resource endpoints

#ifndef SIRAH_ENDPOINTS_CONTROLLERS_H
#define SIRAH_ENDPOINTS_CONTROLLERS_H

// ============================================================================
// StatefulSet Endpoints
// ============================================================================

/**
 * POST /apis/apps/v1/namespaces/{namespace}/statefulsets
 * Create a StatefulSet
 */
int endpoint_create_statefulset(const char* namespace, const char* body,
                                char* response_buffer, int* response_code);

/**
 * GET /apis/apps/v1/namespaces/{namespace}/statefulsets/{name}
 * Retrieve a StatefulSet
 */
int endpoint_get_statefulset(const char* namespace, const char* name,
                             char* response_buffer, int* response_code);

/**
 * GET /apis/apps/v1/namespaces/{namespace}/statefulsets
 * List StatefulSets in namespace
 */
int endpoint_list_statefulsets(const char* namespace, char* response_buffer, int* response_code);

/**
 * PATCH /apis/apps/v1/namespaces/{namespace}/statefulsets/{name}
 * Update StatefulSet (CAS semantics)
 */
int endpoint_patch_statefulset(const char* namespace, const char* name, const char* body,
                               const char* content_type, char* response_buffer, int* response_code);

/**
 * DELETE /apis/apps/v1/namespaces/{namespace}/statefulsets/{name}
 * Delete StatefulSet
 */
int endpoint_delete_statefulset(const char* namespace, const char* name,
                                char* response_buffer, int* response_code);

// ============================================================================
// Job Endpoints
// ============================================================================

/**
 * POST /apis/batch/v1/namespaces/{namespace}/jobs
 * Create a Job
 */
int endpoint_create_job(const char* namespace, const char* body,
                        char* response_buffer, int* response_code);

/**
 * GET /apis/batch/v1/namespaces/{namespace}/jobs/{name}
 * Retrieve a Job
 */
int endpoint_get_job(const char* namespace, const char* name,
                     char* response_buffer, int* response_code);

/**
 * GET /apis/batch/v1/namespaces/{namespace}/jobs
 * List Jobs in namespace
 */
int endpoint_list_jobs(const char* namespace, char* response_buffer, int* response_code);

/**
 * PATCH /apis/batch/v1/namespaces/{namespace}/jobs/{name}
 * Update Job (CAS semantics)
 */
int endpoint_patch_job(const char* namespace, const char* name, const char* body,
                       const char* content_type, char* response_buffer, int* response_code);

/**
 * DELETE /apis/batch/v1/namespaces/{namespace}/jobs/{name}
 * Delete Job
 */
int endpoint_delete_job(const char* namespace, const char* name,
                        char* response_buffer, int* response_code);

// ============================================================================
// LimitRange Endpoints
// ============================================================================

/**
 * POST /api/v1/namespaces/{namespace}/limitranges
 * Create a LimitRange
 */
int endpoint_create_limitrange(const char* namespace, const char* body,
                               char* response_buffer, int* response_code);

/**
 * GET /api/v1/namespaces/{namespace}/limitranges/{name}
 * Retrieve a LimitRange
 */
int endpoint_get_limitrange(const char* namespace, const char* name,
                            char* response_buffer, int* response_code);

/**
 * GET /api/v1/namespaces/{namespace}/limitranges
 * List LimitRanges in namespace
 */
int endpoint_list_limitranges(const char* namespace, char* response_buffer, int* response_code);

/**
 * PATCH /api/v1/namespaces/{namespace}/limitranges/{name}
 * Update LimitRange (CAS semantics)
 */
int endpoint_patch_limitrange(const char* namespace, const char* name, const char* body,
                              const char* content_type, char* response_buffer, int* response_code);

/**
 * DELETE /api/v1/namespaces/{namespace}/limitranges/{name}
 * Delete LimitRange
 */
int endpoint_delete_limitrange(const char* namespace, const char* name,
                               char* response_buffer, int* response_code);

#endif // SIRAH_ENDPOINTS_CONTROLLERS_H
