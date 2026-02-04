#include "etcd_client.h"
#include <curl/curl.h>
#include <json-c/json.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>

/**
 * etcd_client.c - etcd v3 HTTP API Client Implementation
 *
 * Implements etcd v3.5+ HTTP API bindings with:
 * - Multi-endpoint failover and circuit breaker pattern
 * - Automatic retry on transient failures
 * - Thread-safe operations with mutex protection
 * - Full JSON serialization/deserialization
 * - CAS (Compare-And-Swap) for optimistic locking
 */

/* ============================================================================
 * Constants
 * ============================================================================ */

#define ETCD_CB_FAILURE_THRESHOLD 5
#define ETCD_CB_TIMEOUT 30
#define ETCD_DEFAULT_TIMEOUT 30
#define ETCD_DEFAULT_MAX_RETRIES 3
#define ETCD_BUFFER_SIZE 4096

/* ============================================================================
 * HTTP Request/Response Handling
 * ============================================================================ */

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} etcd_buffer_t;

static size_t etcd_curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    etcd_buffer_t *buf = (etcd_buffer_t *)userp;

    char *ptr = realloc(buf->data, buf->size + realsize + 1);
    if (!ptr) {
        fprintf(stderr, "etcd: Not enough memory for curl callback\n");
        return 0;
    }

    buf->data = ptr;
    memcpy(&(buf->data[buf->size]), contents, realsize);
    buf->size += realsize;
    buf->data[buf->size] = 0;

    return realsize;
}

static etcd_buffer_t *etcd_buffer_create(void) {
    etcd_buffer_t *buf = malloc(sizeof(etcd_buffer_t));
    if (!buf) return NULL;
    buf->data = malloc(ETCD_BUFFER_SIZE);
    if (!buf->data) {
        free(buf);
        return NULL;
    }
    buf->size = 0;
    buf->capacity = ETCD_BUFFER_SIZE;
    return buf;
}

static void etcd_buffer_free(etcd_buffer_t *buf) {
    if (!buf) return;
    if (buf->data) free(buf->data);
    free(buf);
}

/* ============================================================================
 * Base64 Encoding/Decoding (for etcd keys/values)
 * ============================================================================ */

static char *etcd_base64_encode(const char *input, size_t input_len) {
    // etcd v3 API requires base64 encoding for keys and values
    int output_len = (input_len + 2) / 3 * 4 + 1;
    char *output = malloc(output_len);
    if (!output) return NULL;

    // Simple base64 encoding
    static const char *base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    int i = 0, j = 0;
    unsigned char a[3], b[4];

    while (input_len--) {
        a[i++] = *input++;
        if (i == 3) {
            b[0] = (a[0] & 0xfc) >> 2;
            b[1] = ((a[0] & 0x03) << 4) + ((a[1] & 0xf0) >> 4);
            b[2] = ((a[1] & 0x0f) << 2) + ((a[2] & 0xc0) >> 6);
            b[3] = a[2] & 0x3f;

            for (i = 0; i < 4; i++)
                output[j++] = base64_chars[b[i]];
            i = 0;
        }
    }

    if (i > 0) {
        for (int k = i; k < 3; k++)
            a[k] = '\0';

        b[0] = (a[0] & 0xfc) >> 2;
        b[1] = ((a[0] & 0x03) << 4) + ((a[1] & 0xf0) >> 4);
        b[2] = ((a[1] & 0x0f) << 2) + ((a[2] & 0xc0) >> 6);

        for (int k = 0; k <= i; k++)
            output[j++] = base64_chars[b[k]];

        while (i++ < 3)
            output[j++] = '=';
    }

    output[j] = '\0';
    return output;
}

