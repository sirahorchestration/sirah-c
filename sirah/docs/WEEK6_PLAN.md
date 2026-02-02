# Week 6 Implementation Plan: Security & Production Readiness

**Status**: Planning Phase  
**Target Duration**: 5 days  
**Target Code**: 2500-3000 additional lines  
**Overall Progress Target**: 90%+ of MVP

---

## Executive Summary

Week 6 focuses on security hardening and production readiness, taking the fully-featured Sirah platform from feature-complete to production-ready. This phase implements:

1. **Role-Based Access Control (RBAC)** - Fine-grained permission management
2. **TLS/Certificate Management** - Secure communication and authentication
3. **High-Availability Control Plane** - Multi-master setup with leader election
4. **Kubernetes Conformance Testing** - Pass compatibility tests
5. **Production Hardening** - Error handling, monitoring, documentation

---

## Architecture Overview

### Current State (End of Week 5)

```
Control Plane (Functional)
  ├── API Server (with all endpoints)
  ├── Scheduler (advanced with plugins)
  ├── Controllers (all types)
  └── etcd (basic single-node)

Worker Nodes
  ├── Kubelet (with volume mounting)
  ├── kube-proxy (service routing)
  └── Container Runtime

Storage System
  ├── PersistentVolumes
  ├── PersistentVolumeClaims
  └── Volume mounting

Networking
  ├── CNI plugins
  ├── Services with endpoints
  └── DNS resolution

NO SECURITY:
  ❌ No authentication
  ❌ No authorization
  ❌ Plain HTTP (no TLS)
  ❌ Single master point of failure
```

### Target State (End of Week 6)

```
Control Plane (Production-Ready)
  ├── API Server (with RBAC enforcement)
  ├── Scheduler (with certificate validation)
  ├── Controllers (all types, HA)
  └── etcd (clustered, replicated)

Security Layer
  ├── TLS certificates for all components
  ├── RBAC with Roles and RoleBindings
  ├── ServiceAccount authentication
  ├── Audit logging of API access

Worker Nodes
  ├── Kubelet (with TLS client certs)
  ├── kube-proxy (secure)
  └── Container Runtime

High Availability
  ├── Multiple API servers
  ├── Leader election in controller manager
  ├── etcd cluster (3+ nodes)
  └── Load balancing for API servers

Conformance & Hardening
  ├── Pass basic K8s conformance tests
  ├── Proper error codes and messages
  ├── Comprehensive logging
  ├── Security best practices
```

---

## Week 6 Detailed Tasks

### 6.1: RBAC Implementation (1.5 days)

#### Goals
- Implement Role and RoleBinding objects
- Implement ClusterRole and ClusterRoleBinding
- Authorization logic in API server
- ServiceAccount management

#### What to Add

**Role & RoleBinding Types** (~200 lines):
```c
// Role - namespace-scoped permissions
typedef struct {
    k8s_object_meta_t metadata;    // name, namespace
    struct {
        k8s_policy_rule_t rules[100];  // "can I do X to Y?"
        int num_rules;
    } spec;
} k8s_role_t;

// Policy rule - what can be done
typedef struct {
    char* verbs[20];               // "get", "create", "delete"
    char* apiGroups[10];           // "apps", "core", ""
    char* resources[10];           // "pods", "deployments"
    char* resourceNames[10];       // Specific names (optional)
    int num_verbs, num_apigroups, num_resources, num_resourcenames;
} k8s_policy_rule_t;

// RoleBinding - assign Role to user/service account
typedef struct {
    k8s_object_meta_t metadata;    // name, namespace
    struct {
        k8s_policy_rule_t role_ref;    // References Role
        struct {
            char* kind;                // "User", "ServiceAccount", "Group"
            char* name;
            char* namespace;           // For ServiceAccount
        } subjects[100];               // Who it applies to
        int num_subjects;
    } spec;
} k8s_role_binding_t;

// ClusterRole - cluster-scoped permissions
typedef struct {
    k8s_object_meta_t metadata;    // name, NO namespace
    struct {
        k8s_policy_rule_t rules[100];
        int num_rules;
    } spec;
    // Can include non-namespaced resources (nodes, pv, namespaces)
} k8s_cluster_role_t;

// ClusterRoleBinding - cluster-scoped role assignments
typedef struct {
    k8s_object_meta_t metadata;
    struct {
        k8s_policy_rule_t role_ref;    // References ClusterRole
        struct {
            char* kind;
            char* name;
            char* namespace;
        } subjects[100];
        int num_subjects;
    } spec;
} k8s_cluster_role_binding_t;

// ServiceAccount - identity for pods
typedef struct {
    k8s_object_meta_t metadata;    // name, namespace
    struct {
        char* automount_service_account_token;  // bool as string
        // In real K8s, also has secrets array, but MVP simpler
    } spec;
} k8s_service_account_t;
```

