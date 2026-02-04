// internal/controller/pod_eviction_controller.c
// Pod Eviction Controller Implementation

#include "pod_eviction_controller.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

static pod_eviction_controller_t g_controller = {0};

// ============ Initialization & Cleanup ============

int pod_eviction_controller_init(void) {
    memset(&g_controller, 0, sizeof(pod_eviction_controller_t));
    pthread_mutex_init(&g_controller.lock, NULL);
    return 0;
}

int pod_eviction_controller_run(void) {
    g_controller.running = 1;
    
    if (pthread_create(&g_controller.eviction_thread, NULL,
                      pod_eviction_controller_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create eviction thread\n");
        return -1;
    }
    
    printf("Pod Eviction Controller started\n");
    return 0;
}

int pod_eviction_controller_shutdown(void) {
    g_controller.running = 0;
    pthread_join(g_controller.eviction_thread, NULL);
    pthread_mutex_destroy(&g_controller.lock);
    return 0;
}

// ============ PDB Management ============

int pod_eviction_create_pdb(const char* namespace, const char* name,
                           const char* selector, pdb_policy_type_t policy_type,
                           int policy_value) {
    if (!namespace || !name || !selector) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    if (g_controller.pdb_count >= EVICTION_MAX_PDBS) {
        pthread_mutex_unlock(&g_controller.lock);
        return -1;
    }
    
    pod_disruption_budget_t* pdb = &g_controller.pdbs[g_controller.pdb_count++];
    memset(pdb, 0, sizeof(pod_disruption_budget_t));
    
    strncpy(pdb->name, name, sizeof(pdb->name) - 1);
    strncpy(pdb->namespace, namespace, sizeof(pdb->namespace) - 1);
    strncpy(pdb->selector, selector, sizeof(pdb->selector) - 1);
    
    pdb->policy_type = policy_type;
    pdb->policy_value = policy_value;
    pdb->enabled = 1;
    pdb->created_at = time(NULL);
    
    // Create status record
    if (g_controller.pdb_status_count < EVICTION_MAX_PDBS) {
        pdb_status_t* status = &g_controller.pdb_statuses[g_controller.pdb_status_count++];
        memset(status, 0, sizeof(pdb_status_t));
        strncpy(status->name, name, sizeof(status->name) - 1);
        strncpy(status->namespace, namespace, sizeof(status->namespace) - 1);
        status->desired_healthy = policy_value;
        status->last_updated = time(NULL);
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    
    printf("[PodEviction] Created PDB: %s/%s\n", namespace, name);
    return 0;
}

int pod_eviction_update_pdb(const char* namespace, const char* name,
                           pdb_policy_type_t policy_type, int policy_value) {
    if (!namespace || !name) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.pdb_count; i++) {
        if (strcmp(g_controller.pdbs[i].namespace, namespace) == 0 &&
            strcmp(g_controller.pdbs[i].name, name) == 0) {
            
            g_controller.pdbs[i].policy_type = policy_type;
            g_controller.pdbs[i].policy_value = policy_value;
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int pod_eviction_delete_pdb(const char* namespace, const char* name) {
    if (!namespace || !name) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.pdb_count; i++) {
        if (strcmp(g_controller.pdbs[i].namespace, namespace) == 0 &&
            strcmp(g_controller.pdbs[i].name, name) == 0) {
            
            if (i < g_controller.pdb_count - 1) {
                memmove(&g_controller.pdbs[i], &g_controller.pdbs[i + 1],
                       (g_controller.pdb_count - i - 1) * sizeof(pod_disruption_budget_t));
            }
            g_controller.pdb_count--;
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int pod_eviction_list_pdbs(const char* namespace, json_object** result) {
    if (!result) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    *result = json_object_new_array();
    
    for (int i = 0; i < g_controller.pdb_count; i++) {
        pod_disruption_budget_t* pdb = &g_controller.pdbs[i];
        
        if (namespace == NULL || strcmp(pdb->namespace, namespace) == 0) {
            json_object* item = json_object_new_object();
            json_object_object_add(item, "name", json_object_new_string(pdb->name));
            json_object_object_add(item, "namespace", json_object_new_string(pdb->namespace));
            json_object_object_add(item, "policy_type", json_object_new_int(pdb->policy_type));
            json_object_object_add(item, "policy_value", json_object_new_int(pdb->policy_value));
            json_object_object_add(item, "enabled", json_object_new_int(pdb->enabled));
            
            json_array_add(json_object_get_array(*result), item);
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return 0;
}

int pod_eviction_get_pdb(const char* namespace, const char* name, json_object** result) {
    if (!namespace || !name || !result) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.pdb_count; i++) {
        pod_disruption_budget_t* pdb = &g_controller.pdbs[i];
        
        if (strcmp(pdb->namespace, namespace) == 0 && strcmp(pdb->name, name) == 0) {
            *result = json_object_new_object();
            json_object_object_add(*result, "name", json_object_new_string(pdb->name));
            json_object_object_add(*result, "namespace", json_object_new_string(pdb->namespace));
            json_object_object_add(*result, "policy_type", json_object_new_int(pdb->policy_type));
            json_object_object_add(*result, "policy_value", json_object_new_int(pdb->policy_value));
            json_object_object_add(*result, "enabled", json_object_new_int(pdb->enabled));
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

// ============ PDB Status ============

int pod_eviction_get_pdb_status(const char* namespace, const char* name, pdb_status_t* status) {
    if (!namespace || !name || !status) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.pdb_status_count; i++) {
        pdb_status_t* s = &g_controller.pdb_statuses[i];
        if (strcmp(s->namespace, namespace) == 0 && strcmp(s->name, name) == 0) {
            memcpy(status, s, sizeof(pdb_status_t));
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int pod_eviction_can_disrupt_pod(const char* namespace, const char* pod_name) {
    if (!namespace || !pod_name) return 0;
    
    pthread_mutex_lock(&g_controller.lock);
    
    // Check if pod is covered by any PDB
    for (int i = 0; i < g_controller.pdb_count; i++) {
        pod_disruption_budget_t* pdb = &g_controller.pdbs[i];
        
        if (strcmp(pdb->namespace, namespace) != 0) continue;
        if (!pdb->enabled) continue;
        
        // Check if pod matches selector
        // In real Kubernetes, this would use label matching
        // For now, simple substring matching
        if (strstr(pod_name, pdb->selector) != NULL) {
            // Find this PDB's status
            for (int j = 0; j < g_controller.pdb_status_count; j++) {
                pdb_status_t* status = &g_controller.pdb_statuses[j];
                if (strcmp(status->namespace, namespace) == 0 &&
                    strcmp(status->name, pdb->name) == 0) {
                    
                    if (pdb->policy_type == PDB_POLICY_MIN_AVAILABLE) {
                        // Can disrupt if we won't violate minimum
                        int result = status->current_healthy > pdb->policy_value ? 1 : 0;
                        pthread_mutex_unlock(&g_controller.lock);
                        return result;
                    } else {  // MAX_UNAVAILABLE
                        // Can disrupt if within limits
                        int result = status->disruptions_active < pdb->policy_value ? 1 : 0;
                        pthread_mutex_unlock(&g_controller.lock);
                        return result;
                    }
                }
            }
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return 1;  // No PDB covering this pod, can disrupt
}

int pod_eviction_get_disruptions_allowed(const char* namespace, const char* pdb_name,
                                        int* allowed) {
    if (!namespace || !pdb_name || !allowed) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.pdb_status_count; i++) {
        pdb_status_t* status = &g_controller.pdb_statuses[i];
        if (strcmp(status->namespace, namespace) == 0 &&
            strcmp(status->name, pdb_name) == 0) {
            
            *allowed = status->disruptions_allowed;
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

// ============ Eviction Operations ============

int pod_eviction_request_eviction(const char* pod_namespace, const char* pod_name,
                                 const char* evicting_node, int grace_period,
                                 const char* reason) {
    if (!pod_namespace || !pod_name) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    if (g_controller.eviction_count >= EVICTION_MAX_PODS) {
        pthread_mutex_unlock(&g_controller.lock);
        return -1;
    }
    
    eviction_request_t* req = &g_controller.evictions[g_controller.eviction_count++];
    memset(req, 0, sizeof(eviction_request_t));
    
    strncpy(req->pod_namespace, pod_namespace, sizeof(req->pod_namespace) - 1);
    strncpy(req->pod_name, pod_name, sizeof(req->pod_name) - 1);
    if (evicting_node) {
        strncpy(req->evicting_node, evicting_node, sizeof(req->evicting_node) - 1);
    }
    
    req->grace_period = grace_period > 0 ? grace_period : EVICTION_GRACE_PERIOD_DEFAULT;
    if (reason) {
        strncpy(req->reason, reason, sizeof(req->reason) - 1);
    }
    
    req->eviction_requested_at = time(NULL);
    req->eviction_deadline = req->eviction_requested_at + req->grace_period;
    
    pthread_mutex_unlock(&g_controller.lock);
    
    printf("[PodEviction] Eviction requested: %s/%s\n", pod_namespace, pod_name);
    return 0;
}

int pod_eviction_execute_eviction(const char* pod_namespace, const char* pod_name,
                                 int grace_period) {
    if (!pod_namespace || !pod_name) return -1;
    
    printf("[PodEviction] Executing eviction: %s/%s (grace: %d seconds)\n",
          pod_namespace, pod_name, grace_period);
    
    // This would call the pod controller to delete the pod
    return 0;
}

int pod_eviction_cancel_eviction(const char* pod_namespace, const char* pod_name) {
    if (!pod_namespace || !pod_name) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.eviction_count; i++) {
        eviction_request_t* req = &g_controller.evictions[i];
        if (strcmp(req->pod_namespace, pod_namespace) == 0 &&
            strcmp(req->pod_name, pod_name) == 0 &&
            !req->evicted && !req->failed) {
            
            if (i < g_controller.eviction_count - 1) {
                memmove(req, &g_controller.evictions[i + 1],
                       (g_controller.eviction_count - i - 1) * sizeof(eviction_request_t));
            }
            g_controller.eviction_count--;
            
            pthread_mutex_unlock(&g_controller.lock);
            printf("[PodEviction] Cancelled eviction: %s/%s\n", pod_namespace, pod_name);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int pod_eviction_force_evict(const char* pod_namespace, const char* pod_name) {
    if (!pod_namespace || !pod_name) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.eviction_count; i++) {
        eviction_request_t* req = &g_controller.evictions[i];
        if (strcmp(req->pod_namespace, pod_namespace) == 0 &&
            strcmp(req->pod_name, pod_name) == 0) {
            
            req->force_eviction = 1;
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

// ============ Eviction Status ============

int pod_eviction_get_eviction_status(const char* pod_namespace, const char* pod_name,
                                    eviction_request_t* request) {
    if (!pod_namespace || !pod_name || !request) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.eviction_count; i++) {
        eviction_request_t* req = &g_controller.evictions[i];
        if (strcmp(req->pod_namespace, pod_namespace) == 0 &&
            strcmp(req->pod_name, pod_name) == 0) {
            
            memcpy(request, req, sizeof(eviction_request_t));
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int pod_eviction_list_pending_evictions(json_object** result) {
    if (!result) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    *result = json_object_new_array();
    
    for (int i = 0; i < g_controller.eviction_count; i++) {
        eviction_request_t* req = &g_controller.evictions[i];
        
        if (!req->evicted && !req->failed) {
            json_object* item = json_object_new_object();
            json_object_object_add(item, "pod_namespace", json_object_new_string(req->pod_namespace));
            json_object_object_add(item, "pod_name", json_object_new_string(req->pod_name));
            json_object_object_add(item, "grace_period", json_object_new_int(req->grace_period));
            json_object_object_add(item, "reason", json_object_new_string(req->reason));
            
            json_array_add(json_object_get_array(*result), item);
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return 0;
}

int pod_eviction_get_eviction_progress(int* total, int* evicted, int* failed) {
    if (!total || !evicted || !failed) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    *total = g_controller.eviction_count;
    *evicted = 0;
    *failed = 0;
    
    for (int i = 0; i < g_controller.eviction_count; i++) {
        if (g_controller.evictions[i].evicted) (*evicted)++;
        if (g_controller.evictions[i].failed) (*failed)++;
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return 0;
}

// ============ Batch Operations ============

int pod_eviction_drain_node(const char* node_name, const char* reason,
                           int grace_period, int* eviction_count) {
    if (!node_name || !eviction_count) return -1;
    
    *eviction_count = 0;
    
    printf("[PodEviction] Draining node: %s (reason: %s)\n", node_name, reason ? reason : "");
    
    // This would get pods on the node and evict them
    // For now, just return success
    
    return 0;
}

int pod_eviction_get_node_drain_status(const char* node_name,
                                      int* total_pods, int* evicted_pods,
                                      int* failed_pods) {
    if (!node_name || !total_pods || !evicted_pods || !failed_pods) return -1;
    
    *total_pods = 0;
    *evicted_pods = 0;
    *failed_pods = 0;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.eviction_count; i++) {
        eviction_request_t* req = &g_controller.evictions[i];
        if (strlen(req->evicting_node) > 0 && strcmp(req->evicting_node, node_name) == 0) {
            (*total_pods)++;
            if (req->evicted) (*evicted_pods)++;
            if (req->failed) (*failed_pods)++;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return 0;
}

// ============ Background Thread ============

void* pod_eviction_controller_thread(void* arg) {
    (void)arg;
    
    while (g_controller.running) {
        sleep(EVICTION_POLL_INTERVAL);
        
        pthread_mutex_lock(&g_controller.lock);
        
        // Process eviction requests
        time_t now = time(NULL);
        for (int i = 0; i < g_controller.eviction_count; i++) {
            eviction_request_t* req = &g_controller.evictions[i];
            
            if (req->evicted || req->failed) continue;
            
            // Check if deadline reached
            if (now >= req->eviction_deadline) {
                req->evicted = 1;
                g_controller.successful_evictions++;
            }
        }
        
        pthread_mutex_unlock(&g_controller.lock);
    }
    
    return NULL;
}
