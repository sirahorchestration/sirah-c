#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

/* HTTP Request structure */
typedef struct {
    char *method;
    char *path;
    char *query;
    char *body;
    char **headers;
    int header_count;
} http_request_t;

/* HTTP Response structure */
typedef struct {
    int status_code;
    char **headers;
    int header_count;
    char *body;
    size_t body_size;
} http_response_t;

/* HTTP Request utilities */
const char* http_request_get_path_param(http_request_t *request, const char *param);
const char* http_request_get_body(http_request_t *request);

/* HTTP Response utilities */
int http_response_set_status(http_response_t *response, int status);
int http_response_set_header(http_response_t *response, const char *key, const char *value);
int http_response_send_headers(http_response_t *response);
int http_response_write(http_response_t *response, const char *data);
int http_response_error(http_response_t *response, int status, const char *error, const char *message);
int http_response_json(http_response_t *response, int status, const char *json);

#endif /* HTTP_H */
