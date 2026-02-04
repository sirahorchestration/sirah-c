/*
 * circuit_breaker.c
 * 
 * Implementation of 3-state circuit breaker pattern
 */

#include "circuit_breaker.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/**
 * Create a new circuit breaker instance
 */
circuit_breaker_t* circuit_breaker_new(const circuit_breaker_config_t *config) {
    if (!config) {
        return NULL;
    }
    
    circuit_breaker_t *cb = (circuit_breaker_t *)malloc(sizeof(circuit_breaker_t));
    if (!cb) {
        return NULL;
    }
    
    cb->state = CB_STATE_CLOSED;
    cb->config = *config;
    memset(&cb->metrics, 0, sizeof(cb->metrics));
    
    pthread_mutex_init(&cb->mutex, NULL);
    
    cb->current_backoff_ms = config->initial_backoff_ms;
    cb->next_retry_time = 0;
    
    cb->half_open_requests = 0;
    cb->half_open_successes = 0;
    
    return cb;
}

/**
 * Free circuit breaker instance
 */
void circuit_breaker_free(circuit_breaker_t *cb) {
    if (!cb) {
        return;
    }
    
    pthread_mutex_destroy(&cb->mutex);
    free(cb);
}

/**
 * Get current state of circuit breaker
 */
circuit_breaker_state_t circuit_breaker_get_state(circuit_breaker_t *cb) {
    if (!cb) {
        return CB_STATE_CLOSED;
    }
    
    pthread_mutex_lock(&cb->mutex);
    circuit_breaker_state_t state = cb->state;
    pthread_mutex_unlock(&cb->mutex);
    
    return state;
}

/**
 * Calculate exponential backoff with jitter
 */
static uint32_t calculate_exponential_backoff(circuit_breaker_t *cb, uint32_t failures) {
    if (!cb->config.use_exponential_backoff) {
        return 0;  // No backoff
    }
    
    // exponential: initial * 2^failures, capped at max
    uint32_t backoff = cb->config.initial_backoff_ms;
    
    // Avoid overflow - cap at max_backoff
    for (uint32_t i = 0; i < failures && backoff < cb->config.max_backoff_ms; i++) {
        uint32_t next = backoff * 2;
        if (next > cb->config.max_backoff_ms) {
            backoff = cb->config.max_backoff_ms;
            break;
        }
        backoff = next;
    }
    
    // Add jitter: ±10%
    uint32_t jitter_percent = (rand() % 21) - 10;  // -10 to +10
    uint32_t jitter = (backoff * abs(jitter_percent)) / 100;
    
    if (jitter_percent >= 0) {
        backoff += jitter;
    } else {
        backoff = (backoff > jitter) ? (backoff - jitter) : 1;
    }
    
    // Cap at max
    if (backoff > cb->config.max_backoff_ms) {
        backoff = cb->config.max_backoff_ms;
    }
    
    return backoff;
}

/**
 * Record a successful request
 */
void circuit_breaker_record_success(circuit_breaker_t *cb) {
    if (!cb) {
        return;
    }
    
    pthread_mutex_lock(&cb->mutex);
    
    cb->metrics.total_requests++;
    cb->metrics.last_failure_time = 0;  // Clear last failure
    
    if (cb->state == CB_STATE_CLOSED) {
        // In CLOSED, reset failure counter
        cb->metrics.consecutive_failures = 0;
    } else if (cb->state == CB_STATE_HALF_OPEN) {
        // In HALF_OPEN, track successes
        cb->half_open_successes++;
        cb->half_open_requests--;
        
        // If success threshold reached, transition to CLOSED
        if (cb->half_open_successes >= cb->config.success_threshold) {
            cb->state = CB_STATE_CLOSED;
            cb->metrics.consecutive_failures = 0;
            cb->half_open_requests = 0;
            cb->half_open_successes = 0;
            cb->current_backoff_ms = cb->config.initial_backoff_ms;  // Reset backoff
            cb->metrics.last_state_change = time(NULL);
        }
    }
    
    pthread_mutex_unlock(&cb->mutex);
}

/**
 * Record a failed request
 */