**Authorization Engine** (~300 lines):
```c
// Check if subject can perform action
typedef struct {
    char* user;                    // Username from certificate
    char* namespace;               // Namespace for request
    char* verb;                    // "get", "create", etc
    char* api_group;               // "apps", "", "storage.k8s.io"
    char* resource;                // "pods", "deployments"
    char* resource_name;           // Specific pod name (optional)
    char* kind;                    // "User", "ServiceAccount"
} authz_request_t;

typedef struct {
    bool allowed;
    char* reason;                  // Why allowed/denied
} authz_result_t;

// Main authorization function
authz_result_t* authorize_request(authz_request_t* req) {
    authz_result_t* result = malloc(sizeof(*result));
    result->allowed = false;
    
    // 1. Get user's RoleBindings (namespace-scoped + cluster)
    k8s_role_binding_t** bindings = get_role_bindings_for_user(req->user, req->namespace);
    k8s_cluster_role_binding_t** cbindings = get_cluster_role_bindings_for_user(req->user);
    
    // 2. For each RoleBinding, get the referenced Role
    for (int i = 0; bindings[i]; i++) {
        k8s_role_t* role = api_get_role(bindings[i]->spec.role_ref.name, req->namespace);
        if (rule_matches_request(role->spec.rules, req)) {
            result->allowed = true;
            result->reason = strdup("Matched rule in Role");
            return result;
        }
    }
    
    // 3. For each ClusterRoleBinding, get the referenced ClusterRole
    for (int i = 0; cbindings[i]; i++) {
        k8s_cluster_role_t* crole = api_get_cluster_role(cbindings[i]->spec.role_ref.name);
        if (rule_matches_request(crole->spec.rules, req)) {
            result->allowed = true;
            result->reason = strdup("Matched rule in ClusterRole");
            return result;
        }
    }
    
    // 4. Check for wildcards
    if (has_wildcard_rule(bindings, cbindings)) {
        result->allowed = true;
        result->reason = strdup("Matched wildcard rule");
        return result;
    }
    
    result->reason = strdup("No matching rules found");
    return result;
}

// Check if a policy rule matches the request
bool rule_matches_request(k8s_policy_rule_t* rules, authz_request_t* req) {
    for (int r = 0; rules[r].verbs[0]; r++) {
        k8s_policy_rule_t* rule = &rules[r];
        
        // Check verb
        if (!string_in_array(req->verb, rule->verbs, rule->num_verbs)) continue;
        
        // Check API group
        if (!string_in_array(req->api_group, rule->apiGroups, rule->num_apigroups)) continue;
        
        // Check resource
        if (!string_in_array(req->resource, rule->resources, rule->num_resources)) continue;
        
        // Check resource name (if specified)
        if (rule->num_resourcenames > 0 && req->resource_name) {
            if (!string_in_array(req->resource_name, rule->resourceNames, rule->num_resourcenames)) continue;
        }
        
        return true;  // All checks passed
    }
    return false;
}

// Helper: check if string is in array (with wildcard support)
bool string_in_array(const char* str, char* array[], int count) {
    for (int i = 0; i < count; i++) {
        if (strcmp(array[i], "*") == 0) return true;  // Wildcard matches anything
        if (strcmp(array[i], str) == 0) return true;
    }
    return false;
}
```

**Default Roles** (~100 lines):
```c
// Create Kubernetes default roles on startup
void create_default_roles() {
    // cluster-admin: Full access
    k8s_cluster_role_t* admin = k8s_cluster_role_new("cluster-admin");
    admin->spec.rules[0].verbs[0] = "*";
    admin->spec.rules[0].apiGroups[0] = "*";
    admin->spec.rules[0].resources[0] = "*";
    admin->spec.num_rules = 1;
    api_create_cluster_role(admin);
    
    // edit: Can create/edit resources except RBAC
    k8s_cluster_role_t* edit = k8s_cluster_role_new("edit");
    // ... add rules for pods, deployments, etc but not roles/bindings
    
    // view: Read-only access
    k8s_cluster_role_t* view = k8s_cluster_role_new("view");
    // ... add rules for get/list/watch but not create/delete
}
```

**API Endpoints for RBAC** (~150 lines):
```c
// Endpoints needed:
// GET/POST /api/v1/roles
// GET/POST /api/v1/namespaces/{ns}/roles
// GET/POST /api/v1/rolebindings
// GET/POST /api/v1/namespaces/{ns}/rolebindings
// GET/POST /api/v1/clusterroles
// GET/POST /api/v1/clusterrolebindings
// GET/POST /api/v1/namespaces/{ns}/serviceaccounts
```

**Files to Create**:
- `pkg/types/rbac.h/c` - Role, RoleBinding, ClusterRole, ClusterRoleBinding, ServiceAccount
- `internal/apiserver/authz.h/c` - Authorization engine
- `internal/apiserver/rbac_endpoints.c` - REST endpoints for RBAC objects
- `internal/apiserver/default_roles.c` - Create default K8s roles

**Acceptance Criteria**:
- [ ] Role and RoleBinding objects can be created
- [ ] Authorization denies unauthorized requests
- [ ] Authorization allows authorized requests
- [ ] Default roles (cluster-admin, edit, view) created on startup
- [ ] RBAC rules with wildcards work
- [ ] kubectl auth can-i works
- [ ] ServiceAccounts can be created and used

---

### 6.2: TLS/Certificate Management (1.5 days)

#### Goals
- Generate certificates for API server
- Implement TLS for API server
- Client certificate authentication
- Secure kubelet-apiserver communication

#### What to Add

