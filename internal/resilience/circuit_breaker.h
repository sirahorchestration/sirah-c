/*
 * circuit_breaker.h
 * 
 * 3-state circuit breaker pattern for API communication resilience
 * 
 * States:
 *   CLOSED: Normal operation - requests pass through
 *   OPEN: Failure threshold reached - requests fail immediately (fail-fast)
 *   HALF_OPEN: Testing recovery - allows limited requests to test if service recovered
 * 
 * Kubernetes v1.28 Conformance:
 *   - Used for API server → etcd communication resilience
 *   - Used for controller → API server communication
 *   - Implements standard circuit breaker pattern with exponential backoff
 * 
 * References:
 *   - https://en.wikipedia.org/wiki/Circuit_breaker_pattern
 *   - https://kubernetes.io/docs/concepts/architecture/
 */

#ifndef SIRAH_CIRCUIT_BREAKER_H
#define SIRAH_CIRCUIT_BREAKER_H

#include <time.h>
#include <pthread.h>
#include <stdbool.h>

/**
 * Circuit breaker states
 */
typedef enum {
    CB_STATE_CLOSED = 0,      // Normal operation
    CB_STATE_OPEN = 1,        // Failing - reject requests
    CB_STATE_HALF_OPEN = 2    // Testing recovery
} circuit_breaker_state_t;

/**
 * Failure metrics for monitoring
 */
typedef struct {
    uint32_t total_requests;      // Total requests processed
    uint32_t total_failures;      // Total failures seen
    uint32_t consecutive_failures;// Current consecutive failure count
    uint32_t last_error_code;     // Last HTTP error code (0 = no error)
    time_t   last_failure_time;   // Timestamp of last failure
    time_t   last_state_change;   // Timestamp of last state transition
} circuit_breaker_metrics_t;

/**
 * Circuit breaker configuration
 */
typedef struct {
    uint32_t failure_threshold;        // Failures to trigger OPEN (e.g., 5)
    uint32_t success_threshold;        // Successes in HALF_OPEN to close (e.g., 2)
    uint32_t timeout_seconds;          // Time in OPEN before HALF_OPEN (e.g., 30)
    uint32_t max_half_open_requests;   // Max concurrent requests in HALF_OPEN (e.g., 3)
    bool     use_exponential_backoff;  // Enable exponential backoff
    uint32_t initial_backoff_ms;       // Initial backoff in ms (e.g., 100)
    uint32_t max_backoff_ms;           // Max backoff in ms (e.g., 32000)
} circuit_breaker_config_t;

/**
 * Circuit breaker instance
 */
typedef struct {
    circuit_breaker_state_t state;
    circuit_breaker_config_t config;
    circuit_breaker_metrics_t metrics;
    pthread_mutex_t mutex;
    
    // Backoff management
    uint32_t current_backoff_ms;
    time_t   next_retry_time;
    
    // Half-open state management
    uint32_t half_open_requests;
    uint32_t half_open_successes;
} circuit_breaker_t;

/**
 * Create a new circuit breaker instance
 * 
 * @param config Circuit breaker configuration
 * @return Allocated circuit breaker, or NULL on error
 */
circuit_breaker_t* circuit_breaker_new(const circuit_breaker_config_t *config);

/**
 * Free circuit breaker instance
 */
void circuit_breaker_free(circuit_breaker_t *cb);

/**
 * Get current state of circuit breaker
 */
circuit_breaker_state_t circuit_breaker_get_state(circuit_breaker_t *cb);

/**
 * Record a successful request
 * 
 * May transition from HALF_OPEN → CLOSED if success threshold reached
 * Resets failure counter in CLOSED state
 * 
 * @param cb Circuit breaker instance
 */
void circuit_breaker_record_success(circuit_breaker_t *cb);

/**
 * Record a failed request
 * 
 * May transition from CLOSED → OPEN if failure threshold reached
 * In HALF_OPEN, any failure transitions back to OPEN with increased backoff
 * 
 * @param cb Circuit breaker instance
 * @param error_code HTTP error code (e.g., 500)
 */
void circuit_breaker_record_failure(circuit_breaker_t *cb, uint32_t error_code);

/**
 * Check if request should be allowed through circuit
 * 
 * Returns:
 *   - true: Request allowed (in CLOSED or HALF_OPEN with available slots)
 *   - false: Request rejected (in OPEN or HALF_OPEN saturated)
 * 
 * May auto-transition from OPEN → HALF_OPEN if timeout expired
 * 
 * @param cb Circuit breaker instance
 * @return true if request allowed
 */
bool circuit_breaker_allow_request(circuit_breaker_t *cb);

/**
 * Get time until next retry is allowed (for OPEN state)
 * 
 * Returns milliseconds until retry is allowed
 * Returns 0 if immediately allowed
 * 
 * @param cb Circuit breaker instance
 * @return Milliseconds until retry allowed
 */
uint32_t circuit_breaker_get_backoff_ms(circuit_breaker_t *cb);

/**
 * Get current metrics
 * 
 * @param cb Circuit breaker instance
 * @param out_metrics Output metrics struct
 */
void circuit_breaker_get_metrics(circuit_breaker_t *cb, circuit_breaker_metrics_t *out_metrics);

/**
 * Reset circuit breaker to CLOSED state
 * 
 * Clears all metrics and failure counters
 * Used for testing and recovery
 */
void circuit_breaker_reset(circuit_breaker_t *cb);

/**
 * Get human-readable state name
 */
const char* circuit_breaker_state_name(circuit_breaker_state_t state);

#endif // SIRAH_CIRCUIT_BREAKER_H
