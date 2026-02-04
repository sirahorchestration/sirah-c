/*
 * ha_manager.c
 * 
 * Implementation of control plane HA and leader election
 */

#include "ha_manager.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <curl/curl.h>

/**
 * Election thread function
 */
static void* ha_election_thread(void *arg);

/**
 * Helper to write data from etcd API response
 */
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp);

/**
 * Helper to perform CAS operation on etcd
 */
static bool etcd_cas_write(const char *endpoint, const char *key, const char *value,
                          const char *prev_value, uint32_t ttl);

/**
 * Helper to read value from etcd
 */
static char* etcd_read(const char *endpoint, const char *key);

/**
 * Create a new HA manager
 */
ha_manager_t* ha_manager_new(const ha_manager_config_t *config) {
    if (!config) {
        return NULL;
    }
    
    ha_manager_t *manager = (ha_manager_t *)malloc(sizeof(ha_manager_t));
    if (!manager) {
        return NULL;
    }
    
    manager->config = *config;
    manager->config.etcd_endpoint = (char *)malloc(strlen(config->etcd_endpoint) + 1);
    strcpy(manager->config.etcd_endpoint, config->etcd_endpoint);
    
    manager->config.election_key = (char *)malloc(strlen(config->election_key) + 1);
    strcpy(manager->config.election_key, config->election_key);
    
    manager->config.component_name = (char *)malloc(strlen(config->component_name) + 1);
    strcpy(manager->config.component_name, config->component_name);
    
    manager->config.instance_id = (char *)malloc(strlen(config->instance_id) + 1);
    strcpy(manager->config.instance_id, config->instance_id);
    
    manager->status = LEADER_STATUS_FOLLOWER;
    memset(&manager->current_leader, 0, sizeof(manager->current_leader));
    
    pthread_mutex_init(&manager->mutex, NULL);
    
    manager->election_running = false;
    
    manager->on_become_leader = NULL;
    manager->on_lose_leadership = NULL;
    manager->callback_userdata = NULL;
    
    return manager;
}

/**
 * Free HA manager
 */
void ha_manager_free(ha_manager_t *manager) {
    if (!manager) {
        return;
    }
    
    if (manager->election_running) {
        ha_manager_stop(manager);
    }
    
    free(manager->config.etcd_endpoint);
    free(manager->config.election_key);
    free(manager->config.component_name);
    free(manager->config.instance_id);
    
    if (manager->current_leader.leader_name) {
        free(manager->current_leader.leader_name);
    }
    if (manager->current_leader.leader_id) {
        free(manager->current_leader.leader_id);
    }
    
    pthread_mutex_destroy(&manager->mutex);
    free(manager);
}

/**
 * Write callback for curl
 */
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    
    char *ptr = (char *)realloc((char *)userp, realsize + 1);
    if (!ptr) {
        return 0;
    }
    
    memcpy(ptr, contents, realsize);
    ptr[realsize] = 0;
    
    // This is a simplified approach - proper implementation would need json parsing
    *(char **)userp = ptr;
    
    return realsize;
}

/**
 * Read from etcd
 */
static char* etcd_read(const char *endpoint, const char *key) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return NULL;
    }
    
    // Construct etcd API URL
    char url[512];
    snprintf(url, sizeof(url), "http://%s/v3/kv/range", endpoint);
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    // Build request body with key in base64 (simplified: just use raw)
    char body[256];
    snprintf(body, sizeof(body), "{\"key\":\"%s\"}", key);
    
    char *response_data = (char *)malloc(1);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        free(response_data);
        return NULL;
    }
    
    return response_data;
}

/**
 * CAS write to etcd (Compare-And-Swap)
 */