**Certificate Management** (~250 lines):
```c
// Certificate storage
typedef struct {
    char* name;                    // "apiserver", "kubelet-node1"
    char* cert_pem;                // PEM-encoded certificate
    char* key_pem;                 // PEM-encoded private key
    time_t issued_at;
    time_t expires_at;
    char* subject;                 // CN from certificate
    char* issuer;                  // CA that issued it
} k8s_certificate_t;

// CA certificate
typedef struct {
    char* ca_cert_pem;             // CA certificate
    char* ca_key_pem;              // CA private key
    char* cn;                      // Common name (e.g. "kubernetes")
    int validity_days;             // How long certs are valid
} k8s_ca_t;

// Initialize CA on cluster startup
k8s_ca_t* ca_initialize(const char* cn, int validity_days) {
    k8s_ca_t* ca = malloc(sizeof(*ca));
    
    // Generate CA key pair using OpenSSL
    // Create self-signed CA certificate
    // Store in /etc/kubernetes/pki/ca.crt and ca.key
    
    ca->cn = strdup(cn);
    ca->validity_days = validity_days;
    ca->ca_cert_pem = read_file("/etc/kubernetes/pki/ca.crt");
    ca->ca_key_pem = read_file("/etc/kubernetes/pki/ca.key");
    
    return ca;
}

// Sign certificate request
k8s_certificate_t* ca_sign_cert(k8s_ca_t* ca, const char* cn, const char* group) {
    k8s_certificate_t* cert = malloc(sizeof(*cert));
    cert->name = strdup(cn);
    cert->subject = strdup(cn);
    cert->issuer = strdup(ca->cn);
    
    // Generate private key
    // Create certificate request with CN and group
    // Sign with CA private key
    // Store in /etc/kubernetes/pki/{cn}.crt and {cn}.key
    
    cert->issued_at = time(NULL);
    cert->expires_at = cert->issued_at + (ca->validity_days * 86400);
    
    return cert;
}

// Load certificate from file
k8s_certificate_t* cert_load(const char* cert_path, const char* key_path) {
    k8s_certificate_t* cert = malloc(sizeof(*cert));
    cert->cert_pem = read_file(cert_path);
    cert->key_pem = read_file(key_path);
    // Parse certificate to extract CN, expires_at, etc
    return cert;
}

// Verify certificate validity
bool cert_is_valid(k8s_certificate_t* cert) {
    time_t now = time(NULL);
    return now >= cert->issued_at && now < cert->expires_at;
}
```

**TLS Server Setup** (~200 lines):
```c
// HTTPS listener for API server
typedef struct {
    int port;                      // 6443
    k8s_certificate_t* server_cert;
    k8s_ca_t* ca;                  // For verifying client certs
    struct MHD_Daemon* daemon;     // libmicrohttpd daemon
} api_server_tls_t;

// Start TLS-enabled API server
api_server_tls_t* api_server_tls_start(int port, 
                                      k8s_certificate_t* cert,
                                      k8s_ca_t* ca) {
    api_server_tls_t* server = malloc(sizeof(*server));
    server->port = port;
    server->server_cert = cert;
    server->ca = ca;
    
    // Configure libmicrohttpd for TLS
    struct MHD_Daemon* daemon = MHD_start_daemon(
        MHD_USE_SSL,
        port,
        NULL, NULL,                 // Accept policy
        &api_request_handler,       // Handler
        server,
        MHD_OPTION_HTTPS_CERT_CALLBACK, &cert_callback,
        MHD_OPTION_HTTPS_KEY_CALLBACK, &key_callback,
        MHD_OPTION_END
    );
    
    server->daemon = daemon;
    return server;
}

// Verify client certificate from request
bool verify_client_cert(struct MHD_Connection* conn, k8s_ca_t* ca) {
    const char* cert_header = MHD_lookup_connection_value(
        conn, MHD_HEADER_KIND, "X-SSL-Client-Cert"
    );
    
    if (!cert_header) {
        return false;  // No client cert
    }
    
    // Verify certificate was signed by CA
    // Extract CN (username) from certificate
    // Verify certificate is not expired
    // Return true if valid
    return true;
}

// Extract username from client certificate
const char* extract_username_from_cert(const char* cert_pem) {
    // Parse certificate
    // Extract CN (Common Name) field
    // CN format: "username" or "system:admin"
    // Return CN
    return "username";
}
```

**Kubelet TLS** (~150 lines):
```c
// Kubelet client certificate configuration
typedef struct {
    char* client_cert_path;
    char* client_key_path;
    char* ca_cert_path;            // CA cert for verifying API server
} kubelet_tls_config_t;

// Setup TLS for kubelet API calls
int kubelet_setup_tls(kubelet_t* kubelet, const char* node_name, k8s_ca_t* ca) {
    // 1. Generate kubelet certificate
    k8s_certificate_t* kubelet_cert = ca_sign_cert(ca, node_name, "system:kubelet");
    
    // 2. Save to /var/lib/kubelet/{node_name}.crt and .key
    
    // 3. Store path in kubelet struct
    kubelet->tls_config.client_cert_path = "/var/lib/kubelet/kubelet.crt";
    kubelet->tls_config.client_key_path = "/var/lib/kubelet/kubelet.key";
    kubelet->tls_config.ca_cert_path = "/etc/kubernetes/pki/ca.crt";
    
    return 0;
}

// Make TLS-authenticated API call from kubelet
int kubelet_api_call(kubelet_t* kubelet, const char* endpoint, char** response) {
    CURL* curl = curl_easy_init();
    
    // Set client certificate
    curl_easy_setopt(curl, CURLOPT_SSLCERT, kubelet->tls_config.client_cert_path);
    curl_easy_setopt(curl, CURLOPT_SSLKEY, kubelet->tls_config.client_key_path);
    
    // Verify API server certificate against CA
    curl_easy_setopt(curl, CURLOPT_CAINFO, kubelet->tls_config.ca_cert_path);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    
    // Make request
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    return 0;
}
```

