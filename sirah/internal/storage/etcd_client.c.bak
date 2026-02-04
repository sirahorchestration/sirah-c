// internal/storage/etcd_client.c
#include "etcd_client.h"
#include "store.h"
#include "../../pkg/types/pod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <json-c/json.h>

#define ETCD_API_PREFIX "/v3/kv"
#define ETCD_PUT_PATH "/v3/kv/put"
#define ETCD_GET_PATH "/v3/kv/range"
#define ETCD_DELETE_PATH "/v3/kv/deleterange"
#define SIRAH_POD_PREFIX "/sirah/pods/"

// etcd client structure
typedef struct etcd_client {
    char* addr;
    CURL* curl_handle;
    int connected;
} etcd_client_t;

// Callback for curl write operations
typedef struct {
    char* data;
    size_t size;
} curl_response_t;

static size_t etcd_curl_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    curl_response_t* mem = (curl_response_t*)userp;

    char* ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) {
        fprintf(stderr, "Not enough memory for curl response\n");
        return 0;
    }

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;

    return realsize;
}

// Helper: base64 encode (simplified - only for ASCII strings)
static char* base64_encode(const char* input) {
    if (!input) return NULL;
    
    static const char* base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    size_t input_len = strlen(input);
    size_t output_len = ((input_len + 2) / 3) * 4 + 1;
    char* output = (char*)malloc(output_len);
    if (!output) return NULL;
    
    int i = 0, j = 0;
    unsigned char byte1, byte2, byte3;
    
    while (i < (int)input_len) {
        byte1 = (unsigned char)input[i++];
        byte2 = i < (int)input_len ? (unsigned char)input[i++] : 0;
        byte3 = i < (int)input_len ? (unsigned char)input[i++] : 0;
        
        int b = (byte1 << 16) | (byte2 << 8) | byte3;
        
        output[j++] = base64_chars[(b >> 18) & 0x3F];
        output[j++] = base64_chars[(b >> 12) & 0x3F];
        output[j++] = i - 2 < (int)input_len ? base64_chars[(b >> 6) & 0x3F] : '=';
        output[j++] = i - 1 < (int)input_len ? base64_chars[b & 0x3F] : '=';
    }
    output[j] = '\0';
    return output;
}

// Helper: base64 decode (simplified)
static char* base64_decode(const char* input) {
    if (!input) return NULL;
    
    static const char* base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    size_t input_len = strlen(input);
    char* output = (char*)malloc(input_len + 1);
    if (!output) return NULL;
    
    int i = 0, j = 0;
    unsigned char byte1, byte2, byte3, byte4;
    
    while (i < (int)input_len) {
        byte1 = input[i++];
        byte2 = i < (int)input_len ? input[i++] : 'A';
        byte3 = i < (int)input_len ? input[i++] : 'A';
        byte4 = i < (int)input_len ? input[i++] : 'A';
        
        int index1 = strchr(base64_chars, byte1) - base64_chars;
        int index2 = strchr(base64_chars, byte2) - base64_chars;
        int index3 = strchr(base64_chars, byte3) - base64_chars;
        int index4 = strchr(base64_chars, byte4) - base64_chars;
        
        int b = (index1 << 18) | (index2 << 12) | (index3 << 6) | index4;
        
        output[j++] = (b >> 16) & 0xFF;
        if (byte3 != '=') output[j++] = (b >> 8) & 0xFF;
        if (byte4 != '=') output[j++] = b & 0xFF;
    }
    output[j] = '\0';
    return output;
}

