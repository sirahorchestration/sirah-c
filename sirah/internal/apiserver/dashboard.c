// internal/apiserver/dashboard.c
// Operational dashboard implementation

#include "dashboard.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <json-c/json.h>

int dashboard_handle_request(const char* path, const char* method,
                            const char* body, char* response_buffer,
                            int* response_code) {
    if (!path || !response_buffer) return -1;
    
    // GET /dashboard - Main dashboard HTML
    if (strcmp(path, "/dashboard") == 0 || strcmp(path, "/dashboard/") == 0) {
        const char* html = R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Sirah Kubernetes Dashboard</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: #f5f5f5; }
        .container { max-width: 1400px; margin: 0 auto; padding: 20px; }
        header { background: #1976d2; color: white; padding: 20px; margin: -20px -20px 20px -20px; }
        h1 { margin: 0; font-size: 28px; }
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; margin-bottom: 20px; }
        .card { background: white; border-radius: 4px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); padding: 20px; }
        .card h2 { color: #1976d2; margin-bottom: 15px; font-size: 18px; }
        .stat { display: flex; justify-content: space-between; padding: 8px 0; border-bottom: 1px solid #eee; }
        .stat:last-child { border-bottom: none; }
        .stat-label { color: #666; }
        .stat-value { font-weight: 600; color: #333; }
        table { width: 100%; border-collapse: collapse; margin-top: 10px; }
        th { background: #f5f5f5; padding: 10px; text-align: left; font-weight: 600; color: #666; }
        td { padding: 10px; border-bottom: 1px solid #eee; }
        tr:hover { background: #f9f9f9; }
        .status { display: inline-block; padding: 4px 8px; border-radius: 3px; font-size: 12px; font-weight: 600; }
        .status-running { background: #e8f5e9; color: #2e7d32; }
        .status-pending { background: #fff3e0; color: #e65100; }
        .status-failed { background: #ffebee; color: #c62828; }
        .refresh { background: #1976d2; color: white; border: none; padding: 10px 20px; border-radius: 4px; cursor: pointer; }
        .refresh:hover { background: #1565c0; }
        .metrics { display: grid; grid-template-columns: repeat(4, 1fr); gap: 15px; }
        .metric-box { background: #f5f5f5; padding: 15px; border-radius: 4px; text-align: center; }
        .metric-value { font-size: 24px; font-weight: bold; color: #1976d2; }
        .metric-label { color: #999; font-size: 12px; margin-top: 5px; }
    </style>
</head>
<body>
    <header>
        <h1>Sirah Kubernetes Dashboard</h1>
        <p style="margin-top: 5px; opacity: 0.9;">Real-time cluster monitoring</p>
    </header>
    
    <div class="container">
        <div style="margin-bottom: 20px;">
            <button class="refresh" onclick="refreshData()">Refresh</button>
        </div>
        
        <div class="metrics">
            <div class="metric-box">
                <div class="metric-label">Total Pods</div>
                <div class="metric-value" id="total-pods">-</div>
            </div>
            <div class="metric-box">
                <div class="metric-label">Running</div>
                <div class="metric-value" id="running-pods">-</div>
            </div>
            <div class="metric-box">
                <div class="metric-label">Nodes</div>
                <div class="metric-value" id="total-nodes">-</div>
            </div>
            <div class="metric-box">
                <div class="metric-label">Namespaces</div>
                <div class="metric-value" id="total-ns">-</div>
            </div>
        </div>
        
        <div class="grid">
            <div class="card">
                <h2>Cluster Status</h2>
                <div class="stat">
                    <span class="stat-label">API Server</span>
                    <span class="stat-value" id="api-status">Unknown</span>
                </div>
                <div class="stat">
                    <span class="stat-label">etcd</span>
                    <span class="stat-value" id="etcd-status">Unknown</span>
                </div>
                <div class="stat">
                    <span class="stat-label">Scheduler</span>
                    <span class="stat-value" id="scheduler-status">Unknown</span>
                </div>
                <div class="stat">
                    <span class="stat-label">Controller Manager</span>
                    <span class="stat-value" id="controller-status">Unknown</span>
                </div>
            </div>
            
            <div class="card">
                <h2>Recent Events</h2>
                <div id="events-list" style="font-size: 12px; color: #666;">
                    <p>Loading events...</p>
                </div>
            </div>
        </div>
        
        <div class="card">
            <h2>Pods</h2>
            <table id="pods-table">
                <thead>
                    <tr>
                        <th>Name</th>
                        <th>Namespace</th>
                        <th>Status</th>
                        <th>Node</th>
                        <th>Ready</th>
                        <th>Restarts</th>
                    </tr>
                </thead>
                <tbody id="pods-body">
                    <tr><td colspan="6" style="text-align: center; color: #999;">Loading pods...</td></tr>
                </tbody>
            </table>
        </div>
        
        <div class="card">
            <h2>Nodes</h2>
            <table id="nodes-table">
                <thead>
                    <tr>
                        <th>Name</th>
                        <th>Status</th>
                        <th>CPU Usage</th>
                        <th>Memory Usage</th>
                        <th>Pods</th>
                    </tr>
                </thead>
                <tbody id="nodes-body">
                    <tr><td colspan="5" style="text-align: center; color: #999;">Loading nodes...</td></tr>
                </tbody>
            </table>
        </div>
    </div>
    
    <script>
        async function refreshData() {
            try {
                // Load pods
                const podsResp = await fetch('/api/dashboard/pods');
                const podsData = await podsResp.json();
                updatePodsTable(podsData.pods || []);
                updateMetrics(podsData);
                
                // Load nodes
                const nodesResp = await fetch('/api/dashboard/nodes');
                const nodesData = await nodesResp.json();
                updateNodesTable(nodesData.nodes || []);
                
                // Load metrics
                const metricsResp = await fetch('/api/dashboard/metrics');
                const metricsData = await metricsResp.json();
                updateClusterStatus(metricsData);
            } catch (err) {
                console.error('Failed to refresh data:', err);
            }
        }
        
        function updateMetrics(data) {
            document.getElementById('total-pods').textContent = data.total_pods || 0;
            document.getElementById('running-pods').textContent = data.running_pods || 0;
            document.getElementById('total-nodes').textContent = data.total_nodes || 0;
            document.getElementById('total-ns').textContent = data.total_namespaces || 1;
        }
        
        function updatePodsTable(pods) {
            const tbody = document.getElementById('pods-body');
            tbody.innerHTML = '';
            
            if (pods.length === 0) {
                tbody.innerHTML = '<tr><td colspan="6" style="text-align: center; color: #999;">No pods found</td></tr>';
                return;
            }
            
            pods.forEach(pod => {
                const statusClass = 'status-' + (pod.phase || 'unknown').toLowerCase();
                const row = `
                    <tr>
                        <td>${pod.name}</td>
                        <td>${pod.namespace || 'default'}</td>
                        <td><span class="status ${statusClass}">${pod.phase || 'Unknown'}</span></td>
                        <td>${pod.node || '-'}</td>
                        <td>${pod.ready_containers || 0}/${pod.total_containers || 0}</td>
                        <td>${pod.restarts || 0}</td>
                    </tr>
                `;
                tbody.innerHTML += row;
            });
        }
        
        function updateNodesTable(nodes) {
            const tbody = document.getElementById('nodes-body');
            tbody.innerHTML = '';
            
            if (nodes.length === 0) {
                tbody.innerHTML = '<tr><td colspan="5" style="text-align: center; color: #999;">No nodes found</td></tr>';
                return;
            }
            
            nodes.forEach(node => {
                const row = `
                    <tr>
                        <td>${node.name}</td>
                        <td><span class="status status-${node.status ? 'running' : 'failed'}">${node.status || 'NotReady'}</span></td>
                        <td>${node.cpu_usage || '-'}</td>
                        <td>${node.memory_usage || '-'}</td>
                        <td>${node.pod_count || 0}</td>
                    </tr>
                `;
                tbody.innerHTML += row;
            });
        }
        
        function updateClusterStatus(data) {
            document.getElementById('api-status').textContent = data.api_ok ? '✓ OK' : '✗ Error';
            document.getElementById('etcd-status').textContent = data.etcd_ok ? '✓ OK' : '✗ Error';
            document.getElementById('scheduler-status').textContent = data.scheduler_ok ? '✓ OK' : '✗ N/A';
            document.getElementById('controller-status').textContent = data.controller_ok ? '✓ OK' : '✗ N/A';
        }
        
        // Auto-refresh every 10 seconds
        setInterval(refreshData, 10000);
        refreshData();
    </script>
</body>
</html>)";
        
        strncpy(response_buffer, html, 16383);
        *response_code = 200;
        return 0;
    }
    
    // GET /api/dashboard/pods - Dashboard metrics from etcd
    if (strcmp(path, "/api/dashboard/pods") == 0) {
        // Query pods from etcd (not from in-memory pod_store which is now removed)
        // For now, return placeholder metrics
        
        json_object* resp = json_object_new_object();
        json_object* pods_array = json_object_new_array();
        
        // TODO: Query etcd to get actual pod counts and data
        // For MVP, return empty but valid response
        
        json_object_object_add(resp, "total_pods", json_object_new_int(0));
        json_object_object_add(resp, "running_pods", json_object_new_int(0));
        json_object_object_add(resp, "total_namespaces", json_object_new_int(1));
        json_object_object_add(resp, "pods", pods_array);
        
        const char* json_str = json_object_to_json_string(resp);
        strncpy(response_buffer, json_str, 16383);
        json_object_put(resp);
        *response_code = 200;
        return 0;
    }
    
    // GET /api/dashboard/nodes
    if (strcmp(path, "/api/dashboard/nodes") == 0) {
        json_object* resp = json_object_new_object();
        json_object_object_add(resp, "total_nodes", json_object_new_int(0));
        json_object_object_add(resp, "nodes", json_object_new_array());
        
        const char* json_str = json_object_to_json_string(resp);
        strncpy(response_buffer, json_str, 16383);
        json_object_put(resp);
        *response_code = 200;
        return 0;
    }
    
    // GET /api/dashboard/metrics
    if (strcmp(path, "/api/dashboard/metrics") == 0) {
        json_object* resp = json_object_new_object();
        json_object_object_add(resp, "api_ok", json_object_new_boolean(true));
        json_object_object_add(resp, "etcd_ok", json_object_new_boolean(true));
        json_object_object_add(resp, "scheduler_ok", json_object_new_boolean(false));
        json_object_object_add(resp, "controller_ok", json_object_new_boolean(false));
        
        const char* json_str = json_object_to_json_string(resp);
        strncpy(response_buffer, json_str, 16383);
        json_object_put(resp);
        *response_code = 200;
        return 0;
    }
    
    *response_code = 404;
    strcpy(response_buffer, "{\"error\":\"Dashboard endpoint not found\"}");
    return -1;
}