**Certificate Rotation** (~100 lines):
```c
// Monitor certificate expiry
int cert_rotation_controller_loop() {
    while (running) {
        // Check all certificates in /etc/kubernetes/pki/
        // If expiring within 30 days:
        //   - Log warning
        //   - Generate new certificate
        //   - Replace old one
        // If expired:
        //   - Log critical error
        //   - Require manual intervention
        
        sleep(86400);  // Check daily
    }
    return 0;
}
```

**Files to Create**:
- `pkg/types/certificate.h/c` - Certificate types and functions
- `internal/ca/ca.h/c` - CA initialization and certificate signing
- `internal/apiserver/tls.h/c` - TLS server setup and client cert verification
- `cmd/kubeadm/cert.c` - Certificate generation commands (can be external tool)

**Acceptance Criteria**:
- [ ] API server listens on HTTPS (6443)
- [ ] Client certificate required for API calls
- [ ] Certificate CN extracted as username
- [ ] Kubelet verifies API server certificate
- [ ] API server verifies kubelet certificate
- [ ] Certificates can be rotated
- [ ] kubectl with kubeconfig works
- [ ] Self-signed certificates work

---

### 6.3: High-Availability Control Plane (1.5 days)

#### Goals
- Multi-master API server setup
- Leader election in controller manager
- etcd cluster (Raft consensus)
- Load balancing for API servers

#### What to Add

**etcd Cluster (Raft Implementation)** (~300 lines):
```c
// Raft node state
typedef enum {
    RAFT_FOLLOWER = 0,
    RAFT_CANDIDATE = 1,
    RAFT_LEADER = 2
} raft_state_t;

typedef struct {
    int node_id;
    char* node_name;
    raft_state_t state;
    int current_term;
    int voted_for;                 // Node ID this node voted for in current term
    
    // Log entries (replicated state machine)
    raft_log_entry_t* log;
    int log_index;
    
    // State machine (in-memory key-value store)
    char* keys[10000];
    char* values[10000];
    int state_count;
    
    // Raft consensus
    int commit_index;              // Index of highest committed entry
    int last_applied;              // Index of highest applied entry
    
    // For leaders
    int next_index[10];            // For each follower
    int match_index[10];           // For each follower
    
    // Network
    int peer_count;
    struct {
        int id;
        char* host;
        int port;
    } peers[10];
} raft_node_t;

// Initialize Raft node
raft_node_t* raft_node_new(int node_id, const char* node_name) {
    raft_node_t* node = calloc(1, sizeof(*node));
    node->node_id = node_id;
    node->node_name = strdup(node_name);
    node->state = RAFT_FOLLOWER;
    node->current_term = 0;
    node->voted_for = -1;
    node->log = malloc(sizeof(raft_log_entry_t) * 100000);
    node->log_index = 0;
    return node;
}

// Raft election timer (becomes candidate if no heartbeat from leader)
void* raft_election_timer_thread(void* arg) {
    raft_node_t* node = (raft_node_t*)arg;
    int election_timeout = 150 + (rand() % 150);  // 150-300ms
    int last_heartbeat = time(NULL);
    
    while (running) {
        if (node->state == RAFT_LEADER) {
            // Leader sends heartbeats
            send_heartbeats(node);
            sleep_ms(50);
        } else {
            // Follower/Candidate wait for heartbeat
            if (time(NULL) - last_heartbeat > election_timeout / 1000) {
                // No heartbeat received, start election
                raft_start_election(node);
            }
            sleep_ms(10);
        }
    }
    return NULL;
}

// Start election
void raft_start_election(raft_node_t* node) {
    node->state = RAFT_CANDIDATE;
    node->current_term++;
    node->voted_for = node->node_id;  // Vote for self
    
    int votes = 1;  // Self
    
    // Request votes from all peers
    for (int i = 0; i < node->peer_count; i++) {
        if (node->peers[i].id == node->node_id) continue;
        
        vote_result_t* result = request_vote(
            node, 
            node->peers[i].host, 
            node->peers[i].port,
            node->current_term,
            node->node_id,
            node->log_index,
            node->log[node->log_index].term
        );
        
        if (result && result->vote_granted) {
            votes++;
        }
    }
    
    // Check if won election (majority votes)
    if (votes > node->peer_count / 2) {
        node->state = RAFT_LEADER;
        // Initialize nextIndex for all followers
        for (int i = 0; i < node->peer_count; i++) {
            node->next_index[i] = node->log_index + 1;
            node->match_index[i] = 0;
        }
    } else {
        node->state = RAFT_FOLLOWER;
    }
}

// Append entries (heartbeat + log replication)
int raft_append_entries(raft_node_t* node, raft_node_t* peer,
                       raft_log_entry_t* entries, int num_entries) {
    // Send log entries to follower
    // Follower appends if consistent
    // Leader waits for quorum replication
    return 0;
}

// Commit log entries
void raft_commit_entries(raft_node_t* node) {
    // When majority has replicated entry
    // Move commit_index forward
    // Apply to state machine
    for (int i = node->last_applied + 1; i <= node->commit_index; i++) {
        apply_log_entry(node, &node->log[i]);
        node->last_applied = i;
    }
}
```