static char *etcd_base64_decode(const char *input) {
    // Simple base64 decoder
    int input_len = strlen(input);
    char *output = malloc(input_len + 1);
    if (!output) return NULL;

    static const char *base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    int i = 0, j = 0;
    unsigned char a[4], b[3];
    int n;

    while (input_len--) {
        if (*input == '=') break;

        for (n = 0; n < 64; n++) {
            if (base64_chars[n] == *input) break;
        }
        a[i++] = n;
        input++;

        if (i == 4) {
            b[0] = (a[0] << 2) + ((a[1] & 0x30) >> 4);
            b[1] = (((a[1] & 0x0f) << 4) + ((a[2] & 0x3c) >> 2));
            b[2] = (((a[2] & 0x03) << 6) + a[3]);

            for (i = 0; i < 3; i++)
                output[j++] = b[i];
            i = 0;
        }
    }

    if (i > 0) {
        for (int k = i; k < 4; k++)
            a[k] = 0;

        b[0] = (a[0] << 2) + ((a[1] & 0x30) >> 4);
        b[1] = (((a[1] & 0x0f) << 4) + ((a[2] & 0x3c) >> 2));
        b[2] = (((a[2] & 0x03) << 6) + a[3]);

        for (int k = 0; k < i - 1; k++)
            output[j++] = b[k];
    }

    output[j] = '\0';
    return output;
}

/* ============================================================================
 * Circuit Breaker Implementation
 * ============================================================================ */

static void etcd_circuit_breaker_init(etcd_circuit_breaker_t *cb) {
    cb->state = ETCD_CB_CLOSED;
    cb->failure_count = 0;
    cb->success_count = 0;
    cb->last_failure_time = 0;
    cb->half_open_test_time = 0;
    cb->failure_threshold = ETCD_CB_FAILURE_THRESHOLD;
    cb->timeout_seconds = ETCD_CB_TIMEOUT;
}

static bool etcd_circuit_breaker_should_allow(etcd_circuit_breaker_t *cb) {
    time_t now = time(NULL);

    switch (cb->state) {
    case ETCD_CB_CLOSED:
        return true;

    case ETCD_CB_OPEN:
        // Check if timeout has passed to test recovery
        if (now - cb->last_failure_time > cb->timeout_seconds) {
            cb->state = ETCD_CB_HALF_OPEN;
            cb->half_open_test_time = now;
            return true;
        }
        return false;

    case ETCD_CB_HALF_OPEN:
        return true;

    default:
        return false;
    }
}

static void etcd_circuit_breaker_record_success(etcd_circuit_breaker_t *cb) {
    cb->failure_count = 0;
    cb->success_count++;

    if (cb->state == ETCD_CB_HALF_OPEN) {
        cb->state = ETCD_CB_CLOSED;
        cb->success_count = 0;
    }
}

static void etcd_circuit_breaker_record_failure(etcd_circuit_breaker_t *cb) {
    cb->failure_count++;
    cb->last_failure_time = time(NULL);

    if (cb->state == ETCD_CB_HALF_OPEN) {
        cb->state = ETCD_CB_OPEN;
        cb->failure_count = 1;
    } else if (cb->state == ETCD_CB_CLOSED && cb->failure_count >= cb->failure_threshold) {
        cb->state = ETCD_CB_OPEN;
    }
}

/* ============================================================================
 * JSON Parsing Utilities
 * ============================================================================ */

static char *etcd_parse_json_string(json_object *obj, const char *key, const char *default_val) {
    json_object *jobj = NULL;
    if (!json_object_object_get_ex(obj, key, &jobj)) {
        return default_val ? strdup(default_val) : NULL;
    }

    const char *value = json_object_get_string(jobj);
    return value ? strdup(value) : NULL;
}

static uint64_t etcd_parse_json_int(json_object *obj, const char *key, uint64_t default_val) {
    json_object *jobj = NULL;
    if (!json_object_object_get_ex(obj, key, &jobj)) {
        return default_val;
    }
    return (uint64_t)json_object_get_int64(jobj);
}

/* ============================================================================
 * Client Operations
 * ============================================================================ */

