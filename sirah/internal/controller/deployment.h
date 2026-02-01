#ifndef SIRAH_CONTROLLER_DEPLOYMENT_H
#define SIRAH_CONTROLLER_DEPLOYMENT_H

typedef struct deployment {
    char* name;
    char* namespace;
    int desired_replicas;
    int ready_replicas;
} deployment_t;

int deployment_controller_run();
int deployment_controller_reconcile(deployment_t* deployment);
int deployment_controller_shutdown();

#endif