**etcd API (gRPC-like)** (~150 lines):
```c
// Simple etcd-like API for single-node or clustered
int etcd_put(raft_node_t* node, const char* key, const char* value) {
    if (node->state != RAFT_LEADER) {
        // Redirect to leader
        return -1;
    }
    
    // 1. Create log entry
    raft_log_entry_t entry;
    entry.term = node->current_term;
    entry.type = LOG_TYPE_PUT;
    entry.key = strdup(key);
    entry.value = strdup(value);
    entry.index = ++node->log_index;
    node->log[entry.index] = entry;
    
    // 2. Replicate to followers
    for (int i = 0; i < node->peer_count; i++) {
        replicate_log_entry(node, i, &entry);
    }
    
    // 3. Wait for quorum
    int replicated_count = 1;  // Self
    while (replicated_count <= node->peer_count / 2) {
        sleep_ms(10);
        // Check match_index for each peer
        for (int i = 0; i < node->peer_count; i++) {
            if (node->match_index[i] >= entry.index) {
                replicated_count++;
            }
        }
    }
    
    // 4. Commit and apply
    node->commit_index = entry.index;
    apply_log_entry(node, &entry);
    
    return 0;
}

int etcd_get(raft_node_t* node, const char* key, char** value) {
    // Consistent read - might need quorum
    // For now, simple leader read
    
    for (int i = 0; i < node->state_count; i++) {
        if (strcmp(node->keys[i], key) == 0) {
            *value = node->values[i];
            return 0;
        }
    }
    return -1;  // Key not found
}

void apply_log_entry(raft_node_t* node, raft_log_entry_t* entry) {
    if (entry->type == LOG_TYPE_PUT) {
        // Add to state machine
        int i = node->state_count;
        node->keys[i] = strdup(entry->key);
        node->values[i] = strdup(entry->value);
        node->state_count++;
    } else if (entry->type == LOG_TYPE_DELETE) {
        // Remove from state machine
        for (int i = 0; i < node->state_count; i++) {
            if (strcmp(node->keys[i], entry->key) == 0) {
                // Shift remaining entries
                for (int j = i; j < node->state_count - 1; j++) {
                    node->keys[j] = node->keys[j+1];
                    node->values[j] = node->values[j+1];
                }
                node->state_count--;
                break;
            }
        }
    }
}
```

**Controller Manager Leader Election** (~150 lines):
```c
// Leader election for controller manager
typedef struct {
    char* lock_name;               // e.g., "controller-manager-lock"
    char* leader_id;               // Node/hostname of current leader
    int lease_duration_seconds;    // 15 seconds
    int renew_deadline_seconds;    // 10 seconds
} leader_election_t;

// Acquire leadership
bool leader_election_acquire(leader_election_t* election, 
                            const char* my_id, 
                            etcd_client_t* etcd) {
    // Try to create lock with TTL
    // If already exists, read current leader
    // If leader lease expired, become leader
    
    int result = etcd_compare_and_swap(
        etcd,
        election->lock_name,
        "",  // old value (empty = create)
        my_id,
        election->lease_duration_seconds
    );
    
    if (result == 0) {
        election->leader_id = strdup(my_id);
        return true;  // Acquired lock
    }
    
    return false;  // Lock held by someone else
}

// Renew leadership lease
void leader_election_renew(leader_election_t* election, etcd_client_t* etcd) {
    // Periodically renew the lock
    // Keep lease alive by updating TTL
    
    etcd_compare_and_swap(
        etcd,
        election->lock_name,
        election->leader_id,
        election->leader_id,
        election->lease_duration_seconds
    );
}

// Controller manager main loop
void* controller_manager_loop(void* arg) {
    controller_manager_t* mgr = (controller_manager_t*)arg;
    leader_election_t* election = mgr->election;
    
    while (running) {
        if (leader_election_acquire(election, mgr->node_name, mgr->etcd)) {
            // We are leader - run controllers
            run_all_controllers(mgr);
            
            // Periodically renew lease
            sleep(5);
            leader_election_renew(election, mgr->etcd);
        } else {
            // Not leader - wait and retry
            sleep(1);
        }
    }
    return NULL;
}
```

**API Server Load Balancing** (~100 lines):
```c
// Simple load balancer for multiple API servers
typedef struct {
    int api_server_count;
    struct {
        char* host;
        int port;
        bool healthy;
        time_t last_health_check;
    } servers[10];
} api_lb_t;

api_lb_t* api_lb_new() {
    api_lb_t* lb = calloc(1, sizeof(*lb));
    return lb;
}

// Add API server to load balancer
void api_lb_add_server(api_lb_t* lb, const char* host, int port) {
    int i = lb->api_server_count++;
    lb->servers[i].host = strdup(host);
    lb->servers[i].port = port;
    lb->servers[i].healthy = true;
}

// Get next API server (round-robin)
const char* api_lb_get_server(api_lb_t* lb, int* port) {
    static int round_robin_idx = 0;
    int attempts = 0;
    
    // Try servers in round-robin fashion
    while (attempts < lb->api_server_count) {
        int idx = (round_robin_idx++) % lb->api_server_count;
        
        if (lb->servers[idx].healthy) {
            *port = lb->servers[idx].port;
            return lb->servers[idx].host;
        }
        
        attempts++;
    }
    
    return NULL;  // All servers down
}

// Health check loop
void* api_lb_health_check_loop(void* arg) {
    api_lb_t* lb = (api_lb_t*)arg;
    
    while (running) {
        for (int i = 0; i < lb->api_server_count; i++) {
            bool healthy = api_server_is_healthy(lb->servers[i].host, lb->servers[i].port);
            lb->servers[i].healthy = healthy;
            lb->servers[i].last_health_check = time(NULL);
        }
        
        sleep(5);
    }
    return NULL;
}
```

