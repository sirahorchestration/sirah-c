#ifndef SIRAH_SCHEDULER_PLUGINS_H
#define SIRAH_SCHEDULER_PLUGINS_H

#include "../../pkg/types/pod.h"

int plugin_filter_by_resource(k8s_pod_t* pod, const char* node);
int plugin_score_by_load(k8s_pod_t* pod, const char* node);
int plugin_score_by_affinity(k8s_pod_t* pod, const char* node);

#endif
