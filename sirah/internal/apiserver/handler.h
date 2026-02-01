#ifndef SIRAH_APISERVER_HANDLER_H
#define SIRAH_APISERVER_HANDLER_H

int api_handle_request(const char* method, const char* path, const char* body,
                       char* response_buffer, int* response_code);

#endif
