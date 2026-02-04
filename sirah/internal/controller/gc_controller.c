// internal/controller/gc_controller.c
// Garbage Collection Controller Implementation

#include "gc_controller.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

static gc_controller_t g_controller = {0};

// ============ Initialization & Cleanup ============

int gc_controller_init(void) {
    memset(&g_controller, 0, sizeof(gc_controller_t));
    pthread_mutex_init(&g_controller.lock, NULL);
    return 0;
}

int gc_controller_run(void) {
    g_controller.running = 1;
    
    // Start garbage collection thread
    if (pthread_create(&g_controller.gc_thread, NULL,
                      gc_controller_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create GC thread\n");
        return -1;
    }
    
    printf("Garbage Collection Controller started\n");
    return 0;
}

int gc_controller_shutdown(void) {
    g_controller.running = 0;
    pthread_join(g_controller.gc_thread, NULL);
    pthread_mutex_destroy(&g_controller.lock);
    return 0;
}

// ============ Object Tracking ============

int gc_register_object(const char* kind, const char* namespace, const char* name, const char* uid) {
    if (!kind || !namespace || !name || !uid) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    if (g_controller.object_count >= GC_MAX_OBJECTS) {
        pthread_mutex_unlock(&g_controller.lock);
        return -1;
    }
    
    gc_object_record_t* obj = &g_controller.objects[g_controller.object_count++];
    memset(obj, 0, sizeof(gc_object_record_t));
    
    strncpy(obj->kind, kind, sizeof(obj->kind) - 1);
    strncpy(obj->namespace, namespace, sizeof(obj->namespace) - 1);
    strncpy(obj->name, name, sizeof(obj->name) - 1);
    strncpy(obj->uid, uid, sizeof(obj->uid) - 1);
    
    obj->created_at = time(NULL);
    obj->propagation_policy = GC_PROPAGATION_BACKGROUND;
    
    pthread_mutex_unlock(&g_controller.lock);
    
    printf("[GC] Registered object: %s/%s.%s\n", namespace, name, kind);
    return 0;
}

int gc_unregister_object(const char* kind, const char* namespace, const char* name) {
    if (!kind || !namespace || !name) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.object_count; i++) {
        gc_object_record_t* obj = &g_controller.objects[i];
        if (strcmp(obj->kind, kind) == 0 &&
            strcmp(obj->namespace, namespace) == 0 &&
            strcmp(obj->name, name) == 0) {
            
            if (i < g_controller.object_count - 1) {
                memmove(obj, &g_controller.objects[i + 1],
                       (g_controller.object_count - i - 1) * sizeof(gc_object_record_t));
            }
            g_controller.object_count--;
            
            pthread_mutex_unlock(&g_controller.lock);
            printf("[GC] Unregistered object: %s/%s.%s\n", namespace, name, kind);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int gc_get_object_record(const char* kind, const char* namespace, const char* name,
                        gc_object_record_t* record) {
    if (!kind || !namespace || !name || !record) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.object_count; i++) {
        gc_object_record_t* obj = &g_controller.objects[i];
        if (strcmp(obj->kind, kind) == 0 &&
            strcmp(obj->namespace, namespace) == 0 &&
            strcmp(obj->name, name) == 0) {
            
            memcpy(record, obj, sizeof(gc_object_record_t));
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

// ============ Ownership Management ============

int gc_add_owner_reference(const char* kind, const char* namespace, const char* name,
                          const gc_owner_ref_t* owner_ref) {
    if (!kind || !namespace || !name || !owner_ref) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.object_count; i++) {
        gc_object_record_t* obj = &g_controller.objects[i];
        if (strcmp(obj->kind, kind) == 0 &&
            strcmp(obj->namespace, namespace) == 0 &&
            strcmp(obj->name, name) == 0) {
            
            if (obj->owner_count >= GC_MAX_OWNERSHIP_REFS) {
                pthread_mutex_unlock(&g_controller.lock);
                return -1;
            }
            
            memcpy(&obj->owner_refs[obj->owner_count++], owner_ref, sizeof(gc_owner_ref_t));
            obj->orphaned = 0;  // Has at least one owner now
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int gc_remove_owner_reference(const char* kind, const char* namespace, const char* name,
                             const char* owner_kind, const char* owner_name) {
    if (!kind || !namespace || !name || !owner_kind || !owner_name) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.object_count; i++) {
        gc_object_record_t* obj = &g_controller.objects[i];
        if (strcmp(obj->kind, kind) == 0 &&
            strcmp(obj->namespace, namespace) == 0 &&
            strcmp(obj->name, name) == 0) {
            
            // Find and remove the owner reference
            for (int j = 0; j < obj->owner_count; j++) {
                if (strcmp(obj->owner_refs[j].owner_kind, owner_kind) == 0 &&
                    strcmp(obj->owner_refs[j].owner_name, owner_name) == 0) {
                    
                    if (j < obj->owner_count - 1) {
                        memmove(&obj->owner_refs[j], &obj->owner_refs[j + 1],
                               (obj->owner_count - j - 1) * sizeof(gc_owner_ref_t));
                    }
                    obj->owner_count--;
                    
                    // If no owners remain, mark as orphaned
                    if (obj->owner_count == 0) {
                        obj->orphaned = 1;
                    }
                    
                    pthread_mutex_unlock(&g_controller.lock);
                    return 0;
                }
            }
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int gc_get_object_owners(const char* kind, const char* namespace, const char* name,
                        gc_owner_ref_t** owners, int* count) {
    if (!kind || !namespace || !name || !owners || !count) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.object_count; i++) {
        gc_object_record_t* obj = &g_controller.objects[i];
        if (strcmp(obj->kind, kind) == 0 &&
            strcmp(obj->namespace, namespace) == 0 &&
            strcmp(obj->name, name) == 0) {
            
            *count = obj->owner_count;
            if (obj->owner_count > 0) {
                *owners = (gc_owner_ref_t*)malloc(obj->owner_count * sizeof(gc_owner_ref_t));
                memcpy(*owners, obj->owner_refs, obj->owner_count * sizeof(gc_owner_ref_t));
            } else {
                *owners = NULL;
            }
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

// ============ Finalizer Support ============

int gc_add_finalizer(const char* kind, const char* namespace, const char* name,
                    const char* finalizer) {
    if (!kind || !namespace || !name || !finalizer) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.object_count; i++) {
        gc_object_record_t* obj = &g_controller.objects[i];
        if (strcmp(obj->kind, kind) == 0 &&
            strcmp(obj->namespace, namespace) == 0 &&
            strcmp(obj->name, name) == 0) {
            
            if (obj->finalizer_count >= 10) {
                pthread_mutex_unlock(&g_controller.lock);
                return -1;
            }
            
            gc_finalizer_t* fin = &obj->finalizers[obj->finalizer_count++];
            strncpy(fin->finalizer, finalizer, sizeof(fin->finalizer) - 1);
            fin->blocking = 1;
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int gc_remove_finalizer(const char* kind, const char* namespace, const char* name,
                       const char* finalizer) {
    if (!kind || !namespace || !name || !finalizer) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.object_count; i++) {
        gc_object_record_t* obj = &g_controller.objects[i];
        if (strcmp(obj->kind, kind) == 0 &&
            strcmp(obj->namespace, namespace) == 0 &&
            strcmp(obj->name, name) == 0) {
            
            for (int j = 0; j < obj->finalizer_count; j++) {
                if (strcmp(obj->finalizers[j].finalizer, finalizer) == 0) {
                    if (j < obj->finalizer_count - 1) {
                        memmove(&obj->finalizers[j], &obj->finalizers[j + 1],
                               (obj->finalizer_count - j - 1) * sizeof(gc_finalizer_t));
                    }
                    obj->finalizer_count--;
                    
                    pthread_mutex_unlock(&g_controller.lock);
                    return 0;
                }
            }
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int gc_get_finalizers(const char* kind, const char* namespace, const char* name,
                     char*** finalizers, int* count) {
    if (!kind || !namespace || !name || !finalizers || !count) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.object_count; i++) {
        gc_object_record_t* obj = &g_controller.objects[i];
        if (strcmp(obj->kind, kind) == 0 &&
            strcmp(obj->namespace, namespace) == 0 &&
            strcmp(obj->name, name) == 0) {
            
            *count = obj->finalizer_count;
            if (obj->finalizer_count > 0) {
                *finalizers = (char**)malloc(obj->finalizer_count * sizeof(char*));
                for (int j = 0; j < obj->finalizer_count; j++) {
                    (*finalizers)[j] = strdup(obj->finalizers[j].finalizer);
                }
            } else {
                *finalizers = NULL;
            }
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int gc_has_finalizers(const char* kind, const char* namespace, const char* name) {
    if (!kind || !namespace || !name) return 0;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.object_count; i++) {
        gc_object_record_t* obj = &g_controller.objects[i];
        if (strcmp(obj->kind, kind) == 0 &&
            strcmp(obj->namespace, namespace) == 0 &&
            strcmp(obj->name, name) == 0) {
            
            int result = obj->finalizer_count > 0 ? 1 : 0;
            pthread_mutex_unlock(&g_controller.lock);
            return result;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return 0;
}

// ============ Deletion Cascade ============

int gc_mark_for_deletion(const char* kind, const char* namespace, const char* name,
                        int grace_seconds, gc_propagation_policy_t propagation) {
    if (!kind || !namespace || !name) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.object_count; i++) {
        gc_object_record_t* obj = &g_controller.objects[i];
        if (strcmp(obj->kind, kind) == 0 &&
            strcmp(obj->namespace, namespace) == 0 &&
            strcmp(obj->name, name) == 0) {
            
            obj->pending_deletion = 1;
            obj->deletion_timestamp = time(NULL);
            obj->deletion_grace_seconds = grace_seconds;
            obj->propagation_policy = propagation;
            
            if (propagation == GC_PROPAGATION_FOREGROUND) {
                obj->deletion_strategy = GC_DELETE_WITH_GRACE_PERIOD;
            } else {
                obj->deletion_strategy = GC_DELETE_IMMEDIATE;
            }
            
            printf("[GC] Marked for deletion: %s/%s.%s (propagation: %d)\n",
                  namespace, name, kind, propagation);
            
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int gc_start_deletion_cascade(const char* kind, const char* namespace, const char* name,
                             json_object** cascade_list) {
    if (!kind || !namespace || !name || !cascade_list) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    *cascade_list = json_object_new_array();
    int cascade_count = 0;
    
    // Find all objects owned by this object
    for (int i = 0; i < g_controller.object_count; i++) {
        gc_object_record_t* obj = &g_controller.objects[i];
        
        for (int j = 0; j < obj->owner_count; j++) {
            if (strcmp(obj->owner_refs[j].owner_kind, kind) == 0 &&
                strcmp(obj->owner_refs[j].owner_name, name) == 0) {
                
                json_object* item = json_object_new_object();
                json_object_object_add(item, "kind", json_object_new_string(obj->kind));
                json_object_object_add(item, "namespace", json_object_new_string(obj->namespace));
                json_object_object_add(item, "name", json_object_new_string(obj->name));
                json_array_add(json_object_get_array(*cascade_list), item);
                
                cascade_count++;
            }
        }
    }
    
    printf("[GC] Cascade: %d objects will be deleted\n", cascade_count);
    pthread_mutex_unlock(&g_controller.lock);
    return cascade_count;
}

int gc_get_dependent_objects(const char* kind, const char* namespace, const char* name,
                            char*** dependent_kinds, char*** dependent_names, 
                            char*** dependent_namespaces, int* count) {
    if (!kind || !namespace || !name || !count) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    *count = 0;
    
    // Count dependencies first
    int dep_count = 0;
    for (int i = 0; i < g_controller.object_count; i++) {
        gc_object_record_t* obj = &g_controller.objects[i];
        for (int j = 0; j < obj->owner_count; j++) {
            if (strcmp(obj->owner_refs[j].owner_kind, kind) == 0 &&
                strcmp(obj->owner_refs[j].owner_name, name) == 0) {
                dep_count++;
            }
        }
    }
    
    if (dep_count == 0) {
        pthread_mutex_unlock(&g_controller.lock);
        return 0;
    }
    
    // Allocate arrays
    *dependent_kinds = (char**)malloc(dep_count * sizeof(char*));
    *dependent_names = (char**)malloc(dep_count * sizeof(char*));
    *dependent_namespaces = (char**)malloc(dep_count * sizeof(char*));
    
    // Fill arrays
    *count = 0;
    for (int i = 0; i < g_controller.object_count; i++) {
        gc_object_record_t* obj = &g_controller.objects[i];
        for (int j = 0; j < obj->owner_count; j++) {
            if (strcmp(obj->owner_refs[j].owner_kind, kind) == 0 &&
                strcmp(obj->owner_refs[j].owner_name, name) == 0) {
                
                (*dependent_kinds)[*count] = strdup(obj->kind);
                (*dependent_names)[*count] = strdup(obj->name);
                (*dependent_namespaces)[*count] = strdup(obj->namespace);
                (*count)++;
            }
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return 0;
}

// ============ Orphaned Object Operations ============

int gc_get_orphaned_objects(const char* kind, json_object** result) {
    if (!result) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    *result = json_object_new_array();
    
    for (int i = 0; i < g_controller.object_count; i++) {
        gc_object_record_t* obj = &g_controller.objects[i];
        
        if ((kind == NULL || strcmp(obj->kind, kind) == 0) && obj->orphaned) {
            json_object* item = json_object_new_object();
            json_object_object_add(item, "kind", json_object_new_string(obj->kind));
            json_object_object_add(item, "namespace", json_object_new_string(obj->namespace));
            json_object_object_add(item, "name", json_object_new_string(obj->name));
            json_object_object_add(item, "uid", json_object_new_string(obj->uid));
            
            json_array_add(json_object_get_array(*result), item);
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return 0;
}

int gc_cleanup_orphaned_objects(const char* kind, int* cleanup_count) {
    if (!cleanup_count) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    *cleanup_count = 0;
    
    // Remove orphaned objects that have passed grace period
    time_t now = time(NULL);
    for (int i = g_controller.object_count - 1; i >= 0; i--) {
        gc_object_record_t* obj = &g_controller.objects[i];
        
        if (obj->orphaned && (kind == NULL || strcmp(obj->kind, kind) == 0)) {
            if (obj->pending_deletion && 
               (now - obj->deletion_timestamp >= obj->deletion_grace_seconds)) {
                
                printf("[GC] Cleaning up orphaned object: %s/%s.%s\n",
                      obj->namespace, obj->name, obj->kind);
                
                if (i < g_controller.object_count - 1) {
                    memmove(obj, &g_controller.objects[i + 1],
                           (g_controller.object_count - i - 1) * sizeof(gc_object_record_t));
                }
                g_controller.object_count--;
                (*cleanup_count)++;
            }
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return 0;
}

// ============ Propagation Policy ============

int gc_set_propagation_policy(const char* kind, const char* namespace, const char* name,
                             gc_propagation_policy_t policy) {
    if (!kind || !namespace || !name) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.object_count; i++) {
        gc_object_record_t* obj = &g_controller.objects[i];
        if (strcmp(obj->kind, kind) == 0 &&
            strcmp(obj->namespace, namespace) == 0 &&
            strcmp(obj->name, name) == 0) {
            
            obj->propagation_policy = policy;
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

int gc_get_propagation_policy(const char* kind, const char* namespace, const char* name,
                             gc_propagation_policy_t* policy) {
    if (!kind || !namespace || !name || !policy) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    for (int i = 0; i < g_controller.object_count; i++) {
        gc_object_record_t* obj = &g_controller.objects[i];
        if (strcmp(obj->kind, kind) == 0 &&
            strcmp(obj->namespace, namespace) == 0 &&
            strcmp(obj->name, name) == 0) {
            
            *policy = obj->propagation_policy;
            pthread_mutex_unlock(&g_controller.lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return -1;
}

// ============ Statistics ============

int gc_get_statistics(int* total_objects, int* orphaned_objects, 
                     int* pending_deletion, time_t* last_scan) {
    if (!total_objects || !orphaned_objects || !pending_deletion) return -1;
    
    pthread_mutex_lock(&g_controller.lock);
    
    *total_objects = g_controller.object_count;
    *orphaned_objects = 0;
    *pending_deletion = 0;
    *last_scan = g_controller.last_scan;
    
    for (int i = 0; i < g_controller.object_count; i++) {
        if (g_controller.objects[i].orphaned) {
            (*orphaned_objects)++;
        }
        if (g_controller.objects[i].pending_deletion) {
            (*pending_deletion)++;
        }
    }
    
    pthread_mutex_unlock(&g_controller.lock);
    return 0;
}

// ============ Background Thread ============

void* gc_controller_thread(void* arg) {
    (void)arg;
    
    while (g_controller.running) {
        sleep(GC_SCAN_INTERVAL);
        
        // Run garbage collection scan
        pthread_mutex_lock(&g_controller.lock);
        g_controller.last_scan = time(NULL);
        
        // Clean up orphaned objects that have passed grace period
        time_t now = time(NULL);
        for (int i = g_controller.object_count - 1; i >= 0; i--) {
            gc_object_record_t* obj = &g_controller.objects[i];
            
            if (obj->pending_deletion && obj->orphaned &&
               (now - obj->deletion_timestamp >= obj->deletion_grace_seconds)) {
                
                if (i < g_controller.object_count - 1) {
                    memmove(obj, &g_controller.objects[i + 1],
                           (g_controller.object_count - i - 1) * sizeof(gc_object_record_t));
                }
                g_controller.object_count--;
                g_controller.objects_cleaned++;
            }
        }
        
        g_controller.objects_scanned = g_controller.object_count;
        pthread_mutex_unlock(&g_controller.lock);
    }
    
    return NULL;
}