**Files to Create**:
- `internal/etcd/raft.h/c` - Raft consensus implementation
- `internal/etcd/etcd.h/c` - etcd API and state machine
- `internal/controller/leader_election.h/c` - Leader election
- `internal/apiserver/load_balancer.h/c` - API server load balancing

**Acceptance Criteria**:
- [ ] Multiple etcd nodes can form cluster
- [ ] Log replication works
- [ ] Leader election works
- [ ] Failed leader replaced by new leader
- [ ] Controller manager runs only on leader
- [ ] API server load balancer distributes requests
- [ ] API servers automatically discover each other
- [ ] Can tolerate failure of 1 node in 3-node cluster

---

### 6.4: Kubernetes Conformance Testing (1 day)

#### Goals
- Pass basic Kubernetes conformance tests
- Implement missing small features
- Fix edge cases

#### What to Add

**Test Harness** (~200 lines):
```c
// Test framework
typedef int (*test_fn_t)();

typedef struct {
    char* test_name;
    test_fn_t test_fn;
    bool passed;
    char* error_message;
} test_case_t;

// Pod lifecycle test
int test_pod_create_get_delete() {
    // 1. Create pod
    k8s_pod_t* pod = k8s_pod_new("test-pod", "default");
    int result = api_create_pod(pod);
    if (result != 0) return 1;  // FAIL
    
    // 2. Get pod
    k8s_pod_t* retrieved = api_get_pod("test-pod", "default");
    if (!retrieved) return 1;
    
    // 3. Delete pod
    result = api_delete_pod("test-pod", "default");
    if (result != 0) return 1;
    
    return 0;  // PASS
}

// Deployment scaling test
int test_deployment_scaling() {
    // 1. Create deployment
    k8s_deployment_t* deploy = k8s_deployment_new("scale-test", "default");
    deploy->spec.replicas = 3;
    api_create_deployment(deploy);
    
    // 2. Wait for pods
    sleep(5);
    k8s_pod_t** pods = api_get_pods_for_deployment("scale-test");
    if (count_pods(pods) != 3) return 1;
    
    // 3. Scale up
    deploy->spec.replicas = 5;
    api_update_deployment(deploy);
    sleep(5);
    if (count_pods(api_get_pods_for_deployment("scale-test")) != 5) return 1;
    
    // 4. Scale down
    deploy->spec.replicas = 2;
    api_update_deployment(deploy);
    sleep(5);
    if (count_pods(api_get_pods_for_deployment("scale-test")) != 2) return 1;
    
    return 0;  // PASS
}

// Service endpoints test
int test_service_endpoints() {
    // 1. Create pods
    k8s_pod_t* pod1 = k8s_pod_new("test-1", "default");
    k8s_pod_t* pod2 = k8s_pod_new("test-2", "default");
    api_create_pod(pod1);
    api_create_pod(pod2);
    
    // 2. Create service
    k8s_service_t* svc = k8s_service_new("test-svc", "default");
    svc->spec.selector_label_key = "app";
    svc->spec.selector_label_value = "test";
    api_create_service(svc);
    
    // 3. Verify endpoints created
    sleep(2);
    k8s_endpoint_t* endpoints = api_get_endpoints("test-svc", "default");
    if (count_endpoint_addresses(endpoints) != 2) return 1;
    
    return 0;  // PASS
}

// Run all tests
int run_conformance_tests() {
    test_case_t tests[] = {
        {"Pod create/get/delete", test_pod_create_get_delete},
        {"Deployment scaling", test_deployment_scaling},
        {"Service endpoints", test_service_endpoints},
        // ... many more tests
        {NULL, NULL}
    };
    
    int passed = 0, failed = 0;
    for (int i = 0; tests[i].test_name; i++) {
        tests[i].passed = (tests[i].test_fn() == 0);
        if (tests[i].passed) {
            printf("✓ %s\n", tests[i].test_name);
            passed++;
        } else {
            printf("✗ %s\n", tests[i].test_name);
            failed++;
        }
    }
    
    printf("\nResults: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
```

**Missing Feature Implementations** (~300 lines):
```
- Pod preemption logic
- Node drain for maintenance
- Pod disruption budgets
- Network policies enforcement
- Resource quota enforcement improvements
- Namespace deletion cascades
- Owner references and garbage collection
- Pod affinity/anti-affinity refinements
- Init containers execution order
```

**Files to Create**:
- `tests/conformance.c/h` - Conformance test suite
- `tests/e2e.c/h` - End-to-end integration tests
- `tests/unit.c/h` - Unit tests for components

**Acceptance Criteria**:
- [ ] 50+ conformance tests pass
- [ ] Pod lifecycle tests pass
- [ ] Deployment tests pass
- [ ] Service tests pass
- [ ] StatefulSet tests pass
- [ ] RBAC tests pass
- [ ] TLS tests pass

---

### 6.5: Production Hardening & Documentation (1 day)

#### Goals
- Comprehensive error handling
- Logging and observability
- Production documentation
- Security best practices

#### What to Add

