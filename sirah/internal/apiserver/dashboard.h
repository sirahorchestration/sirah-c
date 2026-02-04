// internal/apiserver/dashboard.h
// Operational dashboard - HTML/JS interface for cluster visualization

#ifndef K8S_DASHBOARD_H
#define K8S_DASHBOARD_H

// Dashboard request handler
int dashboard_handle_request(const char* path, const char* method,
                            const char* body, char* response_buffer,
                            int* response_code);

// Dashboard endpoints
// GET /dashboard - Main dashboard HTML
// GET /api/dashboard/pods - JSON list of pods
// GET /api/dashboard/nodes - JSON list of nodes
// GET /api/dashboard/metrics - JSON metrics summary
// GET /api/dashboard/events - JSON recent events

#endif