static bool etcd_cas_write(const char *endpoint, const char *key, const char *value,
                          const char *prev_value, uint32_t ttl) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return false;
    }
    
    // Construct etcd API URL for atomic write
    char url[512];
    snprintf(url, sizeof(url), "http://%s/v3/kv/txn", endpoint);
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    // Build transaction for CAS
    // This is simplified - real etcd uses base64 encoding
    char body[1024];
    if (prev_value) {
        snprintf(body, sizeof(body),
                "{\"compare\":[{\"key\":\"%s\",\"result\":\"EQUAL\",\"value\":\"%s\"}],"
                "\"success\":[{\"request_put\":{\"key\":\"%s\",\"value\":\"%s\"}}]}",
                key, prev_value, key, value);
    } else {
        snprintf(body, sizeof(body),
                "{\"success\":[{\"request_put\":{\"key\":\"%s\",\"value\":\"%s\"}}]}",
                key, value);
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    return (res == CURLE_OK && http_code == 200);
}

/**
 * Election thread function
 */
static void* ha_election_thread(void *arg) {
    ha_manager_t *manager = (ha_manager_t *)arg;
    
    printf("HA_MANAGER: Election thread started\n");
    
    while (manager->election_running) {
        pthread_mutex_lock(&manager->mutex);
        
        // Check if we're currently leader
        bool is_leader = (manager->status == LEADER_STATUS_LEADER);
        
        pthread_mutex_unlock(&manager->mutex);
        
        if (is_leader) {
            // Renew leadership every renewal_interval_seconds
            printf("HA_MANAGER: Renewing leadership lease\n");
            
            // Try to renew leadership via CAS
            char lease_value[256];
            snprintf(lease_value, sizeof(lease_value), 
                    "{\"leader\":\"%s\",\"id\":\"%s\",\"since\":%ld}",
                    manager->config.component_name,
                    manager->config.instance_id,
                    time(NULL));
            
            bool renewed = etcd_cas_write(
                manager->config.etcd_endpoint,
                manager->config.election_key,
                lease_value,
                NULL,  // Don't require specific prev value
                manager->config.lease_duration_seconds
            );
            
            if (!renewed) {
                printf("HA_MANAGER: Failed to renew leadership, stepping down\n");
                
                pthread_mutex_lock(&manager->mutex);
                manager->status = LEADER_STATUS_FOLLOWER;
                pthread_mutex_unlock(&manager->mutex);
                
                if (manager->on_lose_leadership) {
                    manager->on_lose_leadership(manager->callback_userdata);
                }
            }
        } else {
            // Try to become leader
            printf("HA_MANAGER: Attempting to become leader\n");
            
            char lease_value[256];
            snprintf(lease_value, sizeof(lease_value), 
                    "{\"leader\":\"%s\",\"id\":\"%s\",\"since\":%ld}",
                    manager->config.component_name,
                    manager->config.instance_id,
                    time(NULL));
            
            bool became_leader = etcd_cas_write(
                manager->config.etcd_endpoint,
                manager->config.election_key,
                lease_value,
                NULL,  // Try to create or overwrite
                manager->config.lease_duration_seconds
            );
            
            if (became_leader) {
                printf("HA_MANAGER: Became leader\n");
                
                pthread_mutex_lock(&manager->mutex);
                manager->status = LEADER_STATUS_LEADER;
                manager->current_leader.leadership_transitions++;
                pthread_mutex_unlock(&manager->mutex);
                
                if (manager->on_become_leader) {
                    manager->on_become_leader(manager->callback_userdata);
                }
            }
        }
        
        // Sleep before next attempt
        uint32_t sleep_seconds = is_leader ? 
            manager->config.renewal_interval_seconds :
            manager->config.failover_timeout_seconds / 2;
        
        sleep(sleep_seconds);
    }
    
    printf("HA_MANAGER: Election thread exiting\n");
    return NULL;
}

/**
 * Start leader election
 */