**Structured Logging** (~150 lines):
```c
// Logging levels
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3,
    LOG_CRITICAL = 4
} log_level_t;

typedef struct {
    log_level_t level;
    time_t timestamp;
    char* component;               // "apiserver", "scheduler", "kubelet"
    char* message;
    char* error;                   // Error details if any
    struct {
        char* key;
        char* value;
    } fields[20];                  // Structured fields
    int num_fields;
} log_entry_t;

void log_info(const char* component, const char* message) {
    log_entry_t entry = {0};
    entry.level = LOG_INFO;
    entry.timestamp = time(NULL);
    entry.component = (char*)component;
    entry.message = (char*)message;
    
    printf("[%ld] [%s] [INFO] %s\n", entry.timestamp, component, message);
    // Also write to file for persistence
    append_log_file(&entry);
}

void log_error(const char* component, const char* message, const char* error) {
    log_entry_t entry = {0};
    entry.level = LOG_ERROR;
    entry.timestamp = time(NULL);
    entry.component = (char*)component;
    entry.message = (char*)message;
    entry.error = (char*)error;
    
    fprintf(stderr, "[%ld] [%s] [ERROR] %s: %s\n", 
            entry.timestamp, component, message, error);
    append_log_file(&entry);
}

// Structured logging with fields
void log_with_fields(log_level_t level, const char* component,
                    const char* message, const char* field_key,
                    const char* field_value, ...) {
    // Support multiple key-value pairs
    // Useful for: pod_name="test", namespace="default"
}
```

**Audit Logging** (~100 lines):
```c
// Audit log for all API changes
typedef struct {
    time_t timestamp;
    char* user;                    // From certificate CN
    char* namespace;
    char* verb;                    // "create", "update", "delete"
    char* api_group;
    char* resource;                // "pods", "deployments"
    char* resource_name;
    char* status_code;             // HTTP status
    char* request_object;          // JSON of requested object
    char* response_object;         // JSON of response
} audit_event_t;

void audit_log_request(audit_event_t* event) {
    // Write to audit log file
    // Format: JSON lines (one event per line)
    // Path: /var/log/kubernetes/audit/audit.log
    
    char json[4096];
    snprintf(json, sizeof(json),
        "{\"timestamp\":\"%ld\",\"user\":\"%s\",\"verb\":\"%s\","
        "\"resource\":\"%s\",\"name\":\"%s\",\"status_code\":\"%s\"}\n",
        event->timestamp, event->user, event->verb,
        event->resource, event->resource_name, event->status_code);
    
    FILE* f = fopen("/var/log/kubernetes/audit/audit.log", "a");
    fputs(json, f);
    fclose(f);
}
```

**Health Checks** (~100 lines):
```c
// Readiness probe endpoint
int healthz_ready_handler(struct MHD_Connection* conn, void* cls) {
    // Check if all controllers are running
    // Check if etcd is accessible
    // Check if scheduler is running
    
    if (all_components_healthy()) {
        return http_response_ok(conn, "{\"status\":\"ready\"}");
    } else {
        return http_response_service_unavailable(conn, 
            "{\"status\":\"not ready\",\"reason\":\"component unhealthy\"}");
    }
}

// Liveness probe endpoint
int healthz_live_handler(struct MHD_Connection* conn, void* cls) {
    // Simple check that process is running
    return http_response_ok(conn, "{\"status\":\"alive\"}");
}

// Detailed health status
typedef struct {
    bool apiserver_healthy;
    bool scheduler_healthy;
    bool controller_healthy;
    bool etcd_healthy;
    char* unhealthy_components[10];
    int unhealthy_count;
} cluster_health_t;

cluster_health_t* get_cluster_health() {
    cluster_health_t* health = calloc(1, sizeof(*health));
    
    // Check each component
    health->apiserver_healthy = (api_server_running);
    health->scheduler_healthy = (scheduler_running);
    health->controller_healthy = (controller_running);
    health->etcd_healthy = (etcd_cluster_healthy());
    
    // List unhealthy ones
    if (!health->apiserver_healthy) {
        health->unhealthy_components[health->unhealthy_count++] = "apiserver";
    }
    // ... etc
    
    return health;
}
```

**Production Configuration** (~150 lines):
```c
// Load configuration from files
typedef struct {
    // Server config
    char* bind_address;            // 0.0.0.0
    int port;                      // 6443
    
    // TLS config
    char* tls_cert_file;           // /etc/kubernetes/pki/apiserver.crt
    char* tls_key_file;            // /etc/kubernetes/pki/apiserver.key
    
    // etcd config
    char* etcd_endpoints[10];      // etcd cluster addresses
    int etcd_endpoint_count;
    
    // Logging
    char* log_file;                // /var/log/kubernetes/apiserver.log
    log_level_t log_level;         // DEBUG, INFO, WARN, ERROR
    
    // Feature gates
    bool enable_rbac;              // true
    bool enable_audit_logging;     // true
    bool enable_profiling;         // false (for security)
    
    // Limits
    int max_request_body_bytes;    // 3MB default
    int request_timeout_seconds;   // 60
    
} apiserver_config_t;

// Load from YAML/JSON file
apiserver_config_t* load_config(const char* config_file) {
    apiserver_config_t* config = calloc(1, sizeof(*config));
    
    // Parse config file (YAML/JSON)
    // Set defaults for missing values
    // Validate configuration
    
    return config;
}
```

