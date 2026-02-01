// Week 5: ConfigMap, Secret, PV, PVC, StatefulSet endpoints

#include <stdio.h>
#include <string.h>
#include <json-c/json.h>
#include "types/config.h"
#include "types/storage.h"

// Global stores for Week 5 resources
static struct {
    k8s_configmap_t* configmaps[1000];
    int count;
} configmap_store = {0};

static struct {
    k8s_secret_t* secrets[1000];
    int count;
} secret_store = {0};

static struct {
    k8s_persistent_volume_t* volumes[1000];
    int count;
} pv_store = {0};

static struct {
    k8s_persistent_volume_claim_t* claims[1000];
    int count;
} pvc_store = {0};

static struct {
    k8s_statefulset_t* statefulsets[1000];
    int count;
} statefulset_store = {0};

// ============ ConfigMap Endpoints ============

int endpoint_list_configmaps(const char* namespace, char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object* items = json_object_new_array();

    for (int i = 0; i < configmap_store.count; i++) {
        if (strlen(namespace) == 0 || strcmp(configmap_store.configmaps[i]->metadata.namespace, namespace) == 0) {
            char* cm_json = k8s_configmap_to_json(configmap_store.configmaps[i]);
            json_object* cm_obj = json_tokener_parse(cm_json);
            if (cm_obj) json_object_array_add(items, cm_obj);
            free(cm_json);
        }
    }

    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("ConfigMapList"));
    json_object_object_add(root, "items", items);

    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_get_configmap(const char* namespace, const char* name,
                          char* response_buffer, int* response_code) {
    for (int i = 0; i < configmap_store.count; i++) {
        k8s_configmap_t* cm = configmap_store.configmaps[i];
        if (strcmp(cm->metadata.name, name) == 0 && strcmp(cm->metadata.namespace, namespace) == 0) {
            char* json_str = k8s_configmap_to_json(cm);
            strcpy(response_buffer, json_str);
            free(json_str);
            *response_code = 200;
            return 0;
        }
    }
    *response_code = 404;
    strcpy(response_buffer, "{\"error\": \"ConfigMap not found\"}");
    return -1;
}

int endpoint_create_configmap(const char* namespace, const char* body,
                             char* response_buffer, int* response_code) {
    if (configmap_store.count >= 1000) {
        *response_code = 500;
        strcpy(response_buffer, "{\"error\": \"ConfigMap store full\"}");
        return -1;
    }

    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\": \"Invalid JSON\"}");
        return -1;
    }

    json_object* meta_obj = json_object_object_get(obj, "metadata");
    const char* name = json_object_get_string(json_object_object_get(meta_obj, "name"));

    if (!name) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\": \"Missing name in metadata\"}");
        json_object_put(obj);
        return -1;
    }

    k8s_configmap_t* cm = k8s_configmap_new(name, (char*)namespace);
    configmap_store.configmaps[configmap_store.count++] = cm;

    // Parse data from body
    json_object* data_obj = json_object_object_get(obj, "data");
    if (data_obj) {
        json_object_iter iter;
        json_object_object_foreachC(data_obj, iter) {
            const char* value = json_object_get_string(iter.val);
            k8s_configmap_set(cm, iter.key, (char*)value);
        }
    }

    char* response_json = k8s_configmap_to_json(cm);
    strcpy(response_buffer, response_json);
    free(response_json);

    *response_code = 201;
    json_object_put(obj);
    return 0;
}

int endpoint_delete_configmap(const char* namespace, const char* name,
                             char* response_buffer, int* response_code) {
    for (int i = 0; i < configmap_store.count; i++) {
        k8s_configmap_t* cm = configmap_store.configmaps[i];
        if (strcmp(cm->metadata.name, name) == 0 && strcmp(cm->metadata.namespace, namespace) == 0) {
            // Remove from store
            for (int j = i; j < configmap_store.count - 1; j++) {
                configmap_store.configmaps[j] = configmap_store.configmaps[j + 1];
            }
            configmap_store.count--;

            k8s_configmap_free(cm);
            *response_code = 204;
            strcpy(response_buffer, "");
            return 0;
        }
    }
    *response_code = 404;
    strcpy(response_buffer, "{\"error\": \"ConfigMap not found\"}");
    return -1;
}

// ============ Secret Endpoints ============

