// internal/controller/namespace_controller.c
// Namespace Controller Implementation

#include "namespace_controller.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

static namespace_controller_t g_controller = {0};

// ============ Initialization & Cleanup ============

int namespace_controller_init(void) {
    memset(&g_controller, 0, sizeof(namespace_controller_t));
    pthread_mutex_init(&g_controller.lock, NULL);
    
    // Create default namespace
    namespace_config_t default_config = {0};
    strcpy(default_config.name, NAMESPACE_DEFAULT);
    namespace_controller_create(&default_config);
    
    return 0;
}

int namespace_controller_run(void) {
    g_controller.running = 1;
    
    if (pthread_create(&g_controller.cleanup_thread, NULL,
                      namespace_controller_cleanup_thread, NULL) != 0) {
        return -1;
    }
    
    printf("Namespace Controller started\n");
    return 0;
}

int namespace_controller_shutdown(void) {
    g_controller.running = 0;
    pthread_join(g_controller.cleanup_thread, NULL);
    pthread_mutex_destroy(&g_controller.lock);
    return 0;
}

// ============ Namespace Lifecycle ============

int namespace_controller_create(const namespace_config_t* config) {
    if (!config || strlen(config->name) == 0) return -1;
    if (namespace_controller_validate_name(config->name) != 0) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    // Check if exists
    for (int i = 0; i < g_controller.namespace_count; i++) {
        if (strcmp(g_controller.namespaces[i].name, config->name) == 0) {
            pthread_mutex_unlock(&g_controller.lock);
            return -1;
        }
    }
    
    if (g_controller.namespace_count >= NAMESPACE_MAX_NAMESPACES) {
        pthread_mutex_unlock(&g_controller.lock);
        return -1;
    }
    
    namespace_record_t* ns = &g_controller.namespaces[g_controller.namespace_count++];
    memset(ns, 0, sizeof(namespace_record_t));
    
    strncpy(ns->name, config->name, sizeof(ns->name) - 1);
    strncpy(ns->labels, config->labels, sizeof(ns->labels) - 1);
    
    ns->phase = NAMESPACE_PHASE_ACTIVE;
    ns->isolation_level = config->isolation_level;
    ns->created_at = time(NULL);
    
    if (config->create_default_network_policy) {
        ns->network_isolation.ingress_restricted = 1;
        ns->network_isolation.allow_pod_to_pod = 1;
    }
    
    g_controller.total_created++;
    
    pthread_mutex_unlock(&g_controller.lock);
    
    printf("[Namespace] Created: %s\n", config->name);
    return 0;
}

