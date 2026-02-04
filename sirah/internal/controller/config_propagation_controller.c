// internal/controller/config_propagation_controller.c
// Config Propagation Controller Implementation

#include "config_propagation_controller.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

static config_propagation_controller_t g_controller = {0};

// ============ Initialization & Cleanup ============

int config_propagation_controller_init(void) {
    memset(&g_controller, 0, sizeof(config_propagation_controller_t));
    pthread_mutex_init(&g_controller.lock, NULL);
    return 0;
}

int config_propagation_controller_run(void) {
    g_controller.running = 1;
    
    if (pthread_create(&g_controller.propagation_thread, NULL,
                      config_propagation_controller_thread, NULL) != 0) {
        return -1;
    }
    
    printf("Config Propagation Controller started\n");
    return 0;
}

int config_propagation_controller_shutdown(void) {
    g_controller.running = 0;
    pthread_join(g_controller.propagation_thread, NULL);
    pthread_mutex_destroy(&g_controller.lock);
    return 0;
}

// ============ Rule Management ============

int config_propagation_create_rule(
    const char* source_namespace,
    const char* source_name,
    const char* source_kind,
    config_propagation_scope_t scope,
    const char* scope_selector,
    config_propagation_mode_t mode,
    int auto_sync) {
    
    if (!source_namespace || !source_name || !source_kind) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    if (g_controller.rule_count >= CONFIG_MAX_PROPAGATIONS) {
        pthread_mutex_unlock(&g_controller.lock);
        return -1;
    }
    
    config_propagation_rule_t* rule = &g_controller.rules[g_controller.rule_count++];
    memset(rule, 0, sizeof(config_propagation_rule_t));
    
    strncpy(rule->source_namespace, source_namespace, sizeof(rule->source_namespace) - 1);
    strncpy(rule->source_name, source_name, sizeof(rule->source_name) - 1);
    strncpy(rule->source_kind, source_kind, sizeof(rule->source_kind) - 1);
    
    rule->scope = scope;
    if (scope_selector) {
        strncpy(rule->scope_selector, scope_selector, sizeof(rule->scope_selector) - 1);
    }
    
    rule->default_mode = mode;
    rule->auto_sync = auto_sync;
    rule->enabled = 1;
    rule->created_at = time(NULL);
    
    pthread_mutex_unlock(&g_controller.lock);
    
    printf("[ConfigPropagation] Created rule: %s.%s/%s\n", source_namespace, source_kind, source_name);
    return 0;
}

