#ifndef SIRAH_APISERVER_SERVER_H
#define SIRAH_APISERVER_SERVER_H

int api_server_init(int port);
int api_server_run(void);
void api_server_shutdown(void);

#endif