int namespace_controller_delete(const char* name) {
    if (!name || strlen(name) == 0) return -1;
    if (namespace_controller_is_protected(name)) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.namespace_count; i++) {
        if (strcmp(g_controller.namespaces[i].name, name) == 0) {
            namespace_record_t* ns = &g_controller.namespaces[i];
            ns->phase = NAMESPACE_PHASE_TERMINATING;
            ns->deleted_at = time(NULL);
            ns->deletion_grace_seconds = 30;
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int namespace_controller_get(const char* name, json_object** result) {
    if (!name || !result) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.namespace_count; i++) {
        namespace_record_t* ns = &g_controller.namespaces[i];
        if (strcmp(ns->name, name) == 0) {
            *result = json_object_new_object();
            json_object_object_add(*result, "name", json_object_new_string(ns->name));
            json_object_object_add(*result, "phase", json_object_new_int(ns->phase));
            json_object_object_add(*result, "isolation_level", json_object_new_int(ns->isolation_level));
            json_object_object_add(*result, "pod_count", json_object_new_int(ns->pod_count));
            json_object_object_add(*result, "service_count", json_object_new_int(ns->service_count));
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int namespace_controller_list(json_object** result) {
    if (!result) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    *result = json_object_new_array();
    
    for (int i = 0; i < g_controller.namespace_count; i++) {
        namespace_record_t* ns = &g_controller.namespaces[i];
        
        json_object* item = json_object_new_object();
        json_object_object_add(item, "name", json_object_new_string(ns->name));
        json_object_object_add(item, "phase", json_object_new_int(ns->phase));
        json_object_object_add(item, "pod_count", json_object_new_int(ns->pod_count));
        
        json_array_add(json_object_get_array(*result), item);
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return 0;
}

int namespace_controller_update(const char* name, const char* labels, const char* annotations) {
    if (!name) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.namespace_count; i++) {
        if (strcmp(g_controller.namespaces[i].name, name) == 0) {
            if (labels) strncpy(g_controller.namespaces[i].labels, labels, sizeof(g_controller.namespaces[i].labels) - 1);
            if (annotations) strncpy(g_controller.namespaces[i].annotations, annotations, sizeof(g_controller.namespaces[i].annotations) - 1);
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

// ============ State Management ============

int namespace_controller_get_phase(const char* name, namespace_phase_t* phase) {
    if (!name || !phase) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.namespace_count; i++) {
        if (strcmp(g_controller.namespaces[i].name, name) == 0) {
            *phase = g_controller.namespaces[i].phase;
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int namespace_controller_start_termination(const char* name, int grace_seconds) {
    if (!name) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.namespace_count; i++) {
        if (strcmp(g_controller.namespaces[i].name, name) == 0) {
            g_controller.namespaces[i].phase = NAMESPACE_PHASE_TERMINATING;
            g_controller.namespaces[i].deleted_at = time(NULL);
            g_controller.namespaces[i].deletion_grace_seconds = grace_seconds;
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int namespace_controller_finalize_deletion(const char* name) {
    if (!name) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.namespace_count; i++) {
        if (strcmp(g_controller.namespaces[i].name, name) == 0) {
            if (i < g_controller.namespace_count - 1) {
                memmove(&g_controller.namespaces[i], &g_controller.namespaces[i + 1],
                       (g_controller.namespace_count - i - 1) * sizeof(namespace_record_t));
            }
            g_controller.namespace_count--;
            g_controller.total_deleted++;
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

// ============ Isolation Management ============

int namespace_controller_set_isolation(const char* name, namespace_isolation_level_t level) {
    if (!name) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.namespace_count; i++) {
        if (strcmp(g_controller.namespaces[i].name, name) == 0) {
            g_controller.namespaces[i].isolation_level = level;
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int namespace_controller_get_isolation(const char* name, namespace_isolation_level_t* level) {
    if (!name || !level) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.namespace_count; i++) {
        if (strcmp(g_controller.namespaces[i].name, name) == 0) {
            *level = g_controller.namespaces[i].isolation_level;
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int namespace_controller_configure_network_isolation(const char* name,
                                                    const network_isolation_t* isolation) {
    if (!name || !isolation) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.namespace_count; i++) {
        if (strcmp(g_controller.namespaces[i].name, name) == 0) {
            memcpy(&g_controller.namespaces[i].network_isolation, isolation, sizeof(network_isolation_t));
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

// ============ Object Counting ============

int namespace_controller_get_object_count(const char* name,
                                         int* pod_count,
                                         int* service_count,
                                         int* deployment_count,
                                         int* configmap_count,
                                         int* secret_count) {
    if (!name) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.namespace_count; i++) {
        if (strcmp(g_controller.namespaces[i].name, name) == 0) {
            namespace_record_t* ns = &g_controller.namespaces[i];
            if (pod_count) *pod_count = ns->pod_count;
            if (service_count) *service_count = ns->service_count;
            if (deployment_count) *deployment_count = ns->deployment_count;
            if (configmap_count) *configmap_count = ns->configmap_count;
            if (secret_count) *secret_count = ns->secret_count;
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

// ============ Validation ============

int namespace_controller_validate_name(const char* name) {
    if (!name || strlen(name) == 0) return -1;
    if (strlen(name) > 253) return -1;
    
    // RFC 1123: lowercase alphanumeric and hyphens only
    for (int i = 0; name[i]; i++) {
        if (!((name[i] >= 'a' && name[i] <= 'z') ||
              (name[i] >= '0' && name[i] <= '9') ||
              name[i] == '-')) {
            return -1;
        }
    }
    
    return 0;
}

int namespace_controller_is_protected(const char* name) {
    if (!name) return 0;
    
    // Protect system namespaces
    if (strcmp(name, "default") == 0 ||
        strcmp(name, "kube-system") == 0 ||
        strcmp(name, "kube-public") == 0 ||
        strcmp(name, "kube-node-lease") == 0) {
        return 1;
    }
    
    return 0;
}

// ============ Resource Cleanup ============

int namespace_controller_cleanup_resources(const char* name) {
    if (!name) return -1;
    
    printf("[Namespace] Cleaning up resources in namespace: %s\n", name);
    
    // This would delete all objects in the namespace
    return 0;
}

int namespace_controller_cleanup_orphaned_namespaces(int* cleanup_count) {
    if (!cleanup_count) return -1;
    
    *cleanup_count = 0;
    
    pthread_mutex_lock(&g_controller.lock);
    
    time_t now = time(NULL);
    for (int i = g_controller.namespace_count - 1; i >= 0; i--) {
        namespace_record_t* ns = &g_controller.namespaces[i];
        
        if (ns->phase == NAMESPACE_PHASE_TERMINATING &&
           (now - ns->deleted_at >= ns->deletion_grace_seconds)) {
            
            if (!namespace_controller_is_protected(ns->name)) {
                if (i < g_controller.namespace_count - 1) {
                    memmove(ns, &g_controller.namespaces[i + 1],
                           (g_controller.namespace_count - i - 1) * sizeof(namespace_record_t));
                }
                g_controller.namespace_count--;
                (*cleanup_count)++;
            }
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return 0;
}

// ============ Background Thread ============

void* namespace_controller_cleanup_thread(void* arg) {
    (void)arg;
    
    while (g_controller.running) {
        sleep(30);
        
        int cleanup_count = 0;
        namespace_controller_cleanup_orphaned_namespaces(&cleanup_count);
        
        if (cleanup_count > 0) {
            printf("[Namespace] Cleaned up %d namespaces\n", cleanup_count);
        }
    }
    
    return NULL;
}
