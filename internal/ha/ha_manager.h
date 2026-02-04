/*
 * ha_manager.h
 * 
 * Control plane high availability and leader election
 * 
 * Implements leader election for multi-node control planes using etcd
 * 
 * Features:
 *   - Leader election via etcd Compare-And-Swap (CAS)
 *   - Automatic failover when leader fails
 *   - Health checks for leader liveness
 *   - Lease-based leadership with renewal
 * 
 * Kubernetes v1.28 Conformance:
 *   - Standard Kubernetes leader election pattern
 *   - Compatible with kube-controller-manager and kube-scheduler
 *   - Uses etcd as coordination backend
 * 
 * References:
 *   - https://kubernetes.io/docs/concepts/configuration/overview/
 *   - https://etcd.io/docs/v3.5/learning/api_guarantees/
 */

#ifndef SIRAH_HA_MANAGER_H
#define SIRAH_HA_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>

/**
 * Leadership status
 */
typedef enum {
    LEADER_STATUS_FOLLOWER = 0,
    LEADER_STATUS_LEADER = 1,
    LEADER_STATUS_CANDIDATE = 2
} leader_status_t;

/**
 * Leadership information
 */
typedef struct {
    char *leader_name;
    char *leader_id;
    time_t leader_since;
    time_t lease_expires;
    uint32_t leadership_transitions;
} leader_info_t;

/**
 * HA manager configuration
 */
typedef struct {
    char *etcd_endpoint;              // etcd server endpoint (e.g., "localhost:2379")
    char *election_key;               // etcd key for leader election (e.g., "/sirah/leader")
    char *component_name;             // Component name (e.g., "controller-manager", "scheduler")
    char *instance_id;                // This instance's unique ID
    uint32_t lease_duration_seconds;  // How long leadership lasts (e.g., 30)
    uint32_t renewal_interval_seconds;// How often to renew lease (e.g., 10)
    uint32_t failover_timeout_seconds;// Time before initiating failover (e.g., 40)
} ha_manager_config_t;

/**
 * HA manager instance
 */
typedef struct {
    ha_manager_config_t config;
    leader_status_t status;
    leader_info_t current_leader;
    
    pthread_mutex_t mutex;
    pthread_t election_thread;
    bool election_running;
    
    // Callback when becoming leader
    void (*on_become_leader)(void *userdata);
    
    // Callback when losing leadership
    void (*on_lose_leadership)(void *userdata);
    
    void *callback_userdata;
} ha_manager_t;

/**
 * Create a new HA manager
 */
ha_manager_t* ha_manager_new(const ha_manager_config_t *config);

/**
 * Free HA manager
 */
void ha_manager_free(ha_manager_t *manager);

/**
 * Start leader election process
 * 
 * Spawns background thread that attempts to become leader
 * and maintains leadership via lease renewal
 */
bool ha_manager_start(ha_manager_t *manager);

/**
 * Stop leader election
 */
void ha_manager_stop(ha_manager_t *manager);

/**
 * Get current leadership status
 */
leader_status_t ha_manager_get_status(ha_manager_t *manager);

/**
 * Check if this instance is the leader
 */
bool ha_manager_is_leader(ha_manager_t *manager);

/**
 * Get current leader information
 * 
 * Returns leader info struct, caller must free it
 */
leader_info_t* ha_manager_get_leader_info(ha_manager_t *manager);

/**
 * Free leader info struct
 */
void ha_manager_free_leader_info(leader_info_t *info);

/**
 * Register callback when this instance becomes leader
 */
void ha_manager_set_become_leader_callback(ha_manager_t *manager,
                                           void (*callback)(void *userdata),
                                           void *userdata);

/**
 * Register callback when this instance loses leadership
 */
void ha_manager_set_lose_leadership_callback(ha_manager_t *manager,
                                             void (*callback)(void *userdata),
                                             void *userdata);

/**
 * Gracefully step down from leadership
 * 
 * Used when instance needs to shut down or yield leadership
 */
void ha_manager_step_down(ha_manager_t *manager);

/**
 * Force re-election
 * 
 * Used for testing or manual intervention
 */
void ha_manager_force_election(ha_manager_t *manager);

#endif // SIRAH_HA_MANAGER_H
