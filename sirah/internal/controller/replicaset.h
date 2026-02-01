#ifndef SIRAH_CONTROLLER_REPLICASET_H
#define SIRAH_CONTROLLER_REPLICASET_H

typedef struct replicaset {
    char* name;
    char* namespace;
    int desired_replicas;
    int ready_replicas;
} replicaset_t;

int replicaset_controller_run();
int replicaset_controller_reconcile(replicaset_t* replicaset);
int replicaset_controller_shutdown();

#endif
