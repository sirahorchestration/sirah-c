// internal/controller/pod_controller.h
#ifndef SIRAH_POD_CONTROLLER_H
#define SIRAH_POD_CONTROLLER_H

int pod_controller_init(const char* apiserver_url);
int pod_controller_run(void);
int pod_controller_add_pod(const char* namespace, const char* name,
                          const char* image, int memory_mb);
void pod_controller_shutdown(void);

#endif // SIRAH_POD_CONTROLLER_H
