#ifndef SIRAH_SERVICE_CONTROLLER_H
#define SIRAH_SERVICE_CONTROLLER_H

#include "../../pkg/types/service.h"

typedef struct {
    char* api_server_url;
    void* curl_handle;
    int update_interval;
} service_controller_t;

service_controller_t* service_controller_new(const char* api_server_url);
void service_controller_free(service_controller_t* controller);
int service_controller_init(service_controller_t* controller);
void service_controller_shutdown(service_controller_t* controller);
int service_controller_run(service_controller_t* controller);

#endif