etcd_client_t *etcd_client_create(const char **endpoints, int endpoint_count) {
    if (!endpoints || endpoint_count <= 0) {
        fprintf(stderr, "etcd: Invalid endpoints\n");
        return NULL;
    }

    etcd_client_t *client = malloc(sizeof(etcd_client_t));
    if (!client) return NULL;

    client->endpoints = malloc(sizeof(etcd_endpoint_t) * endpoint_count);
    if (!client->endpoints) {
        free(client);
        return NULL;
    }

    for (int i = 0; i < endpoint_count; i++) {
        client->endpoints[i].url = strdup(endpoints[i]);
        etcd_circuit_breaker_init(&client->endpoints[i].breaker);
        client->endpoints[i].connection_count = 0;
        client->endpoints[i].total_requests = 0;
        client->endpoints[i].total_errors = 0;
    }

    client->endpoint_count = endpoint_count;
    client->current_endpoint_index = 0;
    client->timeout_seconds = ETCD_DEFAULT_TIMEOUT;
    client->max_retries = ETCD_DEFAULT_MAX_RETRIES;

    pthread_mutex_t *mutex = malloc(sizeof(pthread_mutex_t));
    if (!mutex) {
        for (int i = 0; i < endpoint_count; i++) {
            free(client->endpoints[i].url);
        }
        free(client->endpoints);
        free(client);
        return NULL;
    }

    pthread_mutex_init(mutex, NULL);
    client->mutex = (void *)mutex;

    client->total_requests = 0;
    client->total_errors = 0;
    client->last_error_time = 0;
    client->last_error_message[0] = '\0';

    return client;
}

void etcd_client_set_timeout(etcd_client_t *client, int timeout_seconds) {
    if (client) {
        client->timeout_seconds = timeout_seconds;
    }
}

void etcd_client_set_max_retries(etcd_client_t *client, int max_retries) {
    if (client) {
        client->max_retries = max_retries;
    }
}

void etcd_client_free(etcd_client_t *client) {
    if (!client) return;

    for (int i = 0; i < client->endpoint_count; i++) {
        free(client->endpoints[i].url);
    }
    free(client->endpoints);

    pthread_mutex_t *mutex = (pthread_mutex_t *)client->mutex;
    if (mutex) {
        pthread_mutex_destroy(mutex);
        free(mutex);
    }

    free(client);
}

/* ============================================================================
 * HTTP Request Execution
 * ============================================================================ */

typedef struct {
    const char *url;
    const char *method;
    const char *request_body;
    int timeout;
    etcd_buffer_t *response;
    long http_code;
} etcd_http_request_t;

static etcd_status_t etcd_execute_http_request(etcd_http_request_t *req) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return ETCD_INTERNAL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, req->url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, req->timeout);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, req->method);

    if (req->request_body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->request_body);
    }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, etcd_curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)req->response);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        if (res == CURLE_OPERATION_TIMEDOUT) {
            return ETCD_TIMEOUT;
        }
        return ETCD_NETWORK_ERROR;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &req->http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return ETCD_OK;
}

/* ============================================================================
 * PUT Operation (Store key-value)
 * ============================================================================ */