**Documentation Deliverables**:
- `WEEK6_QUICK_REFERENCE.md` - Quick usage guide
- `SECURITY.md` - Security best practices
- `HA_SETUP.md` - High-availability configuration
- `TROUBLESHOOTING.md` - Common issues and solutions
- `API_REFERENCE.md` - Complete API documentation
- `RBAC_GUIDE.md` - RBAC configuration guide
- `TLS_GUIDE.md` - TLS certificate management

**Files to Create**:
- `pkg/utils/logging.h/c` - Structured logging
- `internal/apiserver/audit.h/c` - Audit logging
- `internal/apiserver/health.h/c` - Health check endpoints
- `cmd/config/config.h/c` - Configuration loading

**Acceptance Criteria**:
- [ ] All components have proper error handling
- [ ] Structured logging implemented
- [ ] Audit log records all API changes
- [ ] /healthz and /ready endpoints work
- [ ] Configuration can be loaded from file
- [ ] All production documentation complete
- [ ] Security best practices documented
- [ ] No sensitive data in logs

---

## Testing Strategy

### Unit Tests
- RBAC authorization logic
- Certificate validation
- Raft consensus algorithm
- etcd state machine

### Integration Tests
- Multi-master failover
- Leader election
- Certificate rotation
- RBAC enforcement in API server

### End-to-End Tests
- Create cluster with 3 API servers
- Create workloads
- Kill API server
- Verify cluster continues working
- Verify new leader elected

### Conformance Tests
- 50+ K8s conformance tests
- kubectl compatibility
- Helm chart deployment

---

## Success Criteria

By end of Week 6:

✅ **Security**:
- RBAC fully implemented and enforced
- TLS enabled on all components
- Client certificates required
- Audit logging operational

✅ **High Availability**:
- Multi-master control plane (3 nodes)
- Raft-based etcd cluster
- Automatic leader election
- Zero-downtime upgrades possible

✅ **Production Ready**:
- 50+ conformance tests passing
- Structured logging
- Health check endpoints
- Configuration management
- Error handling comprehensive

✅ **Overall**:
- ~2500-3000 additional lines of code
- All 4 binaries build with zero errors
- ~90% of MVP complete
- Ready for deployment testing

---

## File Structure

```
sirah/
├── pkg/types/
│   ├── rbac.h/c            # NEW: RBAC types
│   └── certificate.h/c     # NEW: Certificate types
│
├── internal/
│   ├── apiserver/
│   │   ├── authz.h/c       # NEW: Authorization engine
│   │   ├── tls.h/c         # NEW: TLS server
│   │   ├── audit.h/c       # NEW: Audit logging
│   │   ├── health.h/c      # NEW: Health checks
│   │   └── rbac_endpoints.c # NEW: RBAC API endpoints
│   │
│   ├── etcd/
│   │   ├── raft.h/c        # NEW: Raft consensus
│   │   └── etcd.h/c        # NEW: etcd API
│   │
│   ├── ca/
│   │   └── ca.h/c          # NEW: Certificate authority
│   │
│   ├── controller/
│   │   └── leader_election.h/c  # NEW: Leader election
│   │
│   └── apiserver/
│       └── load_balancer.h/c    # NEW: API LB
│
├── cmd/
│   └── kubeadm/
│       └── cert.c          # NEW: Certificate generation tool
│
├── tests/
│   ├── conformance.c/h     # NEW: Conformance tests
│   ├── e2e.c/h             # NEW: E2E tests
│   └── unit.c/h            # NEW: Unit tests
│
└── Documentation/
    ├── WEEK6_QUICK_REFERENCE.md
    ├── SECURITY.md
    ├── HA_SETUP.md
    ├── TROUBLESHOOTING.md
    ├── API_REFERENCE.md
    ├── RBAC_GUIDE.md
    └── TLS_GUIDE.md
```

---

## Roadmap Beyond Week 6

### Week 7+: Production Features
- Custom Resource Definitions (CRDs)
- Operators framework
- Service mesh integration (Istio/Linkerd)
- Advanced networking (NetworkPolicies, Calico)

### Week 8+: Scale & Performance
- Cluster autoscaling
- Pod autoscaling (HPA)
- Multi-cluster federation
- Performance optimization

### Week 9+: Observability
- Prometheus metrics integration
- Fluentd logging aggregation
- Distributed tracing (Jaeger)
- UI dashboard

---

## Integration Checklist

Before starting implementation:
- [ ] Review Week 5 completion
- [ ] Plan RBAC model for MVP
- [ ] Design TLS certificate layout
- [ ] Design Raft consensus implementation
- [ ] Plan conformance test suite
- [ ] Review Kubernetes API spec for missing features

---

## Known Constraints

- **OpenSSL/TLS Library**: Need OpenSSL or similar for certificate operations
- **Raft Implementation**: Can be simplified (don't need full production Raft)
- **Conformance Tests**: ~50-100 tests should be sufficient for MVP
- **HA Setup**: 3-node cluster is sufficient (tolerate 1 failure)
- **No Kubernetes Compliance**: We're compatible but not conformant yet (that's Week 7+)

---

## References

- Kubernetes RBAC: https://kubernetes.io/docs/reference/access-authn-authz/rbac/
- X.509 Certificates: https://kubernetes.io/docs/tasks/administer-cluster/certificates/
- etcd Raft: https://github.com/etcd-io/etcd/blob/main/raft/README.md
- Kubernetes API Conventions: https://kubernetes.io/docs/reference/using-api/api-concepts/