etcd_client_t* etcd_connect(const char* addr) {
    if (!addr) {
        fprintf(stderr, "etcd address required\n");
        return NULL;
    }

    etcd_client_t* client = (etcd_client_t*)malloc(sizeof(etcd_client_t));
    if (!client) return NULL;

    client->addr = (char*)malloc(strlen(addr) + 1);
    if (!client->addr) {
        free(client);
        return NULL;
    }
    strcpy(client->addr, addr);

    client->curl_handle = curl_easy_init();
    if (!client->curl_handle) {
        free(client->addr);
        free(client);
        return NULL;
    }

    // Test connection
    char url[1024];
    snprintf(url, sizeof(url), "http://%s/version", client->addr);
    
    curl_easy_setopt(client->curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(client->curl_handle, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(client->curl_handle, CURLOPT_CONNECTTIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(client->curl_handle);
    if (res != CURLE_OK) {
        fprintf(stderr, "Failed to connect to etcd: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(client->curl_handle);
        free(client->addr);
        free(client);
        return NULL;
    }

    client->connected = 1;
    printf("Connected to etcd at %s\n", addr);
    return client;
}

void etcd_disconnect(etcd_client_t* client) {
    if (!client) return;
    if (client->curl_handle) {
        curl_easy_cleanup(client->curl_handle);
    }
    free(client->addr);
    free(client);
}

int etcd_is_connected(etcd_client_t* client) {
    if (!client) return 0;
    return client->connected;
}

int etcd_put(etcd_client_t* client, const char* key, const char* value) {
    if (!client || !key || !value) return -1;
    if (!client->connected) return -1;

    char url[1024];
    snprintf(url, sizeof(url), "http://%s%s", client->addr, ETCD_PUT_PATH);

    // Prepare JSON body with base64 encoded key and value
    char* encoded_key = base64_encode(key);
    char* encoded_value = base64_encode(value);
    
    char json_body[16384];
    snprintf(json_body, sizeof(json_body), 
             "{\"key\":\"%s\",\"value\":\"%s\"}",
             encoded_key ? encoded_key : "",
             encoded_value ? encoded_value : "");

    curl_response_t response = {0};
    response.data = (char*)malloc(1);

    curl_easy_setopt(client->curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(client->curl_handle, CURLOPT_POSTFIELDS, json_body);
    curl_easy_setopt(client->curl_handle, CURLOPT_WRITEFUNCTION, etcd_curl_write_callback);
    curl_easy_setopt(client->curl_handle, CURLOPT_WRITEDATA, (void*)&response);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(client->curl_handle, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(client->curl_handle);
    curl_slist_free_all(headers);

    free(encoded_key);
    free(encoded_value);
    free(response.data);

    return (res == CURLE_OK) ? 0 : -1;
}

char* etcd_get(etcd_client_t* client, const char* key) {
    if (!client || !key) return NULL;
    if (!client->connected) return NULL;

    char url[1024];
    snprintf(url, sizeof(url), "http://%s%s", client->addr, ETCD_GET_PATH);

    char* encoded_key = base64_encode(key);
    char json_body[2048];
    snprintf(json_body, sizeof(json_body), "{\"key\":\"%s\"}", encoded_key ? encoded_key : "");

    curl_response_t response = {0};
    response.data = (char*)malloc(1);

    curl_easy_setopt(client->curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(client->curl_handle, CURLOPT_POSTFIELDS, json_body);
    curl_easy_setopt(client->curl_handle, CURLOPT_WRITEFUNCTION, etcd_curl_write_callback);
    curl_easy_setopt(client->curl_handle, CURLOPT_WRITEDATA, (void*)&response);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(client->curl_handle, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(client->curl_handle);
    curl_slist_free_all(headers);
    free(encoded_key);

    if (res != CURLE_OK) {
        free(response.data);
        return NULL;
    }

    // Parse JSON response
    json_object* root = json_tokener_parse(response.data);
    free(response.data);
    
    if (!root) return NULL;

    // Navigate: kvs[0].value (base64 encoded)
    json_object* kvs = json_object_object_get(root, "kvs");
    char* decoded_value = NULL;

    if (kvs && json_object_is_type(kvs, json_type_array) && json_object_array_length(kvs) > 0) {
        json_object* kv = json_object_array_get_idx(kvs, 0);
        json_object* value_obj = json_object_object_get(kv, "value");
        
        if (value_obj) {
            const char* encoded = json_object_get_string(value_obj);
            if (encoded) {
                decoded_value = base64_decode(encoded);
            }
        }
    }

    json_object_put(root);
    return decoded_value;
}

int etcd_delete(etcd_client_t* client, const char* key) {
    if (!client || !key) return -1;
    if (!client->connected) return -1;

    char url[1024];
    snprintf(url, sizeof(url), "http://%s%s", client->addr, ETCD_DELETE_PATH);

    char* encoded_key = base64_encode(key);
    char json_body[2048];
    snprintf(json_body, sizeof(json_body), "{\"key\":\"%s\"}", encoded_key ? encoded_key : "");

    curl_response_t response = {0};
    response.data = (char*)malloc(1);

    curl_easy_setopt(client->curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(client->curl_handle, CURLOPT_POSTFIELDS, json_body);
    curl_easy_setopt(client->curl_handle, CURLOPT_WRITEFUNCTION, etcd_curl_write_callback);
    curl_easy_setopt(client->curl_handle, CURLOPT_WRITEDATA, (void*)&response);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(client->curl_handle, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(client->curl_handle);
    curl_slist_free_all(headers);
    free(encoded_key);
    free(response.data);

    return (res == CURLE_OK) ? 0 : -1;
}

char** etcd_list(etcd_client_t* client, const char* prefix) {
    if (!client || !prefix) return NULL;
    if (!client->connected) return NULL;

    char url[1024];
    snprintf(url, sizeof(url), "http://%s%s", client->addr, ETCD_GET_PATH);

    char* encoded_prefix = base64_encode(prefix);
    char json_body[2048];
    snprintf(json_body, sizeof(json_body), 
             "{\"key\":\"%s\",\"range_end\":\"%s_\"}", 
             encoded_prefix ? encoded_prefix : "",
             encoded_prefix ? encoded_prefix : "");

    curl_response_t response = {0};
    response.data = (char*)malloc(1);

    curl_easy_setopt(client->curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(client->curl_handle, CURLOPT_POSTFIELDS, json_body);
    curl_easy_setopt(client->curl_handle, CURLOPT_WRITEFUNCTION, etcd_curl_write_callback);
    curl_easy_setopt(client->curl_handle, CURLOPT_WRITEDATA, (void*)&response);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(client->curl_handle, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(client->curl_handle);
    curl_slist_free_all(headers);
    free(encoded_prefix);

    if (res != CURLE_OK) {
        free(response.data);
        return NULL;
    }

    // Parse JSON and extract all keys
    json_object* root = json_tokener_parse(response.data);
    free(response.data);
    
    if (!root) return NULL;

    // Allocate space for results (assume max 1000 keys)
    char** keys = (char**)malloc(sizeof(char*) * 1001);
    if (!keys) {
        json_object_put(root);
        return NULL;
    }
    memset(keys, 0, sizeof(char*) * 1001);

    // Extract keys from response
    json_object* kvs = json_object_object_get(root, "kvs");
    int count = 0;

    if (kvs && json_object_is_type(kvs, json_type_array)) {
        int len = json_object_array_length(kvs);
        for (int i = 0; i < len && count < 1000; i++) {
            json_object* kv = json_object_array_get_idx(kvs, i);
            json_object* key_obj = json_object_object_get(kv, "key");
            
            if (key_obj) {
                const char* encoded = json_object_get_string(key_obj);
                if (encoded) {
                    keys[count] = base64_decode(encoded);
                    if (keys[count]) count++;
                }
            }
        }
    }

    json_object_put(root);
    keys[count] = NULL;  // Null-terminate array
    return keys;
}

int etcd_save_pod(etcd_client_t* client, k8s_pod_t* pod) {
    if (!client || !pod) return -1;

    // Serialize pod to JSON
    json_object* pod_json = json_object_new_object();
    if (!pod_json) return -1;

    // Basic pod info
    json_object_object_add(pod_json, "name", 
        json_object_new_string(pod->metadata.name ? pod->metadata.name : ""));
    json_object_object_add(pod_json, "namespace", 
        json_object_new_string(pod->metadata.namespace ? pod->metadata.namespace : ""));
    json_object_object_add(pod_json, "uid", 
        json_object_new_string(pod->metadata.uid ? pod->metadata.uid : ""));
    
    // Status
    json_object_object_add(pod_json, "phase", 
        json_object_new_int(pod->status.phase));
    json_object_object_add(pod_json, "pod_ip", 
        json_object_new_string(pod->status.pod_ip ? pod->status.pod_ip : ""));

    // Build key: /sirah/pods/{namespace}/{name}
    char key[512];
    snprintf(key, sizeof(key), "%s%s/%s", SIRAH_POD_PREFIX, 
             pod->metadata.namespace ? pod->metadata.namespace : "default",
             pod->metadata.name ? pod->metadata.name : "");

    const char* json_str = json_object_to_json_string(pod_json);
    int result = etcd_put(client, key, json_str);

    json_object_put(pod_json);
    return result;
}

k8s_pod_t* etcd_load_pod(etcd_client_t* client, const char* namespace, const char* name) {
    if (!client || !namespace || !name) return NULL;

    // Build key
    char key[512];
    snprintf(key, sizeof(key), "%s%s/%s", SIRAH_POD_PREFIX, namespace, name);

    // Get from etcd
    char* json_str = etcd_get(client, key);
    if (!json_str) return NULL;

    // Parse and reconstruct pod
    json_object* pod_json = json_tokener_parse(json_str);
    free(json_str);
    
    if (!pod_json) return NULL;

    k8s_pod_t* pod = (k8s_pod_t*)malloc(sizeof(k8s_pod_t));
    if (!pod) {
        json_object_put(pod_json);
        return NULL;
    }
    memset(pod, 0, sizeof(k8s_pod_t));

    // Restore pod data
    json_object* name_obj = json_object_object_get(pod_json, "name");
    if (name_obj) {
        const char* n = json_object_get_string(name_obj);
        if (n) {
            pod->metadata.name = (char*)malloc(strlen(n) + 1);
            strcpy(pod->metadata.name, n);
        }
    }

    json_object* ns_obj = json_object_object_get(pod_json, "namespace");
    if (ns_obj) {
        const char* ns = json_object_get_string(ns_obj);
        if (ns) {
            pod->metadata.namespace = (char*)malloc(strlen(ns) + 1);
            strcpy(pod->metadata.namespace, ns);
        }
    }

    json_object* uid_obj = json_object_object_get(pod_json, "uid");
    if (uid_obj) {
        const char* u = json_object_get_string(uid_obj);
        if (u && strlen(u) > 0) {
            pod->metadata.uid = (char*)malloc(strlen(u) + 1);
            strcpy(pod->metadata.uid, u);
        }
    }

    json_object* phase_obj = json_object_object_get(pod_json, "phase");
    if (phase_obj) {
        pod->status.phase = json_object_get_int(phase_obj);
    }

    json_object* ip_obj = json_object_object_get(pod_json, "pod_ip");
    if (ip_obj) {
        const char* ip = json_object_get_string(ip_obj);
        if (ip && strlen(ip) > 0) {
            pod->status.pod_ip = (char*)malloc(strlen(ip) + 1);
            strcpy(pod->status.pod_ip, ip);
        }
    }

    json_object_put(pod_json);
    return pod;
}

int etcd_delete_pod(etcd_client_t* client, const char* namespace, const char* name) {
    if (!client || !namespace || !name) return -1;

    char key[512];
    snprintf(key, sizeof(key), "%s%s/%s", SIRAH_POD_PREFIX, namespace, name);

    return etcd_delete(client, key);
}

int etcd_restore_all_pods(etcd_client_t* client) {
    if (!client) return 0;

    char** pod_keys = etcd_list(client, SIRAH_POD_PREFIX);
    if (!pod_keys) return 0;

    int count = 0;
    for (int i = 0; pod_keys[i] != NULL; i++) {
        // Parse key to extract namespace and name
        const char* key = pod_keys[i];
        const char* start = strstr(key, SIRAH_POD_PREFIX);
        if (!start) continue;

        start += strlen(SIRAH_POD_PREFIX);
        char* slash = strchr(start, '/');
        if (!slash) continue;

        // Extract namespace and name
        int ns_len = slash - start;
        char namespace[256];
        strncpy(namespace, start, ns_len);
        namespace[ns_len] = '\0';
        
        const char* name = slash + 1;

        // Load and restore pod
        k8s_pod_t* pod = etcd_load_pod(client, namespace, name);
        if (pod && pod_store.count < 1000) {
            pod_store.pods[pod_store.count++] = pod;
            count++;
            printf("Restored pod: %s/%s\n", namespace, name);
        }

        free(pod_keys[i]);
    }
    free(pod_keys);

    return count;
}