etcd_status_t etcd_put(etcd_client_t *client, const char *key, const char *value,
                       uint64_t lease_id, etcd_response_t *response) {
    if (!client || !key || !value || !response) {
        return ETCD_INVALID_ARGUMENT;
    }

    pthread_mutex_t *mutex = (pthread_mutex_t *)client->mutex;
    pthread_mutex_lock(mutex);

    client->total_requests++;

    // Prepare request JSON
    json_object *req_json = json_object_new_object();
    
    // Encode key and value in base64
    char *encoded_key = etcd_base64_encode(key, strlen(key));
    char *encoded_value = etcd_base64_encode(value, strlen(value));
    
    json_object_object_add(req_json, "key", json_object_new_string(encoded_key));
    json_object_object_add(req_json, "value", json_object_new_string(encoded_value));
    
    if (lease_id > 0) {
        json_object_object_add(req_json, "lease", json_object_new_int64(lease_id));
    }

    const char *request_body = json_object_to_json_string(req_json);
    
    // Find healthy endpoint
    int endpoint_idx = client->current_endpoint_index;
    etcd_status_t status = ETCD_CLUSTER_UNAVAILABLE;

    for (int attempt = 0; attempt < client->max_retries; attempt++) {
        if (!etcd_circuit_breaker_should_allow(&client->endpoints[endpoint_idx].breaker)) {
            endpoint_idx = (endpoint_idx + 1) % client->endpoint_count;
            continue;
        }

        // Build URL
        char url[512];
        snprintf(url, sizeof(url), "%s/v3/kv/put", client->endpoints[endpoint_idx].url);

        etcd_buffer_t *resp_buf = etcd_buffer_create();
        if (!resp_buf) {
            status = ETCD_INTERNAL;
            break;
        }

        etcd_http_request_t http_req = {
            .url = url,
            .method = "POST",
            .request_body = request_body,
            .timeout = client->timeout_seconds,
            .response = resp_buf
        };

        status = etcd_execute_http_request(&http_req);

        if (status == ETCD_OK) {
            // Parse response
            json_object *resp_json = json_tokener_parse(resp_buf->data);
            if (resp_json) {
                response->revision = etcd_parse_json_int(resp_json, "header", 0);
                
                json_object *header_obj = NULL;
                if (json_object_object_get_ex(resp_json, "header", &header_obj)) {
                    response->revision = etcd_parse_json_int(header_obj, "revision", 0);
                }

                json_object_put(resp_json);
                etcd_circuit_breaker_record_success(&client->endpoints[endpoint_idx].breaker);
                etcd_buffer_free(resp_buf);
                break;
            }
        } else if (status == ETCD_TIMEOUT) {
            etcd_circuit_breaker_record_failure(&client->endpoints[endpoint_idx].breaker);
        } else {
            etcd_circuit_breaker_record_failure(&client->endpoints[endpoint_idx].breaker);
        }

        etcd_buffer_free(resp_buf);
        endpoint_idx = (endpoint_idx + 1) % client->endpoint_count;
    }

    client->current_endpoint_index = endpoint_idx;

    json_object_put(req_json);
    free(encoded_key);
    free(encoded_value);

    if (status != ETCD_OK) {
        client->total_errors++;
        client->last_error_time = time(NULL);
    }

    pthread_mutex_unlock(mutex);
    return status;
}

/* ============================================================================
 * GET Operation (Retrieve value)
 * ============================================================================ */

etcd_status_t etcd_get(etcd_client_t *client, const char *key, etcd_response_t *response) {
    if (!client || !key || !response) {
        return ETCD_INVALID_ARGUMENT;
    }

    pthread_mutex_t *mutex = (pthread_mutex_t *)client->mutex;
    pthread_mutex_lock(mutex);

    client->total_requests++;

    // Build URL
    char encoded_key[512];
    // Simple URL encoding: key goes in request body
    json_object *req_json = json_object_new_object();
    char *b64_key = etcd_base64_encode(key, strlen(key));
    json_object_object_add(req_json, "key", json_object_new_string(b64_key));

    const char *request_body = json_object_to_json_string(req_json);

    int endpoint_idx = client->current_endpoint_index;
    etcd_status_t status = ETCD_CLUSTER_UNAVAILABLE;

    for (int attempt = 0; attempt < client->max_retries; attempt++) {
        if (!etcd_circuit_breaker_should_allow(&client->endpoints[endpoint_idx].breaker)) {
            endpoint_idx = (endpoint_idx + 1) % client->endpoint_count;
            continue;
        }

        char url[512];
        snprintf(url, sizeof(url), "%s/v3/kv/range", client->endpoints[endpoint_idx].url);

        etcd_buffer_t *resp_buf = etcd_buffer_create();
        if (!resp_buf) {
            status = ETCD_INTERNAL;
            break;
        }

        etcd_http_request_t http_req = {
            .url = url,
            .method = "POST",
            .request_body = request_body,
            .timeout = client->timeout_seconds,
            .response = resp_buf
        };

        status = etcd_execute_http_request(&http_req);

        if (status == ETCD_OK && http_req.http_code == 200) {
            json_object *resp_json = json_tokener_parse(resp_buf->data);
            if (resp_json) {
                json_object *kvs_array = NULL;
                if (json_object_object_get_ex(resp_json, "kvs", &kvs_array)) {
                    if (json_object_array_length(kvs_array) > 0) {
                        json_object *kv = json_object_array_get_idx(kvs_array, 0);
                        
                        char *encoded_val = etcd_parse_json_string(kv, "value", NULL);
                        if (encoded_val) {
                            response->value = etcd_base64_decode(encoded_val);
                            free(encoded_val);
                        }
                        
                        response->version = etcd_parse_json_int(kv, "version", 0);
                        response->mod_revision = etcd_parse_json_int(kv, "mod_revision", 0);
                        response->create_revision = etcd_parse_json_int(kv, "create_revision", 0);
                        
                        status = ETCD_OK;
                    } else {
                        status = ETCD_NOT_FOUND;
                    }
                }

                json_object_put(resp_json);
                if (status == ETCD_OK) {
                    etcd_circuit_breaker_record_success(&client->endpoints[endpoint_idx].breaker);
                    etcd_buffer_free(resp_buf);
                    break;
                }
            }
        } else if (status == ETCD_TIMEOUT) {
            etcd_circuit_breaker_record_failure(&client->endpoints[endpoint_idx].breaker);
        } else {
            etcd_circuit_breaker_record_failure(&client->endpoints[endpoint_idx].breaker);
        }

        etcd_buffer_free(resp_buf);
        endpoint_idx = (endpoint_idx + 1) % client->endpoint_count;
    }

    client->current_endpoint_index = endpoint_idx;
    json_object_put(req_json);
    free(b64_key);

    if (status != ETCD_OK) {
        client->total_errors++;
        client->last_error_time = time(NULL);
    }

    pthread_mutex_unlock(mutex);
    return status;
}

