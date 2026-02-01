#ifndef SIRAH_SCHEDULER_SCHEDULER_H
#define SIRAH_SCHEDULER_SCHEDULER_H

int scheduler_init(const char* apiserver_url);
int scheduler_run(void);
void scheduler_shutdown(void);

#endif