int endpoint_list_secrets(const char* namespace, char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object* items = json_object_new_array();

    for (int i = 0; i < secret_store.count; i++) {
        if (strlen(namespace) == 0 || strcmp(secret_store.secrets[i]->metadata.namespace, namespace) == 0) {
            char* secret_json = k8s_secret_to_json(secret_store.secrets[i]);
            json_object* secret_obj = json_tokener_parse(secret_json);
            if (secret_obj) json_object_array_add(items, secret_obj);
            free(secret_json);
        }
    }

    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("SecretList"));
    json_object_object_add(root, "items", items);

    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_get_secret(const char* namespace, const char* name,
                       char* response_buffer, int* response_code) {
    for (int i = 0; i < secret_store.count; i++) {
        k8s_secret_t* secret = secret_store.secrets[i];
        if (strcmp(secret->metadata.name, name) == 0 && strcmp(secret->metadata.namespace, namespace) == 0) {
            char* json_str = k8s_secret_to_json(secret);
            strcpy(response_buffer, json_str);
            free(json_str);
            *response_code = 200;
            return 0;
        }
    }
    *response_code = 404;
    strcpy(response_buffer, "{\"error\": \"Secret not found\"}");
    return -1;
}

int endpoint_create_secret(const char* namespace, const char* body,
                          char* response_buffer, int* response_code) {
    if (secret_store.count >= 1000) {
        *response_code = 500;
        strcpy(response_buffer, "{\"error\": \"Secret store full\"}");
        return -1;
    }

    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\": \"Invalid JSON\"}");
        return -1;
    }

    json_object* meta_obj = json_object_object_get(obj, "metadata");
    const char* name = json_object_get_string(json_object_object_get(meta_obj, "name"));
    json_object* type_obj = json_object_object_get(obj, "type");
    const char* type = type_obj ? json_object_get_string(type_obj) : "Opaque";

    if (!name) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\": \"Missing name in metadata\"}");
        json_object_put(obj);
        return -1;
    }

    k8s_secret_t* secret = k8s_secret_new(name, (char*)namespace, (char*)type);
    secret_store.secrets[secret_store.count++] = secret;

    // Parse data from body
    json_object* data_obj = json_object_object_get(obj, "data");
    if (data_obj) {
        json_object_iter iter;
        json_object_object_foreachC(data_obj, iter) {
            const char* value = json_object_get_string(iter.val);
            k8s_secret_set(secret, iter.key, (char*)value);
        }
    }

    char* response_json = k8s_secret_to_json(secret);
    strcpy(response_buffer, response_json);
    free(response_json);

    *response_code = 201;
    json_object_put(obj);
    return 0;
}

int endpoint_delete_secret(const char* namespace, const char* name,
                          char* response_buffer, int* response_code) {
    for (int i = 0; i < secret_store.count; i++) {
        k8s_secret_t* secret = secret_store.secrets[i];
        if (strcmp(secret->metadata.name, name) == 0 && strcmp(secret->metadata.namespace, namespace) == 0) {
            // Remove from store
            for (int j = i; j < secret_store.count - 1; j++) {
                secret_store.secrets[j] = secret_store.secrets[j + 1];
            }
            secret_store.count--;

            k8s_secret_free(secret);
            *response_code = 204;
            strcpy(response_buffer, "");
            return 0;
        }
    }
    *response_code = 404;
    strcpy(response_buffer, "{\"error\": \"Secret not found\"}");
    return -1;
}

// ============ PersistentVolume Endpoints ============

int endpoint_list_pv(char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object* items = json_object_new_array();

    for (int i = 0; i < pv_store.count; i++) {
        char* pv_json = k8s_pv_to_json(pv_store.volumes[i]);
        json_object* pv_obj = json_tokener_parse(pv_json);
        if (pv_obj) json_object_array_add(items, pv_obj);
        free(pv_json);
    }

    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("PersistentVolumeList"));
    json_object_object_add(root, "items", items);

    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_get_pv(const char* name, char* response_buffer, int* response_code) {
    for (int i = 0; i < pv_store.count; i++) {
        k8s_persistent_volume_t* pv = pv_store.volumes[i];
        if (strcmp(pv->metadata.name, name) == 0) {
            char* json_str = k8s_pv_to_json(pv);
            strcpy(response_buffer, json_str);
            free(json_str);
            *response_code = 200;
            return 0;
        }
    }
    *response_code = 404;
    strcpy(response_buffer, "{\"error\": \"PersistentVolume not found\"}");
    return -1;
}