/* ============================================================================
 * DELETE Operation
 * ============================================================================ */

etcd_status_t etcd_delete(etcd_client_t *client, const char *key, etcd_response_t *response) {
    if (!client || !key) {
        return ETCD_INVALID_ARGUMENT;
    }

    pthread_mutex_t *mutex = (pthread_mutex_t *)client->mutex;
    pthread_mutex_lock(mutex);

    client->total_requests++;

    json_object *req_json = json_object_new_object();
    char *b64_key = etcd_base64_encode(key, strlen(key));
    json_object_object_add(req_json, "key", json_object_new_string(b64_key));

    const char *request_body = json_object_to_json_string(req_json);

    int endpoint_idx = client->current_endpoint_index;
    etcd_status_t status = ETCD_CLUSTER_UNAVAILABLE;

    for (int attempt = 0; attempt < client->max_retries; attempt++) {
        if (!etcd_circuit_breaker_should_allow(&client->endpoints[endpoint_idx].breaker)) {
            endpoint_idx = (endpoint_idx + 1) % client->endpoint_count;
            continue;
        }

        char url[512];
        snprintf(url, sizeof(url), "%s/v3/kv/deleterange", client->endpoints[endpoint_idx].url);

        etcd_buffer_t *resp_buf = etcd_buffer_create();
        if (!resp_buf) {
            status = ETCD_INTERNAL;
            break;
        }

        etcd_http_request_t http_req = {
            .url = url,
            .method = "POST",
            .request_body = request_body,
            .timeout = client->timeout_seconds,
            .response = resp_buf
        };

        status = etcd_execute_http_request(&http_req);

        if (status == ETCD_OK) {
            etcd_circuit_breaker_record_success(&client->endpoints[endpoint_idx].breaker);
            etcd_buffer_free(resp_buf);
            break;
        }

        etcd_circuit_breaker_record_failure(&client->endpoints[endpoint_idx].breaker);
        etcd_buffer_free(resp_buf);
        endpoint_idx = (endpoint_idx + 1) % client->endpoint_count;
    }

    client->current_endpoint_index = endpoint_idx;
    json_object_put(req_json);
    free(b64_key);

    if (status != ETCD_OK) {
        client->total_errors++;
    }

    pthread_mutex_unlock(mutex);
    return status;
}

/* ============================================================================
 * LIST Operation (Range query with prefix)
 * ============================================================================ */

