// internal/storage/etcd.c
#include "store.h"
#include <curl/curl.h>
#include <json-c/json.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    char* data;
    size_t size;
} curl_response_t;

// Callback for curl response data
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
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

// etcd client structure
typedef struct {
    char endpoint[256];
    CURL* curl;
} etcd_client_internal_t;

int etcd_init(store_t* s, const char* addr) {
    if (!s || !addr) return -1;
    
    etcd_client_internal_t* client = (etcd_client_internal_t*)malloc(sizeof(etcd_client_internal_t));
    if (!client) return -1;
    
    // Setup endpoint
    snprintf(client->endpoint, sizeof(client->endpoint), "http://%s/v3", addr);
    
    // Initialize curl
    client->curl = curl_easy_init();
    if (!client->curl) {
        free(client);
        return -1;
    }
    
    s->backend = (void*)client;
    return 0;
}

int etcd_put(store_t* s, const char* key, const char* value) {
    if (!s || !key || !value) return -1;
    
    etcd_client_internal_t* client = (etcd_client_internal_t*)s->backend;
    if (!client) return -1;
    
    // Build etcd PUT request (base64 encoding for keys/values)
    // For simplicity, we'll use raw key/value format
    json_object* req = json_object_new_object();
    json_object_object_add(req, "key", json_object_new_string(key));
    json_object_object_add(req, "value", json_object_new_string(value));
    
    const char* json_str = json_object_to_json_string(req);
    
    // Prepare curl request
    char url[512];
    snprintf(url, sizeof(url), "%s/kv/put", client->endpoint);
    
    curl_response_t response = {0};
    response.data = (char*)malloc(1);
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(client->curl, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, (void*)&response);
    
    CURLcode res = curl_easy_perform(client->curl);
    
    curl_slist_free_all(headers);
    json_object_put(req);
    free(response.data);
    
    return (res == CURLE_OK) ? 0 : -1;
}

char* etcd_get(store_t* s, const char* key) {
    if (!s || !key) return NULL;
    
    etcd_client_internal_t* client = (etcd_client_internal_t*)s->backend;
    if (!client) return NULL;
    
    // Build etcd GET request
    json_object* req = json_object_new_object();
    json_object_object_add(req, "key", json_object_new_string(key));
    
    const char* json_str = json_object_to_json_string(req);
    
    char url[512];
    snprintf(url, sizeof(url), "%s/kv/range", client->endpoint);
    
    curl_response_t response = {0};
    response.data = (char*)malloc(1);
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(client->curl, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, (void*)&response);
    
    CURLcode res = curl_easy_perform(client->curl);
    
    curl_slist_free_all(headers);
    json_object_put(req);
    
    if (res != CURLE_OK || !response.data) {
        free(response.data);
        return NULL;
    }
    
    // Parse response to extract value
    json_object* resp_obj = json_tokener_parse(response.data);
    json_object* kvs = NULL;
    json_object* value_obj = NULL;
    const char* value = NULL;
    
    if (resp_obj && json_object_object_get_ex(resp_obj, "kvs", &kvs)) {
        if (json_type_array == json_object_get_type(kvs) && json_object_array_length(kvs) > 0) {
            json_object* kv = json_object_array_get_idx(kvs, 0);
            if (json_object_object_get_ex(kv, "value", &value_obj)) {
                value = json_object_get_string(value_obj);
            }
        }
    }
    
    char* result = NULL;
    if (value) {
        result = (char*)malloc(strlen(value) + 1);
        strcpy(result, value);
    }
    
    if (resp_obj) json_object_put(resp_obj);
    free(response.data);
    
    return result;
}

int etcd_delete(store_t* s, const char* key) {
    if (!s || !key) return -1;
    
    etcd_client_internal_t* client = (etcd_client_internal_t*)s->backend;
    if (!client) return -1;
    
    // Build etcd DELETE request
    json_object* req = json_object_new_object();
    json_object_object_add(req, "key", json_object_new_string(key));
    
    const char* json_str = json_object_to_json_string(req);
    
    char url[512];
    snprintf(url, sizeof(url), "%s/kv/deleterange", client->endpoint);
    
    curl_response_t response = {0};
    response.data = (char*)malloc(1);
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(client->curl, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, (void*)&response);
    
    CURLcode res = curl_easy_perform(client->curl);
    
    curl_slist_free_all(headers);
    json_object_put(req);
    free(response.data);
    
    return (res == CURLE_OK) ? 0 : -1;
}

int etcd_shutdown(store_t* s) {
    if (!s || !s->backend) return 0;
    
    etcd_client_internal_t* client = (etcd_client_internal_t*)s->backend;
    if (client->curl) {
        curl_easy_cleanup(client->curl);
    }
    free(client);
    s->backend = NULL;
    
    return 0;
}
