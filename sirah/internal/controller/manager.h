#ifndef SIRAH_CONTROLLER_MANAGER_H
#define SIRAH_CONTROLLER_MANAGER_H

#include <stdint.h>

int controller_manager_init(const char* apiserver_url);
int controller_manager_run(void);
void controller_manager_shutdown(void);

#endif
