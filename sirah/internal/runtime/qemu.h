#ifndef SIRAH_QEMU_H
#define SIRAH_QEMU_H

#include "runtime.h"

// Initialize QEMU runtime
int qemu_init(void);

// Spawn a unikernel in QEMU
int qemu_spawn(vm_spec_t* spec);

// Stop a running QEMU VM
int qemu_stop(const char* vm_id);

// Get status of QEMU VM
int qemu_get_status(const char* vm_id, char* status, size_t size);

// Cleanup QEMU resources
int qemu_cleanup(void);

#endif // SIRAH_QEMU_H
