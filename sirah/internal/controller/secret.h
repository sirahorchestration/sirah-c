#ifndef SECRET_CONTROLLER_H
#define SECRET_CONTROLLER_H

#include "types/config.h"

typedef struct {
    k8s_secret_t** secrets;
    int num_secrets;
} secret_controller_t;

// Create a new Secret controller
secret_controller_t* secret_controller_new(void);

// Create a Secret
int secret_create(secret_controller_t* controller, k8s_secret_t* secret);

// Get a Secret by name and namespace
k8s_secret_t* secret_get(secret_controller_t* controller,
                        const char* name, const char* namespace);

// List all Secrets in a namespace (namespace=NULL for all)
k8s_secret_t** secret_list(secret_controller_t* controller,
                          const char* namespace, int* count);

// Update a Secret
int secret_update(secret_controller_t* controller, k8s_secret_t* secret);

// Delete a Secret
int secret_delete(secret_controller_t* controller,
                 const char* name, const char* namespace);

// Free the controller
void secret_controller_free(secret_controller_t* controller);

#endif // SECRET_CONTROLLER_H