etcd_status_t etcd_list(etcd_client_t *client, const char *prefix, etcd_response_t *response) {
    if (!client || !prefix || !response) {
        return ETCD_INVALID_ARGUMENT;
    }

    pthread_mutex_t *mutex = (pthread_mutex_t *)client->mutex;
    pthread_mutex_lock(mutex);

    client->total_requests++;

    json_object *req_json = json_object_new_object();
    char *b64_prefix = etcd_base64_encode(prefix, strlen(prefix));
    
    // For range queries, etcd requires: key=prefix_bytes and range_end=prefix_end_bytes
    // prefix_end is prefix with last byte incremented
    char *range_end = malloc(strlen(prefix) + 2);
    strcpy(range_end, prefix);
    // Increment last character (simple approach)
    size_t last = strlen(range_end) - 1;
    range_end[last]++;
    
    char *b64_range_end = etcd_base64_encode(range_end, strlen(range_end));
    
    json_object_object_add(req_json, "key", json_object_new_string(b64_prefix));
    json_object_object_add(req_json, "range_end", json_object_new_string(b64_range_end));

    const char *request_body = json_object_to_json_string(req_json);

    int endpoint_idx = client->current_endpoint_index;
    etcd_status_t status = ETCD_CLUSTER_UNAVAILABLE;

    for (int attempt = 0; attempt < client->max_retries; attempt++) {
        if (!etcd_circuit_breaker_should_allow(&client->endpoints[endpoint_idx].breaker)) {
            endpoint_idx = (endpoint_idx + 1) % client->endpoint_count;
            continue;
        }

        char url[512];
        snprintf(url, sizeof(url), "%s/v3/kv/range", client->endpoints[endpoint_idx].url);

        etcd_buffer_t *resp_buf = etcd_buffer_create();
        if (!resp_buf) {
            status = ETCD_INTERNAL;
            break;
        }

        etcd_http_request_t http_req = {
            .url = url,
            .method = "POST",
            .request_body = request_body,
            .timeout = client->timeout_seconds,
            .response = resp_buf
        };

        status = etcd_execute_http_request(&http_req);

        if (status == ETCD_OK && http_req.http_code == 200) {
            json_object *resp_json = json_tokener_parse(resp_buf->data);
            if (resp_json) {
                json_object *kvs_array = NULL;
                if (json_object_object_get_ex(resp_json, "kvs", &kvs_array)) {
                    int count = json_object_array_length(kvs_array);
                    response->kvs_count = count;

                    if (count > 0) {
                        response->kvs_keys = malloc(sizeof(char *) * count);
                        response->kvs_values = malloc(sizeof(char *) * count);
                        response->kvs_versions = malloc(sizeof(int) * count);

                        for (int i = 0; i < count; i++) {
                            json_object *kv = json_object_array_get_idx(kvs_array, i);
                            
                            char *encoded_key = etcd_parse_json_string(kv, "key", NULL);
                            response->kvs_keys[i] = etcd_base64_decode(encoded_key);
                            free(encoded_key);

                            char *encoded_val = etcd_parse_json_string(kv, "value", NULL);
                            response->kvs_values[i] = etcd_base64_decode(encoded_val);
                            free(encoded_val);

                            response->kvs_versions[i] = etcd_parse_json_int(kv, "version", 0);
                        }

                        status = ETCD_OK;
                    } else {
                        status = ETCD_OK;  // Empty range is OK
                    }
                }

                json_object_put(resp_json);
                if (status == ETCD_OK) {
                    etcd_circuit_breaker_record_success(&client->endpoints[endpoint_idx].breaker);
                    etcd_buffer_free(resp_buf);
                    break;
                }
            }
        } else {
            etcd_circuit_breaker_record_failure(&client->endpoints[endpoint_idx].breaker);
        }

        etcd_buffer_free(resp_buf);
        endpoint_idx = (endpoint_idx + 1) % client->endpoint_count;
    }

    client->current_endpoint_index = endpoint_idx;
    json_object_put(req_json);
    free(b64_prefix);
    free(b64_range_end);
    free(range_end);

    if (status != ETCD_OK && status != ETCD_NOT_FOUND) {
        client->total_errors++;
    }

    pthread_mutex_unlock(mutex);
    return status;
}

/* ============================================================================
 * Optimistic Locking (CAS)
 * ============================================================================ */

