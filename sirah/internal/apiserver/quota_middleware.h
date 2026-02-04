/**
 * ResourceQuota Admission Control Middleware
 * Enforces resource quotas before allowing object creation
 */

#ifndef QUOTA_MIDDLEWARE_H
#define QUOTA_MIDDLEWARE_H

#include "../controller/resource_quota.h"

/**
 * Extract namespace from HTTP request context
 * Returns: 0 on success, -1 on error
 */
int quota_middleware_get_namespace(const char* namespace_str,
                                  char* namespace_out);

/**
 * Check if pod can be created within quota
 * Parses CPU and memory from request, checks against quota
 * Returns: 0 if allowed, -1 if denied, 1 if no quota
 */
int quota_middleware_check_pod_creation(const char* namespace,
                                       const char* pod_spec_json,
                                       quota_admission_result_t* result_out);

/**
 * Check if service can be created within quota
 * Returns: 0 if allowed, -1 if denied, 1 if no quota
 */
int quota_middleware_check_service_creation(const char* namespace,
                                           quota_admission_result_t* result_out);

/**
 * Check if ConfigMap can be created within quota
 * Returns: 0 if allowed, -1 if denied, 1 if no quota
 */
int quota_middleware_check_configmap_creation(const char* namespace,
                                             quota_admission_result_t* result_out);

/**
 * Check if Secret can be created within quota
 * Returns: 0 if allowed, -1 if denied, 1 if no quota
 */
int quota_middleware_check_secret_creation(const char* namespace,
                                          quota_admission_result_t* result_out);

/**
 * Format HTTP 429 (Too Many Requests) error response for quota exceeded
 * Returns: 0 on success, -1 on error
 */
int quota_middleware_format_denied(const quota_admission_result_t* result,
                                  char* response_buffer,
                                  int* response_code);

#endif /* QUOTA_MIDDLEWARE_H */