void circuit_breaker_record_failure(circuit_breaker_t *cb, uint32_t error_code) {
    if (!cb) {
        return;
    }
    
    pthread_mutex_lock(&cb->mutex);
    
    cb->metrics.total_requests++;
    cb->metrics.total_failures++;
    cb->metrics.consecutive_failures++;
    cb->metrics.last_error_code = error_code;
    cb->metrics.last_failure_time = time(NULL);
    
    if (cb->state == CB_STATE_CLOSED) {
        // Check if failure threshold reached
        if (cb->metrics.consecutive_failures >= cb->config.failure_threshold) {
            cb->state = CB_STATE_OPEN;
            cb->current_backoff_ms = cb->config.initial_backoff_ms;
            cb->next_retry_time = time(NULL) + (cb->config.timeout_seconds);
            cb->metrics.last_state_change = time(NULL);
        }
    } else if (cb->state == CB_STATE_HALF_OPEN) {
        // In HALF_OPEN, any failure goes back to OPEN with backoff increase
        cb->state = CB_STATE_OPEN;
        
        // Increase backoff exponentially
        cb->current_backoff_ms = calculate_exponential_backoff(cb, cb->metrics.consecutive_failures);
        cb->next_retry_time = time(NULL) + (cb->current_backoff_ms / 1000);
        
        cb->half_open_requests = 0;
        cb->half_open_successes = 0;
        cb->metrics.last_state_change = time(NULL);
    }
    
    pthread_mutex_unlock(&cb->mutex);
}

/**
 * Check if request should be allowed through circuit
 */
bool circuit_breaker_allow_request(circuit_breaker_t *cb) {
    if (!cb) {
        return false;
    }
    
    pthread_mutex_lock(&cb->mutex);
    
    time_t now = time(NULL);
    bool allowed = false;
    
    if (cb->state == CB_STATE_CLOSED) {
        // CLOSED: Allow all requests
        allowed = true;
    } else if (cb->state == CB_STATE_OPEN) {
        // OPEN: Check if timeout expired
        if (now >= cb->next_retry_time) {
            // Transition to HALF_OPEN
            cb->state = CB_STATE_HALF_OPEN;
            cb->half_open_requests = 0;
            cb->half_open_successes = 0;
            allowed = true;
            cb->half_open_requests++;
            cb->metrics.last_state_change = time(NULL);
        } else {
            // Still in OPEN, reject
            allowed = false;
        }
    } else if (cb->state == CB_STATE_HALF_OPEN) {
        // HALF_OPEN: Allow limited requests
        if (cb->half_open_requests < cb->config.max_half_open_requests) {
            allowed = true;
            cb->half_open_requests++;
        } else {
            allowed = false;
        }
    }
    
    pthread_mutex_unlock(&cb->mutex);
    
    return allowed;
}

/**
 * Get time until next retry is allowed (for OPEN state)
 */
uint32_t circuit_breaker_get_backoff_ms(circuit_breaker_t *cb) {
    if (!cb) {
        return 0;
    }
    
    pthread_mutex_lock(&cb->mutex);
    
    if (cb->state != CB_STATE_OPEN) {
        pthread_mutex_unlock(&cb->mutex);
        return 0;
    }
    
    time_t now = time(NULL);
    if (now >= cb->next_retry_time) {
        pthread_mutex_unlock(&cb->mutex);
        return 0;
    }
    
    uint32_t remaining_ms = (uint32_t)((cb->next_retry_time - now) * 1000);
    
    pthread_mutex_unlock(&cb->mutex);
    
    return remaining_ms;
}

/**
 * Get current metrics
 */
void circuit_breaker_get_metrics(circuit_breaker_t *cb, circuit_breaker_metrics_t *out_metrics) {
    if (!cb || !out_metrics) {
        return;
    }
    
    pthread_mutex_lock(&cb->mutex);
    *out_metrics = cb->metrics;
    pthread_mutex_unlock(&cb->mutex);
}

/**
 * Reset circuit breaker to CLOSED state
 */
void circuit_breaker_reset(circuit_breaker_t *cb) {
    if (!cb) {
        return;
    }
    
    pthread_mutex_lock(&cb->mutex);
    
    cb->state = CB_STATE_CLOSED;
    memset(&cb->metrics, 0, sizeof(cb->metrics));
    cb->current_backoff_ms = cb->config.initial_backoff_ms;
    cb->next_retry_time = 0;
    cb->half_open_requests = 0;
    cb->half_open_successes = 0;
    cb->metrics.last_state_change = time(NULL);
    
    pthread_mutex_unlock(&cb->mutex);
}

/**
 * Get human-readable state name
 */
const char* circuit_breaker_state_name(circuit_breaker_state_t state) {
    switch (state) {
        case CB_STATE_CLOSED:    return "CLOSED";
        case CB_STATE_OPEN:      return "OPEN";
        case CB_STATE_HALF_OPEN: return "HALF_OPEN";
        default:                 return "UNKNOWN";
    }
}