int endpoint_create_pv(const char* body, char* response_buffer, int* response_code) {
    if (pv_store.count >= 1000) {
        *response_code = 500;
        strcpy(response_buffer, "{\"error\": \"PV store full\"}");
        return -1;
    }

    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\": \"Invalid JSON\"}");
        return -1;
    }

    json_object* meta_obj = json_object_object_get(obj, "metadata");
    const char* name = json_object_get_string(json_object_object_get(meta_obj, "name"));

    if (!name) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\": \"Missing name in metadata\"}");
        json_object_put(obj);
        return -1;
    }

    k8s_persistent_volume_t* pv = k8s_pv_new(name);
    pv_store.volumes[pv_store.count++] = pv;

    // Parse spec from body
    json_object* spec_obj = json_object_object_get(obj, "spec");
    if (spec_obj) {
        json_object* type_obj = json_object_object_get(spec_obj, "type");
        if (type_obj) {
            free(pv->spec.type);
            pv->spec.type = strdup(json_object_get_string(type_obj));
        }
        json_object* path_obj = json_object_object_get(spec_obj, "path");
        if (path_obj) {
            free(pv->spec.path);
            pv->spec.path = strdup(json_object_get_string(path_obj));
        }
    }

    char* response_json = k8s_pv_to_json(pv);
    strcpy(response_buffer, response_json);
    free(response_json);

    *response_code = 201;
    json_object_put(obj);
    return 0;
}

int endpoint_delete_pv(const char* name, char* response_buffer, int* response_code) {
    for (int i = 0; i < pv_store.count; i++) {
        k8s_persistent_volume_t* pv = pv_store.volumes[i];
        if (strcmp(pv->metadata.name, name) == 0) {
            // Remove from store
            for (int j = i; j < pv_store.count - 1; j++) {
                pv_store.volumes[j] = pv_store.volumes[j + 1];
            }
            pv_store.count--;

            k8s_pv_free(pv);
            *response_code = 204;
            strcpy(response_buffer, "");
            return 0;
        }
    }
    *response_code = 404;
    strcpy(response_buffer, "{\"error\": \"PersistentVolume not found\"}");
    return -1;
}

// ============ PersistentVolumeClaim Endpoints ============

int endpoint_list_pvc(const char* namespace, char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object* items = json_object_new_array();

    for (int i = 0; i < pvc_store.count; i++) {
        if (strlen(namespace) == 0 || strcmp(pvc_store.claims[i]->metadata.namespace, namespace) == 0) {
            char* pvc_json = k8s_pvc_to_json(pvc_store.claims[i]);
            json_object* pvc_obj = json_tokener_parse(pvc_json);
            if (pvc_obj) json_object_array_add(items, pvc_obj);
            free(pvc_json);
        }
    }

    json_object_object_add(root, "apiVersion", json_object_new_string("v1"));
    json_object_object_add(root, "kind", json_object_new_string("PersistentVolumeClaimList"));
    json_object_object_add(root, "items", items);

    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_get_pvc(const char* namespace, const char* name,
                    char* response_buffer, int* response_code) {
    for (int i = 0; i < pvc_store.count; i++) {
        k8s_persistent_volume_claim_t* pvc = pvc_store.claims[i];
        if (strcmp(pvc->metadata.name, name) == 0 && strcmp(pvc->metadata.namespace, namespace) == 0) {
            char* json_str = k8s_pvc_to_json(pvc);
            strcpy(response_buffer, json_str);
            free(json_str);
            *response_code = 200;
            return 0;
        }
    }
    *response_code = 404;
    strcpy(response_buffer, "{\"error\": \"PersistentVolumeClaim not found\"}");
    return -1;
}

int endpoint_create_pvc(const char* namespace, const char* body,
                       char* response_buffer, int* response_code) {
    if (pvc_store.count >= 1000) {
        *response_code = 500;
        strcpy(response_buffer, "{\"error\": \"PVC store full\"}");
        return -1;
    }

    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\": \"Invalid JSON\"}");
        return -1;
    }

    json_object* meta_obj = json_object_object_get(obj, "metadata");
    const char* name = json_object_get_string(json_object_object_get(meta_obj, "name"));

    if (!name) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\": \"Missing name in metadata\"}");
        json_object_put(obj);
        return -1;
    }

    k8s_persistent_volume_claim_t* pvc = k8s_pvc_new(name, (char*)namespace);
    pvc_store.claims[pvc_store.count++] = pvc;

    char* response_json = k8s_pvc_to_json(pvc);
    strcpy(response_buffer, response_json);
    free(response_json);

    *response_code = 201;
    json_object_put(obj);
    return 0;
}

int endpoint_delete_pvc(const char* namespace, const char* name,
                       char* response_buffer, int* response_code) {
    for (int i = 0; i < pvc_store.count; i++) {
        k8s_persistent_volume_claim_t* pvc = pvc_store.claims[i];
        if (strcmp(pvc->metadata.name, name) == 0 && strcmp(pvc->metadata.namespace, namespace) == 0) {
            // Remove from store
            for (int j = i; j < pvc_store.count - 1; j++) {
                pvc_store.claims[j] = pvc_store.claims[j + 1];
            }
            pvc_store.count--;

            k8s_pvc_free(pvc);
            *response_code = 204;
            strcpy(response_buffer, "");
            return 0;
        }
    }
    *response_code = 404;
    strcpy(response_buffer, "{\"error\": \"PersistentVolumeClaim not found\"}");
    return -1;
}