bool ha_manager_start(ha_manager_t *manager) {
    if (!manager) {
        return false;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    if (manager->election_running) {
        pthread_mutex_unlock(&manager->mutex);
        return true;  // Already running
    }
    
    manager->election_running = true;
    manager->current_leader.leadership_transitions = 0;
    
    pthread_mutex_unlock(&manager->mutex);
    
    if (pthread_create(&manager->election_thread, NULL, ha_election_thread, manager) != 0) {
        pthread_mutex_lock(&manager->mutex);
        manager->election_running = false;
        pthread_mutex_unlock(&manager->mutex);
        perror("pthread_create");
        return false;
    }
    
    printf("HA_MANAGER: Started election process for '%s' (instance: %s)\n",
           manager->config.component_name,
           manager->config.instance_id);
    
    return true;
}

/**
 * Stop leader election
 */
void ha_manager_stop(ha_manager_t *manager) {
    if (!manager) {
        return;
    }
    
    pthread_mutex_lock(&manager->mutex);
    manager->election_running = false;
    pthread_mutex_unlock(&manager->mutex);
    
    printf("HA_MANAGER: Waiting for election thread to exit\n");
    pthread_join(manager->election_thread, NULL);
    printf("HA_MANAGER: Election stopped\n");
}

/**
 * Get leadership status
 */
leader_status_t ha_manager_get_status(ha_manager_t *manager) {
    if (!manager) {
        return LEADER_STATUS_FOLLOWER;
    }
    
    pthread_mutex_lock(&manager->mutex);
    leader_status_t status = manager->status;
    pthread_mutex_unlock(&manager->mutex);
    
    return status;
}

/**
 * Check if is leader
 */
bool ha_manager_is_leader(ha_manager_t *manager) {
    return ha_manager_get_status(manager) == LEADER_STATUS_LEADER;
}

/**
 * Get leader info
 */
leader_info_t* ha_manager_get_leader_info(ha_manager_t *manager) {
    if (!manager) {
        return NULL;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    leader_info_t *info = (leader_info_t *)malloc(sizeof(leader_info_t));
    if (!info) {
        pthread_mutex_unlock(&manager->mutex);
        return NULL;
    }
    
    if (manager->current_leader.leader_name) {
        info->leader_name = (char *)malloc(strlen(manager->current_leader.leader_name) + 1);
        strcpy(info->leader_name, manager->current_leader.leader_name);
    } else {
        info->leader_name = NULL;
    }
    
    if (manager->current_leader.leader_id) {
        info->leader_id = (char *)malloc(strlen(manager->current_leader.leader_id) + 1);
        strcpy(info->leader_id, manager->current_leader.leader_id);
    } else {
        info->leader_id = NULL;
    }
    
    info->leader_since = manager->current_leader.leader_since;
    info->lease_expires = manager->current_leader.lease_expires;
    info->leadership_transitions = manager->current_leader.leadership_transitions;
    
    pthread_mutex_unlock(&manager->mutex);
    
    return info;
}

/**
 * Free leader info
 */
void ha_manager_free_leader_info(leader_info_t *info) {
    if (!info) {
        return;
    }
    
    free(info->leader_name);
    free(info->leader_id);
    free(info);
}

/**
 * Register become leader callback
 */
void ha_manager_set_become_leader_callback(ha_manager_t *manager,
                                           void (*callback)(void *userdata),
                                           void *userdata) {
    if (!manager) {
        return;
    }
    
    pthread_mutex_lock(&manager->mutex);
    manager->on_become_leader = callback;
    manager->callback_userdata = userdata;
    pthread_mutex_unlock(&manager->mutex);
}

/**
 * Register lose leadership callback
 */
void ha_manager_set_lose_leadership_callback(ha_manager_t *manager,
                                             void (*callback)(void *userdata),
                                             void *userdata) {
    if (!manager) {
        return;
    }
    
    pthread_mutex_lock(&manager->mutex);
    manager->on_lose_leadership = callback;
    manager->callback_userdata = userdata;
    pthread_mutex_unlock(&manager->mutex);
}

/**
 * Step down from leadership
 */
void ha_manager_step_down(ha_manager_t *manager) {
    if (!manager) {
        return;
    }
    
    pthread_mutex_lock(&manager->mutex);
    manager->status = LEADER_STATUS_FOLLOWER;
    pthread_mutex_unlock(&manager->mutex);
    
    printf("HA_MANAGER: Stepping down from leadership\n");
}

/**
 * Force re-election
 */
void ha_manager_force_election(ha_manager_t *manager) {
    if (!manager) {
        return;
    }
    
    printf("HA_MANAGER: Forcing re-election\n");
    ha_manager_step_down(manager);
}
