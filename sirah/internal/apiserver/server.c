// internal/apiserver/server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <microhttpd.h>
#include <json-c/json.h>
#include "server.h"
#include "handler.h"

static struct MHD_Daemon* http_daemon = NULL;
static int server_port = 6443;

// Connection state for accumulating request body
typedef struct {
    char* body;
    size_t body_len;
    int processed;
} con_state_t;

// Callback for POST processor (not used yet)
static int post_data_iterator(void *coninfo_cls, enum MHD_ValueKind kind, const char *key,
                              const char *filename, const char *content_type,
                              const char *transfer_encoding, const char *data, uint64_t off,
                              size_t size) {
    (void)coninfo_cls;
    (void)kind;
    (void)key;
    (void)filename;
    (void)content_type;
    (void)transfer_encoding;
    (void)off;
    (void)data;
    (void)size;
    return MHD_YES;
}

// Forward declarations
int api_handle_request(const char* method, const char* path, const char* body,
                       char* response_body, int* response_code);

// HTTP request callback
static enum MHD_Result request_callback(void* cls,
                                        struct MHD_Connection* connection,
                                        const char* url,
                                        const char* method,
                                        const char* version,
                                        const char* upload_data,
                                        size_t* upload_data_size,
                                        void** con_cls) {
    (void)cls;
    (void)version;

    // libmicrohttpd calls this multiple times
    // On first call for POST/PUT, upload_data_size may be 0
    // We need to wait for the second call to get the actual body data
    
    // Get or allocate the connection state
    con_state_t* state = (con_state_t*)*con_cls;
    if (state == NULL) {
        state = (con_state_t*)malloc(sizeof(con_state_t));
        state->body = (char*)malloc(16384);
        state->body_len = 0;
        state->processed = 0;
        *con_cls = (void*)state;
        
        // Return MHD_YES to continue receiving data for methods that have a body
        if (strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0 || strcmp(method, "PATCH") == 0) {
            return MHD_YES;
        }
    }
    
    // Accumulate upload data if provided
    if (upload_data != NULL && *upload_data_size > 0) {
        if (state->body_len + *upload_data_size < 16384) {
            memcpy(state->body + state->body_len, upload_data, *upload_data_size);
            state->body_len += *upload_data_size;
            state->body[state->body_len] = '\0';  // Null-terminate
        }
        *upload_data_size = 0;  // Tell libmicrohttpd we consumed the data
        return MHD_YES;  // Return to get more data if available
    }
    
    // If we've already processed, return
    if (state->processed) {
        return MHD_YES;
    }
    state->processed = 1;

    // Ensure body is null-terminated
    if (state->body_len > 0 && state->body[state->body_len - 1] != '\0') {
        state->body[state->body_len] = '\0';
    }

    // All data received, now process the request
    char* response_body = (char*)malloc(16384);
    if (response_body == NULL) {
        return MHD_NO;
    }
    memset(response_body, 0, 16384);
    
    int response_code = 200;

    // Check Accept header - we only support JSON
    // According to HTTP RFC 7231 and Kubernetes KEP-555:
    // If client requests a format we don't support, return 406 Not Acceptable.
    // 
    // Note: kubectl 1.28+ prefers protobuf encoding for efficiency, requesting it in Accept header.
    // Since Sirah only supports JSON, we return 406 to signal incompatibility.
    // kubectl should then retry with application/json.
    
    const char* accept_header = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Accept");
    
    if (accept_header != NULL && 
        strstr(accept_header, "application/vnd.kubernetes.protobuf") != NULL) {
        // Client explicitly requested protobuf, but we only support JSON
        // Return 406 Not Acceptable so client knows to retry with JSON
        response_code = 406;  // Not Acceptable
        snprintf(response_body, 16384, 
            "{\"kind\":\"Status\",\"apiVersion\":\"v1\",\"metadata\":{},\"status\":\"Failure\","
            "\"message\":\"The API server does not support protobuf encoding (application/vnd.kubernetes.protobuf). "
            "Only application/json is supported. Please use a client configured for JSON encoding.\","
            "\"reason\":\"NotAcceptable\","
            "\"code\":406}");
    } else {
        // Route the request with the accumulated body
        api_handle_request(method, url, state->body, response_body, &response_code);
    }

    // Create response - use MHD_RESPMEM_MUST_FREE since we malloc'd it
    struct MHD_Response* response = MHD_create_response_from_buffer(
        strlen(response_body),
        (void*)response_body,
        MHD_RESPMEM_MUST_FREE
    );

    if (response == NULL) {
        free(response_body);
        if (state->body != NULL) free(state->body);
        free(state);
        return MHD_NO;
    }

    // Determine Content-Type based on request path
    // Log endpoints return raw stream without structured content-type
    // Other endpoints return JSON
    if (strstr(url, "/log") == NULL) {
        MHD_add_response_header(response, "Content-Type", "application/json");
    }
    // For log endpoints, don't set Content-Type - just return raw logs
    enum MHD_Result ret = MHD_queue_response(connection, response_code, response);
    MHD_destroy_response(response);
    
    // Don't clean up state here - let request_completed do it
    // The request_completed callback will be called by libmicrohttpd to clean up

    return ret;
}

// Callback for when request is complete (cleanup)
static void request_completed(void* cls, struct MHD_Connection* connection,
                             void** con_cls, enum MHD_RequestTerminationCode toe) {
    (void)cls;
    (void)connection;
    (void)toe;
    
    con_state_t* state = (con_state_t*)*con_cls;
    if (state != NULL) {
        if (state->body != NULL) {
            free(state->body);
        }
        free(state);
        *con_cls = NULL;
    }
}

int api_server_init(int port) {
    server_port = port;

    http_daemon = MHD_start_daemon(
        MHD_USE_THREAD_PER_CONNECTION,
        port,
        NULL,
        NULL,
        &request_callback,
        NULL,
        MHD_OPTION_NOTIFY_COMPLETED, &request_completed, NULL,
        MHD_OPTION_END
    );

    if (http_daemon == NULL) {
        fprintf(stderr, "Failed to start HTTP server on port %d\n", port);
        return -1;
    }

    printf("API Server initialized on port %d\n", port);
    return 0;
}

int api_server_run(void) {
    printf("API Server running...\n");

    while (1) {
        sleep(1);
    }

    return 0;
}

void api_server_shutdown(void) {
    if (http_daemon != NULL) {
        MHD_stop_daemon(http_daemon);
        http_daemon = NULL;
    }
    printf("API Server shutdown\n");
}