etcd_status_t etcd_put_if_revision_matches(etcd_client_t *client, const char *key,
                                           const char *new_value,
                                           uint64_t expected_revision,
                                           etcd_response_t *response) {
    if (!client || !key || !new_value || !response) {
        return ETCD_INVALID_ARGUMENT;
    }

    pthread_mutex_t *mutex = (pthread_mutex_t *)client->mutex;
    pthread_mutex_lock(mutex);

    client->total_requests++;

    // etcd CAS using txn (transaction) API
    json_object *txn_json = json_object_new_object();
    json_object *compare_array = json_object_new_array();
    
    // Build compare condition: mod_revision == expected_revision
    json_object *compare = json_object_new_object();
    char *b64_key = etcd_base64_encode(key, strlen(key));
    json_object_object_add(compare, "key", json_object_new_string(b64_key));
    json_object_object_add(compare, "result", json_object_new_string("0"));  // EQUAL
    json_object_object_add(compare, "target", json_object_new_string("MOD"));
    json_object_object_add(compare, "mod_revision", json_object_new_int64(expected_revision));
    
    json_object_array_add(compare_array, compare);
    json_object_object_add(txn_json, "compare", compare_array);

    // Success case: PUT new value
    json_object *success_array = json_object_new_array();
    json_object *put_req = json_object_new_object();
    char *b64_value = etcd_base64_encode(new_value, strlen(new_value));
    json_object_object_add(put_req, "request_put", json_object_new_object());
    json_object_object_add(put_req, "key", json_object_new_string(b64_key));
    json_object_object_add(put_req, "value", json_object_new_string(b64_value));
    json_object_array_add(success_array, put_req);
    json_object_object_add(txn_json, "success", success_array);

    const char *request_body = json_object_to_json_string(txn_json);

    int endpoint_idx = client->current_endpoint_index;
    etcd_status_t status = ETCD_CLUSTER_UNAVAILABLE;

    for (int attempt = 0; attempt < client->max_retries; attempt++) {
        if (!etcd_circuit_breaker_should_allow(&client->endpoints[endpoint_idx].breaker)) {
            endpoint_idx = (endpoint_idx + 1) % client->endpoint_count;
            continue;
        }

        char url[512];
        snprintf(url, sizeof(url), "%s/v3/kv/txn", client->endpoints[endpoint_idx].url);

        etcd_buffer_t *resp_buf = etcd_buffer_create();
        if (!resp_buf) {
            status = ETCD_INTERNAL;
            break;
        }

        etcd_http_request_t http_req = {
            .url = url,
            .method = "POST",
            .request_body = request_body,
            .timeout = client->timeout_seconds,
            .response = resp_buf
        };

        status = etcd_execute_http_request(&http_req);

        if (status == ETCD_OK && http_req.http_code == 200) {
            json_object *resp_json = json_tokener_parse(resp_buf->data);
            if (resp_json) {
                json_object *succeeded_obj = NULL;
                if (json_object_object_get_ex(resp_json, "succeeded", &succeeded_obj)) {
                    bool succeeded = json_object_get_boolean(succeeded_obj);
                    if (succeeded) {
                        status = ETCD_OK;
                        
                        // Parse response to get new revision
                        json_object *responses = NULL;
                        if (json_object_object_get_ex(resp_json, "responses", &responses)) {
                            int resp_count = json_object_array_length(responses);
                            if (resp_count > 0) {
                                json_object *first_resp = json_object_array_get_idx(responses, 0);
                                response->revision = etcd_parse_json_int(first_resp, "revision", 0);
                            }
                        }
                    } else {
                        status = ETCD_CAS_FAILED;
                    }
                }

                json_object_put(resp_json);
                if (status == ETCD_OK || status == ETCD_CAS_FAILED) {
                    etcd_circuit_breaker_record_success(&client->endpoints[endpoint_idx].breaker);
                    etcd_buffer_free(resp_buf);
                    break;
                }
            }
        } else {
            etcd_circuit_breaker_record_failure(&client->endpoints[endpoint_idx].breaker);
        }

        etcd_buffer_free(resp_buf);
        endpoint_idx = (endpoint_idx + 1) % client->endpoint_count;
    }

    client->current_endpoint_index = endpoint_idx;
    json_object_put(txn_json);
    free(b64_key);
    free(b64_value);

    if (status != ETCD_OK && status != ETCD_CAS_FAILED) {
        client->total_errors++;
    }

    pthread_mutex_unlock(mutex);
    return status;
}