// ============ StatefulSet Endpoints ============

int endpoint_list_statefulsets(const char* namespace, char* response_buffer, int* response_code) {
    json_object* root = json_object_new_object();
    json_object* items = json_object_new_array();

    for (int i = 0; i < statefulset_store.count; i++) {
        if (strlen(namespace) == 0 || strcmp(statefulset_store.statefulsets[i]->metadata.namespace, namespace) == 0) {
            char* sts_json = k8s_statefulset_to_json(statefulset_store.statefulsets[i]);
            json_object* sts_obj = json_tokener_parse(sts_json);
            if (sts_obj) json_object_array_add(items, sts_obj);
            free(sts_json);
        }
    }

    json_object_object_add(root, "apiVersion", json_object_new_string("apps/v1"));
    json_object_object_add(root, "kind", json_object_new_string("StatefulSetList"));
    json_object_object_add(root, "items", items);

    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);
    *response_code = 200;
    json_object_put(root);
    return 0;
}

int endpoint_get_statefulset(const char* namespace, const char* name,
                            char* response_buffer, int* response_code) {
    for (int i = 0; i < statefulset_store.count; i++) {
        k8s_statefulset_t* sts = statefulset_store.statefulsets[i];
        if (strcmp(sts->metadata.name, name) == 0 && strcmp(sts->metadata.namespace, namespace) == 0) {
            char* json_str = k8s_statefulset_to_json(sts);
            strcpy(response_buffer, json_str);
            free(json_str);
            *response_code = 200;
            return 0;
        }
    }
    *response_code = 404;
    strcpy(response_buffer, "{\"error\": \"StatefulSet not found\"}");
    return -1;
}

int endpoint_create_statefulset(const char* namespace, const char* body,
                               char* response_buffer, int* response_code) {
    if (statefulset_store.count >= 1000) {
        *response_code = 500;
        strcpy(response_buffer, "{\"error\": \"StatefulSet store full\"}");
        return -1;
    }

    json_object* obj = json_tokener_parse(body);
    if (!obj) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\": \"Invalid JSON\"}");
        return -1;
    }

    json_object* meta_obj = json_object_object_get(obj, "metadata");
    const char* name = json_object_get_string(json_object_object_get(meta_obj, "name"));

    if (!name) {
        *response_code = 400;
        strcpy(response_buffer, "{\"error\": \"Missing name in metadata\"}");
        json_object_put(obj);
        return -1;
    }

    k8s_statefulset_t* sts = k8s_statefulset_new(name, (char*)namespace);
    statefulset_store.statefulsets[statefulset_store.count++] = sts;

    // Parse spec from body
    json_object* spec_obj = json_object_object_get(obj, "spec");
    if (spec_obj) {
        json_object* replicas_obj = json_object_object_get(spec_obj, "replicas");
        if (replicas_obj) {
            sts->spec.replicas = json_object_get_int(replicas_obj);
        }
        json_object* service_obj = json_object_object_get(spec_obj, "serviceName");
        if (service_obj) {
            free(sts->spec.service_name);
            sts->spec.service_name = strdup(json_object_get_string(service_obj));
        }
    }

    char* response_json = k8s_statefulset_to_json(sts);
    strcpy(response_buffer, response_json);
    free(response_json);

    *response_code = 201;
    json_object_put(obj);
    return 0;
}

int endpoint_delete_statefulset(const char* namespace, const char* name,
                               char* response_buffer, int* response_code) {
    for (int i = 0; i < statefulset_store.count; i++) {
        k8s_statefulset_t* sts = statefulset_store.statefulsets[i];
        if (strcmp(sts->metadata.name, name) == 0 && strcmp(sts->metadata.namespace, namespace) == 0) {
            // Remove from store
            for (int j = i; j < statefulset_store.count - 1; j++) {
                statefulset_store.statefulsets[j] = statefulset_store.statefulsets[j + 1];
            }
            statefulset_store.count--;

            k8s_statefulset_free(sts);
            *response_code = 204;
            strcpy(response_buffer, "");
            return 0;
        }
    }
    *response_code = 404;
    strcpy(response_buffer, "{\"error\": \"StatefulSet not found\"}");
    return -1;
}
// ============ ConfigMap Patch Endpoint ============
// Already implemented in endpoints.c - no need to redefine

// ============ Secret Patch Endpoint ============
// Already implemented in endpoints.c - no need to redefine

// ============ Event Endpoints ============
// Already implemented in endpoints.c - no need to redefine