int config_propagation_delete_rule(
    const char* source_namespace,
    const char* source_name,
    const char* source_kind) {
    
    if (!source_namespace || !source_name || !source_kind) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.rule_count; i++) {
        config_propagation_rule_t* rule = &g_controller.rules[i];
        
        if (strcmp(rule->source_namespace, source_namespace) == 0 &&
            strcmp(rule->source_name, source_name) == 0 &&
            strcmp(rule->source_kind, source_kind) == 0) {
            
            if (i < g_controller.rule_count - 1) {
                memmove(rule, &g_controller.rules[i + 1],
                       (g_controller.rule_count - i - 1) * sizeof(config_propagation_rule_t));
            }
            g_controller.rule_count--;
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int config_propagation_update_rule(
    const char* source_namespace,
    const char* source_name,
    const char* source_kind,
    config_propagation_scope_t scope,
    const char* scope_selector,
    config_propagation_mode_t mode) {
    
    if (!source_namespace || !source_name || !source_kind) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.rule_count; i++) {
        config_propagation_rule_t* rule = &g_controller.rules[i];
        
        if (strcmp(rule->source_namespace, source_namespace) == 0 &&
            strcmp(rule->source_name, source_name) == 0 &&
            strcmp(rule->source_kind, source_kind) == 0) {
            
            rule->scope = scope;
            rule->default_mode = mode;
            if (scope_selector) {
                strncpy(rule->scope_selector, scope_selector, sizeof(rule->scope_selector) - 1);
            }
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int config_propagation_get_rule(
    const char* source_namespace,
    const char* source_name,
    const char* source_kind,
    json_object** result) {
    
    if (!source_namespace || !source_name || !source_kind || !result) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.rule_count; i++) {
        config_propagation_rule_t* rule = &g_controller.rules[i];
        
        if (strcmp(rule->source_namespace, source_namespace) == 0 &&
            strcmp(rule->source_name, source_name) == 0 &&
            strcmp(rule->source_kind, source_kind) == 0) {
            
            *result = json_object_new_object();
            json_object_object_add(*result, "source_namespace", json_object_new_string(rule->source_namespace));
            json_object_object_add(*result, "source_name", json_object_new_string(rule->source_name));
            json_object_object_add(*result, "source_kind", json_object_new_string(rule->source_kind));
            json_object_object_add(*result, "scope", json_object_new_int(rule->scope));
            json_object_object_add(*result, "target_count", json_object_new_int(rule->target_count));
            json_object_object_add(*result, "enabled", json_object_new_int(rule->enabled));
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int config_propagation_list_rules(
    const char* source_namespace,
    json_object** result) {
    
    if (!result) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    *result = json_object_new_array();
    
    for (int i = 0; i < g_controller.rule_count; i++) {
        config_propagation_rule_t* rule = &g_controller.rules[i];
        
        if (source_namespace == NULL || strcmp(rule->source_namespace, source_namespace) == 0) {
            json_object* item = json_object_new_object();
            json_object_object_add(item, "source_name", json_object_new_string(rule->source_name));
            json_object_object_add(item, "source_kind", json_object_new_string(rule->source_kind));
            json_object_object_add(item, "target_count", json_object_new_int(rule->target_count));
            
            json_array_add(json_object_get_array(*result), item);
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return 0;
}

// ============ Propagation Execution ============

int config_propagation_evaluate_rules(void) {
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.rule_count; i++) {
        config_propagation_rule_t* rule = &g_controller.rules[i];
        
        if (!rule->enabled || !rule->auto_sync) continue;
        
        // Evaluate scope and determine target namespaces
        // In a real implementation, this would query namespace controller
        // For now, just mark rule as evaluated
        rule->last_evaluated = time(NULL);
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return 0;
}

int config_propagation_propagate_to_targets(
    const char* source_namespace,
    const char* source_name,
    const char* source_kind) {
    
    if (!source_namespace || !source_name || !source_kind) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.rule_count; i++) {
        config_propagation_rule_t* rule = &g_controller.rules[i];
        
        if (strcmp(rule->source_namespace, source_namespace) == 0 &&
            strcmp(rule->source_name, source_name) == 0 &&
            strcmp(rule->source_kind, source_kind) == 0) {
            
            // Propagate to each target
            for (int j = 0; j < rule->target_count; j++) {
                config_target_t* target = &rule->targets[j];
                
                propagation_event_t* event = NULL;
                if (g_controller.event_count < CONFIG_MAX_PROPAGATIONS * 2) {
                    event = &g_controller.events[g_controller.event_count++];
                }
                
                if (event) {
                    event->timestamp = time(NULL);
                    strcpy(event->source_namespace, source_namespace);
                    strcpy(event->source_name, source_name);
                    strcpy(event->target_namespace, target->target_namespace);
                    strcpy(event->target_name, target->target_name);
                    strcpy(event->event_type, "Propagated");
                }
                
                target->last_sync = time(NULL);
                target->sync_count++;
            }
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int config_propagation_sync_target(
    const char* source_namespace,
    const char* source_name,
    const char* source_kind,
    const char* target_namespace,
    const char* target_name) {
    
    if (!source_namespace || !source_name || !source_kind ||
        !target_namespace || !target_name) return -1;
    
    printf("[ConfigPropagation] Syncing %s.%s/%s -> %s/%s\n",
          source_namespace, source_kind, source_name, target_namespace, target_name);
    
    // This would copy/sync config from source to target
    return 0;
}

// ============ Target Management ============

int config_propagation_add_target(
    const char* source_namespace,
    const char* source_name,
    const char* source_kind,
    const char* target_namespace,
    const char* target_name,
    config_propagation_mode_t mode) {
    
    if (!source_namespace || !source_name || !source_kind ||
        !target_namespace || !target_name) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.rule_count; i++) {
        config_propagation_rule_t* rule = &g_controller.rules[i];
        
        if (strcmp(rule->source_namespace, source_namespace) == 0 &&
            strcmp(rule->source_name, source_name) == 0 &&
            strcmp(rule->source_kind, source_kind) == 0) {
            
            if (rule->target_count >= CONFIG_MAX_TARGETS) {
                pthread_mutex_unlock(&g_controller.lock);
                return -1;
            }
            
            config_target_t* target = &rule->targets[rule->target_count++];
            memset(target, 0, sizeof(config_target_t));
            
            strncpy(target->target_namespace, target_namespace, sizeof(target->target_namespace) - 1);
            strncpy(target->target_name, target_name, sizeof(target->target_name) - 1);
            target->mode = mode;
            target->enabled = 1;
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int config_propagation_remove_target(
    const char* source_namespace,
    const char* source_name,
    const char* source_kind,
    const char* target_namespace,
    const char* target_name) {
    
    if (!source_namespace || !source_name || !source_kind ||
        !target_namespace || !target_name) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.rule_count; i++) {
        config_propagation_rule_t* rule = &g_controller.rules[i];
        
        if (strcmp(rule->source_namespace, source_namespace) == 0 &&
            strcmp(rule->source_name, source_name) == 0 &&
            strcmp(rule->source_kind, source_kind) == 0) {
            
            for (int j = 0; j < rule->target_count; j++) {
                config_target_t* target = &rule->targets[j];
                
                if (strcmp(target->target_namespace, target_namespace) == 0 &&
                    strcmp(target->target_name, target_name) == 0) {
                    
                    if (j < rule->target_count - 1) {
                        memmove(target, &rule->targets[j + 1],
                               (rule->target_count - j - 1) * sizeof(config_target_t));
                    }
                    rule->target_count--;
                    
                    pthread_mutex_unlock(&g_controller.lock);
                    return 0;
                }
            }
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int config_propagation_list_targets(
    const char* source_namespace,
    const char* source_name,
    const char* source_kind,
    json_object** result) {
    
    if (!source_namespace || !source_name || !source_kind || !result) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    *result = json_object_new_array();
    
    for (int i = 0; i < g_controller.rule_count; i++) {
        config_propagation_rule_t* rule = &g_controller.rules[i];
        
        if (strcmp(rule->source_namespace, source_namespace) == 0 &&
            strcmp(rule->source_name, source_name) == 0 &&
            strcmp(rule->source_kind, source_kind) == 0) {
            
            for (int j = 0; j < rule->target_count; j++) {
                config_target_t* target = &rule->targets[j];
                
                json_object* item = json_object_new_object();
                json_object_object_add(item, "target_namespace", json_object_new_string(target->target_namespace));
                json_object_object_add(item, "target_name", json_object_new_string(target->target_name));
                json_object_object_add(item, "sync_count", json_object_new_int(target->sync_count));
                
                json_array_add(json_object_get_array(*result), item);
            }
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

// ============ Event Tracking ============

int config_propagation_list_events(
    const char* source_namespace,
    json_object** result) {
    
    if (!result) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    *result = json_object_new_array();
    
    for (int i = 0; i < g_controller.event_count; i++) {
        propagation_event_t* event = &g_controller.events[i];
        
        if (source_namespace == NULL || strcmp(event->source_namespace, source_namespace) == 0) {
            json_object* item = json_object_new_object();
            json_object_object_add(item, "source_namespace", json_object_new_string(event->source_namespace));
            json_object_object_add(item, "source_name", json_object_new_string(event->source_name));
            json_object_object_add(item, "target_namespace", json_object_new_string(event->target_namespace));
            json_object_object_add(item, "event_type", json_object_new_string(event->event_type));
            
            json_array_add(json_object_get_array(*result), item);
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return 0;
}

int config_propagation_get_propagation_status(
    const char* source_namespace,
    const char* source_name,
    const char* source_kind,
    json_object** result) {
    
    if (!source_namespace || !source_name || !source_kind || !result) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.rule_count; i++) {
        config_propagation_rule_t* rule = &g_controller.rules[i];
        
        if (strcmp(rule->source_namespace, source_namespace) == 0 &&
            strcmp(rule->source_name, source_name) == 0 &&
            strcmp(rule->source_kind, source_kind) == 0) {
            
            *result = json_object_new_object();
            json_object_object_add(*result, "enabled", json_object_new_int(rule->enabled));
            json_object_object_add(*result, "auto_sync", json_object_new_int(rule->auto_sync));
            json_object_object_add(*result, "target_count", json_object_new_int(rule->target_count));
            json_object_object_add(*result, "last_evaluated", json_object_new_int64(rule->last_evaluated));
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

// ============ Statistics ============

int config_propagation_get_statistics(
    int* total_rules,
    int* total_targets,
    int* successful_syncs,
    int* failed_syncs) {
    
    if (!total_rules || !total_targets || !successful_syncs || !failed_syncs) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    *total_rules = g_controller.rule_count;
    *total_targets = 0;
    *successful_syncs = g_controller.successful_propagations;
    *failed_syncs = g_controller.failed_propagations;
    
    for (int i = 0; i < g_controller.rule_count; i++) {
        *total_targets += g_controller.rules[i].target_count;
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return 0;
}

// ============ Background Thread ============

void* config_propagation_controller_thread(void* arg) {
    (void)arg;
    
    while (g_controller.running) {
        sleep(CONFIG_PROPAGATION_CHECK_INTERVAL);
        
        // Evaluate and execute propagation rules
        config_propagation_evaluate_rules();
        
        // Auto-sync enabled rules
        pthread_mutex_lock(&g_controller.lock);
        
        for (int i = 0; i < g_controller.rule_count; i++) {
            config_propagation_rule_t* rule = &g_controller.rules[i];
            
            if (rule->enabled && rule->auto_sync) {
                for (int j = 0; j < rule->target_count; j++) {
                    config_target_t* target = &rule->targets[j];
                    
                    if (target->enabled) {
                        config_propagation_sync_target(
                            rule->source_namespace,
                            rule->source_name,
                            rule->source_kind,
                            target->target_namespace,
                            target->target_name
                        );
                    }
                }
            }
        }
        
        pthread_mutex_unlock(&g_controller.lock);
    }
    
    return NULL;
}