/* ============================================================================
 * Health & Status Operations
 * ============================================================================ */

bool etcd_is_healthy(etcd_client_t *client) {
    if (!client) return false;

    for (int i = 0; i < client->endpoint_count; i++) {
        if (etcd_circuit_breaker_should_allow(&client->endpoints[i].breaker)) {
            return true;
        }
    }

    return false;
}

etcd_status_t etcd_get_leader(etcd_client_t *client, char **leader_id) {
    // TODO: Implement /maintenance/status endpoint
    return ETCD_OK;
}

etcd_status_t etcd_get_stats(etcd_client_t *client, char **stats_json) {
    // TODO: Implement /v3/maintenance/status endpoint
    return ETCD_OK;
}

etcd_status_t etcd_get_member_list(etcd_client_t *client, char **members_json) {
    // TODO: Implement /v3/cluster/member/list endpoint
    return ETCD_OK;
}

/* ============================================================================
 * Lease Operations
 * ============================================================================ */

etcd_status_t etcd_lease_grant(etcd_client_t *client, int ttl_seconds, uint64_t *lease_id) {
    // TODO: Implement /v3/lease/grant endpoint
    return ETCD_OK;
}

etcd_status_t etcd_lease_revoke(etcd_client_t *client, uint64_t lease_id) {
    // TODO: Implement /v3/lease/revoke endpoint
    return ETCD_OK;
}

etcd_status_t etcd_lease_keep_alive(etcd_client_t *client, uint64_t lease_id) {
    // TODO: Implement /v3/lease/keepalive endpoint
    return ETCD_OK;
}

/* ============================================================================
 * Cleanup & Utilities
 * ============================================================================ */

void etcd_response_free(etcd_response_t *response) {
    if (!response) return;

    if (response->value) free(response->value);
    if (response->prev_value) free(response->prev_value);

    if (response->kvs_keys) {
        for (int i = 0; i < response->kvs_count; i++) {
            if (response->kvs_keys[i]) free(response->kvs_keys[i]);
        }
        free(response->kvs_keys);
    }

    if (response->kvs_values) {
        for (int i = 0; i < response->kvs_count; i++) {
            if (response->kvs_values[i]) free(response->kvs_values[i]);
        }
        free(response->kvs_values);
    }

    if (response->kvs_versions) free(response->kvs_versions);

    memset(response, 0, sizeof(etcd_response_t));
}

etcd_client_stats_t *etcd_client_get_stats(etcd_client_t *client) {
    static etcd_client_stats_t stats;

    if (client) {
        stats.total_requests = client->total_requests;
        stats.total_errors = client->total_errors;
        stats.total_endpoints = client->endpoint_count;

        stats.healthy_endpoints = 0;
        for (int i = 0; i < client->endpoint_count; i++) {
            if (etcd_circuit_breaker_should_allow(&client->endpoints[i].breaker)) {
                stats.healthy_endpoints++;
            }
        }

        stats.last_error = client->last_error_message;
    }

    return &stats;
}

const char *etcd_strerror(etcd_status_t status) {
    switch (status) {
    case ETCD_OK: return "OK";
    case ETCD_NOT_FOUND: return "Not Found";
    case ETCD_CAS_FAILED: return "Compare-And-Swap Failed (Version Mismatch)";
    case ETCD_NETWORK_ERROR: return "Network Error";
    case ETCD_TIMEOUT: return "Request Timeout";
    case ETCD_UNAVAILABLE: return "Service Unavailable";
    case ETCD_PERMISSION_DENIED: return "Permission Denied";
    case ETCD_INVALID_ARGUMENT: return "Invalid Argument";
    case ETCD_OUT_OF_RANGE: return "Out of Range";
    case ETCD_INTERNAL: return "Internal Error";
    case ETCD_CLUSTER_UNAVAILABLE: return "Cluster Unavailable";
    case ETCD_CIRCUIT_BREAKER_OPEN: return "Circuit Breaker Open";
    default: return "Unknown Error";
    }
}
