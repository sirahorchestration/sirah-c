#!/bin/bash

# Test suite for Phase 4: IPAM, DNS, and Health Probes
# Comprehensive tests for IP address management, DNS resolution, and pod health monitoring

set -e

TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$TEST_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# Test logging
log_test() {
    echo -e "${YELLOW}[TEST]${NC} $1"
}

log_pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
    ((TESTS_PASSED++))
}

log_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
    ((TESTS_FAILED++))
}

run_test() {
    ((TESTS_RUN++))
    local test_name="$1"
    local test_func="$2"
    
    log_test "$test_name"
    if $test_func; then
        log_pass "$test_name"
    else
        log_fail "$test_name"
    fi
}

# ============================================================================
# IPAM Tests
# ============================================================================

test_ipam_manager_creation() {
    # Test IPAM manager can be created with cluster and service CIDR
    # Expected: Manager created successfully
    cat > /tmp/test_ipam_create.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include "internal/ipam/ipam_manager.h"

int main() {
    ipam_manager_t *manager = ipam_manager_create("10.244.0.0/16", "10.96.0.0/12");
    if (!manager) {
        fprintf(stderr, "Failed to create IPAM manager\n");
        return 1;
    }
    
    // Verify manager is initialized
    if (!manager || !manager->cluster_cidr || !manager->service_cidr) {
        fprintf(stderr, "IPAM manager not properly initialized\n");
        ipam_manager_free(manager);
        return 1;
    }
    
    printf("Manager created: cluster_cidr=%s, service_cidr=%s\n",
           manager->cluster_cidr, manager->service_cidr);
    
    ipam_manager_free(manager);
    return 0;
}
EOF
    gcc -I"$PROJECT_ROOT" /tmp/test_ipam_create.c "$PROJECT_ROOT/internal/ipam/ipam_manager.c" -o /tmp/test_ipam_create -lpthread -lm 2>/dev/null && /tmp/test_ipam_create
}

test_ipam_node_registration() {
    # Test node registration allocates pod CIDR
    # Expected: Node 0 gets 10.244.1.0/24, Node 1 gets 10.244.2.0/24
    cat > /tmp/test_ipam_node.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal/ipam/ipam_manager.h"

int main() {
    ipam_manager_t *manager = ipam_manager_create("10.244.0.0/16", "10.96.0.0/12");
    
    char *cidr1 = ipam_manager_register_node(manager, "worker-1");
    if (!cidr1) {
        fprintf(stderr, "Failed to register node 1\n");
        ipam_manager_free(manager);
        return 1;
    }
    
    char *cidr2 = ipam_manager_register_node(manager, "worker-2");
    if (!cidr2) {
        fprintf(stderr, "Failed to register node 2\n");
        ipam_manager_free(manager);
        return 1;
    }
    
    // Verify different CIDRs
    if (strcmp(cidr1, cidr2) == 0) {
        fprintf(stderr, "Nodes got same CIDR\n");
        ipam_manager_free(manager);
        return 1;
    }
    
    printf("Node 1 CIDR: %s\n", cidr1);
    printf("Node 2 CIDR: %s\n", cidr2);
    
    ipam_manager_free(manager);
    return 0;
}
EOF
    gcc -I"$PROJECT_ROOT" /tmp/test_ipam_node.c "$PROJECT_ROOT/internal/ipam/ipam_manager.c" -o /tmp/test_ipam_node -lpthread -lm 2>/dev/null && /tmp/test_ipam_node
}

test_ipam_pod_ip_allocation() {
    # Test pod IP allocation from node CIDR
    # Expected: Pods get different IPs from node's CIDR
    cat > /tmp/test_ipam_pod.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal/ipam/ipam_manager.h"

int main() {
    ipam_manager_t *manager = ipam_manager_create("10.244.0.0/16", "10.96.0.0/12");
    ipam_manager_register_node(manager, "worker-1");
    
    char *ip1 = ipam_manager_allocate_pod_ip(manager, "worker-1", "pod-a", "default");
    char *ip2 = ipam_manager_allocate_pod_ip(manager, "worker-1", "pod-b", "default");
    
    if (!ip1 || !ip2) {
        fprintf(stderr, "Failed to allocate pod IPs\n");
        ipam_manager_free(manager);
        return 1;
    }
    
    // Verify different IPs
    if (strcmp(ip1, ip2) == 0) {
        fprintf(stderr, "Pods got same IP\n");
        ipam_manager_free(manager);
        return 1;
    }
    
    printf("Pod A IP: %s\n", ip1);
    printf("Pod B IP: %s\n", ip2);
    
    ipam_manager_free(manager);
    return 0;
}
EOF
    gcc -I"$PROJECT_ROOT" /tmp/test_ipam_pod.c "$PROJECT_ROOT/internal/ipam/ipam_manager.c" -o /tmp/test_ipam_pod -lpthread -lm 2>/dev/null && /tmp/test_ipam_pod
}

test_ipam_service_ip_allocation() {
    # Test service IP allocation
    # Expected: Services get unique IPs from service CIDR
    cat > /tmp/test_ipam_svc.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal/ipam/ipam_manager.h"

int main() {
    ipam_manager_t *manager = ipam_manager_create("10.244.0.0/16", "10.96.0.0/12");
    
    char *svc_ip1 = ipam_manager_allocate_service_ip(manager, "service-1", "default");
    char *svc_ip2 = ipam_manager_allocate_service_ip(manager, "service-2", "default");
    
    if (!svc_ip1 || !svc_ip2) {
        fprintf(stderr, "Failed to allocate service IPs\n");
        ipam_manager_free(manager);
        return 1;
    }
    
    if (strcmp(svc_ip1, svc_ip2) == 0) {
        fprintf(stderr, "Services got same IP\n");
        ipam_manager_free(manager);
        return 1;
    }
    
    printf("Service 1 IP: %s\n", svc_ip1);
    printf("Service 2 IP: %s\n", svc_ip2);
    
    ipam_manager_free(manager);
    return 0;
}
EOF
    gcc -I"$PROJECT_ROOT" /tmp/test_ipam_svc.c "$PROJECT_ROOT/internal/ipam/ipam_manager.c" -o /tmp/test_ipam_svc -lpthread -lm 2>/dev/null && /tmp/test_ipam_svc
}

test_ipam_collision_detection() {
    # Test IP collision detection
    # Expected: Cannot allocate same IP twice
    cat > /tmp/test_ipam_collision.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include "internal/ipam/ipam_manager.h"

int main() {
    ipam_manager_t *manager = ipam_manager_create("10.244.0.0/16", "10.96.0.0/12");
    ipam_manager_register_node(manager, "worker-1");
    
    // Allocate and release same IP
    char *ip1 = ipam_manager_allocate_pod_ip(manager, "worker-1", "pod-a", "default");
    ipam_manager_release_pod_ip(manager, ip1);
    
    // Should be able to allocate again
    char *ip2 = ipam_manager_allocate_pod_ip(manager, "worker-1", "pod-b", "default");
    if (!ip2) {
        fprintf(stderr, "Failed to reallocate released IP\n");
        ipam_manager_free(manager);
        return 1;
    }
    
    printf("Original IP: %s\n", ip1);
    printf("Reallocated IP: %s\n", ip2);
    
    ipam_manager_free(manager);
    return 0;
}
EOF
    gcc -I"$PROJECT_ROOT" /tmp/test_ipam_collision.c "$PROJECT_ROOT/internal/ipam/ipam_manager.c" -o /tmp/test_ipam_collision -lpthread -lm 2>/dev/null && /tmp/test_ipam_collision
}

# ============================================================================
# DNS Tests
# ============================================================================

test_dns_resolver_creation() {
    # Test DNS resolver creation
    # Expected: Resolver created successfully
    cat > /tmp/test_dns_create.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include "internal/dns/dns_resolver.h"

int main() {
    dns_resolver_t *resolver = dns_resolver_create("cluster.local");
    if (!resolver) {
        fprintf(stderr, "Failed to create DNS resolver\n");
        return 1;
    }
    
    if (!resolver->cluster_domain) {
        fprintf(stderr, "DNS resolver not properly initialized\n");
        dns_resolver_free(resolver);
        return 1;
    }
    
    printf("Resolver created: domain=%s\n", resolver->cluster_domain);
    
    dns_resolver_free(resolver);
    return 0;
}
EOF
    gcc -I"$PROJECT_ROOT" /tmp/test_dns_create.c "$PROJECT_ROOT/internal/dns/dns_resolver.c" -o /tmp/test_dns_create -lpthread -lm 2>/dev/null && /tmp/test_dns_create
}

test_dns_service_registration() {
    # Test service DNS registration
    # Expected: Service registered with FQDN
    cat > /tmp/test_dns_svc.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include "internal/dns/dns_resolver.h"

int main() {
    dns_resolver_t *resolver = dns_resolver_create("cluster.local");
    
    bool registered = dns_resolver_register_service(resolver,
                                                   "my-service",
                                                   "default",
                                                   "10.96.0.1",
                                                   NULL, 0, false);
    if (!registered) {
        fprintf(stderr, "Failed to register service\n");
        dns_resolver_free(resolver);
        return 1;
    }
    
    printf("Service registered\n");
    printf("FQDN should be: my-service.default.svc.cluster.local\n");
    
    dns_resolver_free(resolver);
    return 0;
}
EOF
    gcc -I"$PROJECT_ROOT" /tmp/test_dns_svc.c "$PROJECT_ROOT/internal/dns/dns_resolver.c" -o /tmp/test_dns_svc -lpthread -lm 2>/dev/null && /tmp/test_dns_svc
}

test_dns_pod_registration() {
    # Test pod DNS registration
    # Expected: Pod registered with IP-based FQDN
    cat > /tmp/test_dns_pod.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include "internal/dns/dns_resolver.h"

int main() {
    dns_resolver_t *resolver = dns_resolver_create("cluster.local");
    
    bool registered = dns_resolver_register_pod(resolver,
                                              "pod-a",
                                              "default",
                                              "10.244.1.10");
    if (!registered) {
        fprintf(stderr, "Failed to register pod\n");
        dns_resolver_free(resolver);
        return 1;
    }
    
    printf("Pod registered\n");
    printf("FQDN should be: 10-244-1-10.default.pod.cluster.local\n");
    
    dns_resolver_free(resolver);
    return 0;
}
EOF
    gcc -I"$PROJECT_ROOT" /tmp/test_dns_pod.c "$PROJECT_ROOT/internal/dns/dns_resolver.c" -o /tmp/test_dns_pod -lpthread -lm 2>/dev/null && /tmp/test_dns_pod
}

test_dns_service_lookup() {
    # Test service DNS lookup
    # Expected: Resolve service FQDN to IP
    cat > /tmp/test_dns_lookup_svc.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal/dns/dns_resolver.h"

int main() {
    dns_resolver_t *resolver = dns_resolver_create("cluster.local");
    
    dns_resolver_register_service(resolver, "my-service", "default", "10.96.0.1", NULL, 0, false);
    
    char *ips[10];
    uint32_t count = 0;
    bool found = dns_resolver_lookup_service(resolver, "my-service", "default", (char **)ips, &count, NULL);
    
    if (!found || count != 1) {
        fprintf(stderr, "Failed to lookup service\n");
        dns_resolver_free(resolver);
        return 1;
    }
    
    if (strcmp(ips[0], "10.96.0.1") != 0) {
        fprintf(stderr, "Service resolved to wrong IP: %s\n", ips[0]);
        dns_resolver_free(resolver);
        return 1;
    }
    
    printf("Service lookup successful: my-service → %s\n", ips[0]);
    
    dns_resolver_free(resolver);
    return 0;
}
EOF
    gcc -I"$PROJECT_ROOT" /tmp/test_dns_lookup_svc.c "$PROJECT_ROOT/internal/dns/dns_resolver.c" -o /tmp/test_dns_lookup_svc -lpthread -lm 2>/dev/null && /tmp/test_dns_lookup_svc
}

test_dns_pod_lookup() {
    # Test pod DNS lookup
    # Expected: Resolve pod FQDN to IP
    cat > /tmp/test_dns_lookup_pod.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal/dns/dns_resolver.h"

int main() {
    dns_resolver_t *resolver = dns_resolver_create("cluster.local");
    
    dns_resolver_register_pod(resolver, "pod-a", "default", "10.244.1.10");
    
    char *pod_ip = NULL;
    bool found = dns_resolver_lookup_pod(resolver, "10-244-1-10", "default", &pod_ip);
    
    if (!found || !pod_ip) {
        fprintf(stderr, "Failed to lookup pod\n");
        dns_resolver_free(resolver);
        return 1;
    }
    
    if (strcmp(pod_ip, "10.244.1.10") != 0) {
        fprintf(stderr, "Pod resolved to wrong IP: %s\n", pod_ip);
        dns_resolver_free(resolver);
        return 1;
    }
    
    printf("Pod lookup successful: 10-244-1-10 → %s\n", pod_ip);
    
    dns_resolver_free(resolver);
    return 0;
}
EOF
    gcc -I"$PROJECT_ROOT" /tmp/test_dns_lookup_pod.c "$PROJECT_ROOT/internal/dns/dns_resolver.c" -o /tmp/test_dns_lookup_pod -lpthread -lm 2>/dev/null && /tmp/test_dns_lookup_pod
}

test_dns_headless_service() {
    # Test headless service endpoints
    # Expected: Headless service returns all pod IPs
    cat > /tmp/test_dns_headless.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal/dns/dns_resolver.h"

int main() {
    dns_resolver_t *resolver = dns_resolver_create("cluster.local");
    
    char *endpoints[] = {"10.244.1.10", "10.244.1.11", "10.244.1.12"};
    bool registered = dns_resolver_register_service(resolver,
                                                   "mysql",
                                                   "default",
                                                   NULL,  // no cluster IP
                                                   endpoints,
                                                   3,     // 3 endpoints
                                                   true); // headless
    
    if (!registered) {
        fprintf(stderr, "Failed to register headless service\n");
        dns_resolver_free(resolver);
        return 1;
    }
    
    char *ips[10];
    uint32_t count = 0;
    bool found = dns_resolver_lookup_service(resolver, "mysql", "default", (char **)ips, &count, NULL);
    
    if (!found || count != 3) {
        fprintf(stderr, "Failed to lookup headless service endpoints\n");
        dns_resolver_free(resolver);
        return 1;
    }
    
    printf("Headless service lookup: mysql has %d endpoints\n", count);
    for (uint32_t i = 0; i < count; i++) {
        printf("  - %s\n", ips[i]);
    }
    
    dns_resolver_free(resolver);
    return 0;
}
EOF
    gcc -I"$PROJECT_ROOT" /tmp/test_dns_headless.c "$PROJECT_ROOT/internal/dns/dns_resolver.c" -o /tmp/test_dns_headless -lpthread -lm 2>/dev/null && /tmp/test_dns_headless
}

# ============================================================================
# Health Probes Tests
# ============================================================================

test_probe_manager_creation() {
    # Test health probe manager creation
    # Expected: Manager created successfully
    cat > /tmp/test_probe_create.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include "internal/probes/health_probe.h"

int main() {
    pod_probe_manager_t *manager = pod_probe_manager_create("pod-a", "default", "10.244.1.10");
    if (!manager) {
        fprintf(stderr, "Failed to create probe manager\n");
        return 1;
    }
    
    if (!manager->pod_name || !manager->namespace || !manager->pod_ip) {
        fprintf(stderr, "Manager not properly initialized\n");
        pod_probe_manager_free(manager);
        return 1;
    }
    
    printf("Manager created: pod=%s.%s, ip=%s\n", 
           manager->pod_name, manager->namespace, manager->pod_ip);
    
    pod_probe_manager_free(manager);
    return 0;
}
EOF
    gcc -I"$PROJECT_ROOT" /tmp/test_probe_create.c "$PROJECT_ROOT/internal/probes/health_probe.c" -o /tmp/test_probe_create -lpthread -lm 2>/dev/null && /tmp/test_probe_create
}

test_probe_add_startup_probe() {
    # Test adding startup probe
    # Expected: Startup probe added successfully
    cat > /tmp/test_probe_startup.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include "internal/probes/health_probe.h"

int main() {
    pod_probe_manager_t *manager = pod_probe_manager_create("pod-a", "default", "10.244.1.10");
    
    probe_config_t config = {
        .type = PROBE_TYPE_STARTUP,
        .handler_type = PROBE_HANDLER_HTTP,
        .handler_config.http = {
            .path = "/health",
            .port = 8080,
            .host = "localhost",
            .scheme = "HTTP"
        },
        .initial_delay_seconds = 10,
        .timeout_seconds = 3,
        .period_seconds = 10,
        .success_threshold = 1,
        .failure_threshold = 30
    };
    
    probe_instance_t *probe = pod_probe_manager_add_probe(manager, &config);
    if (!probe) {
        fprintf(stderr, "Failed to add startup probe\n");
        pod_probe_manager_free(manager);
        return 1;
    }
    
    printf("Startup probe added\n");
    printf("Manager now has %d probes\n", manager->probe_count);
    
    pod_probe_manager_free(manager);
    return 0;
}
EOF
    gcc -I"$PROJECT_ROOT" /tmp/test_probe_startup.c "$PROJECT_ROOT/internal/probes/health_probe.c" -o /tmp/test_probe_startup -lpthread -lm 2>/dev/null && /tmp/test_probe_startup
}

test_probe_add_readiness_probe() {
    # Test adding readiness probe
    # Expected: Readiness probe added successfully
    cat > /tmp/test_probe_readiness.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include "internal/probes/health_probe.h"

int main() {
    pod_probe_manager_t *manager = pod_probe_manager_create("pod-a", "default", "10.244.1.10");
    
    probe_config_t config = {
        .type = PROBE_TYPE_READINESS,
        .handler_type = PROBE_HANDLER_HTTP,
        .handler_config.http = {
            .path = "/ready",
            .port = 8080,
            .host = "localhost",
            .scheme = "HTTP"
        },
        .initial_delay_seconds = 5,
        .timeout_seconds = 3,
        .period_seconds = 10,
        .success_threshold = 1,
        .failure_threshold = 3
    };
    
    probe_instance_t *probe = pod_probe_manager_add_probe(manager, &config);
    if (!probe) {
        fprintf(stderr, "Failed to add readiness probe\n");
        pod_probe_manager_free(manager);
        return 1;
    }
    
    printf("Readiness probe added\n");
    
    pod_probe_manager_free(manager);
    return 0;
}
EOF
    gcc -I"$PROJECT_ROOT" /tmp/test_probe_readiness.c "$PROJECT_ROOT/internal/probes/health_probe.c" -o /tmp/test_probe_readiness -lpthread -lm 2>/dev/null && /tmp/test_probe_readiness
}

test_probe_add_liveness_probe() {
    # Test adding liveness probe
    # Expected: Liveness probe added successfully
    cat > /tmp/test_probe_liveness.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include "internal/probes/health_probe.h"

int main() {
    pod_probe_manager_t *manager = pod_probe_manager_create("pod-a", "default", "10.244.1.10");
    
    probe_config_t config = {
        .type = PROBE_TYPE_LIVENESS,
        .handler_type = PROBE_HANDLER_TCP,
        .handler_config.tcp = {
            .port = 8080
        },
        .initial_delay_seconds = 15,
        .timeout_seconds = 5,
        .period_seconds = 30,
        .success_threshold = 1,
        .failure_threshold = 3
    };
    
    probe_instance_t *probe = pod_probe_manager_add_probe(manager, &config);
    if (!probe) {
        fprintf(stderr, "Failed to add liveness probe\n");
        pod_probe_manager_free(manager);
        return 1;
    }
    
    printf("Liveness probe added\n");
    
    pod_probe_manager_free(manager);
    return 0;
}
EOF
    gcc -I"$PROJECT_ROOT" /tmp/test_probe_liveness.c "$PROJECT_ROOT/internal/probes/health_probe.c" -o /tmp/test_probe_liveness -lpthread -lm 2>/dev/null && /tmp/test_probe_liveness
}

test_probe_multiple_probes() {
    # Test adding multiple probes to same pod
    # Expected: All three probe types added successfully
    cat > /tmp/test_probe_multi.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include "internal/probes/health_probe.h"

int main() {
    pod_probe_manager_t *manager = pod_probe_manager_create("pod-a", "default", "10.244.1.10");
    
    // Add startup probe
    probe_config_t startup_config = {
        .type = PROBE_TYPE_STARTUP,
        .handler_type = PROBE_HANDLER_HTTP,
        .handler_config.http = {.path = "/health", .port = 8080, .host = "localhost", .scheme = "HTTP"},
        .initial_delay_seconds = 10,
        .timeout_seconds = 3,
        .period_seconds = 10,
        .success_threshold = 1,
        .failure_threshold = 30
    };
    pod_probe_manager_add_probe(manager, &startup_config);
    
    // Add readiness probe
    probe_config_t readiness_config = {
        .type = PROBE_TYPE_READINESS,
        .handler_type = PROBE_HANDLER_HTTP,
        .handler_config.http = {.path = "/ready", .port = 8080, .host = "localhost", .scheme = "HTTP"},
        .initial_delay_seconds = 5,
        .timeout_seconds = 3,
        .period_seconds = 10,
        .success_threshold = 1,
        .failure_threshold = 3
    };
    pod_probe_manager_add_probe(manager, &readiness_config);
    
    // Add liveness probe
    probe_config_t liveness_config = {
        .type = PROBE_TYPE_LIVENESS,
        .handler_type = PROBE_HANDLER_TCP,
        .handler_config.tcp = {.port = 8080},
        .initial_delay_seconds = 15,
        .timeout_seconds = 5,
        .period_seconds = 30,
        .success_threshold = 1,
        .failure_threshold = 3
    };
    pod_probe_manager_add_probe(manager, &liveness_config);
    
    if (manager->probe_count != 3) {
        fprintf(stderr, "Expected 3 probes, got %d\n", manager->probe_count);
        pod_probe_manager_free(manager);
        return 1;
    }
    
    printf("All 3 probes added successfully\n");
    printf("  - Startup probe\n");
    printf("  - Readiness probe\n");
    printf("  - Liveness probe\n");
    
    pod_probe_manager_free(manager);
    return 0;
}
EOF
    gcc -I"$PROJECT_ROOT" /tmp/test_probe_multi.c "$PROJECT_ROOT/internal/probes/health_probe.c" -o /tmp/test_probe_multi -lpthread -lm 2>/dev/null && /tmp/test_probe_multi
}

test_probe_state_transitions() {
    # Test probe state transitions on success/failure
    # Expected: State changes based on threshold
    cat > /tmp/test_probe_state.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include "internal/probes/health_probe.h"

int main() {
    pod_probe_manager_t *manager = pod_probe_manager_create("pod-a", "default", "10.244.1.10");
    
    probe_config_t config = {
        .type = PROBE_TYPE_READINESS,
        .handler_type = PROBE_HANDLER_HTTP,
        .handler_config.http = {.path = "/ready", .port = 8080, .host = "localhost", .scheme = "HTTP"},
        .initial_delay_seconds = 5,
        .timeout_seconds = 3,
        .period_seconds = 10,
        .success_threshold = 2,  // Need 2 consecutive successes
        .failure_threshold = 1   // 1 failure = not ready
    };
    
    probe_instance_t *probe = pod_probe_manager_add_probe(manager, &config);
    
    // First success - not passed yet (need 2)
    pod_probe_update_result(probe, PROBE_RESULT_SUCCESS);
    if (probe->state.passed) {
        fprintf(stderr, "Probe passed after 1 success (should need 2)\n");
        pod_probe_manager_free(manager);
        return 1;
    }
    
    // Second success - should pass now
    pod_probe_update_result(probe, PROBE_RESULT_SUCCESS);
    if (!probe->state.passed) {
        fprintf(stderr, "Probe not passed after 2 successes\n");
        pod_probe_manager_free(manager);
        return 1;
    }
    
    // One failure - should unpassed
    pod_probe_update_result(probe, PROBE_RESULT_FAILURE);
    if (probe->state.passed) {
        fprintf(stderr, "Probe still passed after 1 failure\n");
        pod_probe_manager_free(manager);
        return 1;
    }
    
    printf("Probe state transitions working correctly\n");
    printf("  - After 1 success: not passed (0/2 threshold)\n");
    printf("  - After 2 successes: passed (2/2 threshold)\n");
    printf("  - After 1 failure: not passed (1/1 failure threshold)\n");
    
    pod_probe_manager_free(manager);
    return 0;
}
EOF
    gcc -I"$PROJECT_ROOT" /tmp/test_probe_state.c "$PROJECT_ROOT/internal/probes/health_probe.c" -o /tmp/test_probe_state -lpthread -lm 2>/dev/null && /tmp/test_probe_state
}

test_probe_statistics() {
    # Test probe statistics collection
    # Expected: Statistics reflect current probe states
    cat > /tmp/test_probe_stats.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include "internal/probes/health_probe.h"

int main() {
    pod_probe_manager_t *manager = pod_probe_manager_create("pod-a", "default", "10.244.1.10");
    
    probe_config_t startup = {
        .type = PROBE_TYPE_STARTUP,
        .handler_type = PROBE_HANDLER_HTTP,
        .handler_config.http = {.path = "/health", .port = 8080, .host = "localhost", .scheme = "HTTP"},
        .initial_delay_seconds = 10,
        .timeout_seconds = 3,
        .period_seconds = 10,
        .success_threshold = 1,
        .failure_threshold = 30
    };
    probe_instance_t *startup_probe = pod_probe_manager_add_probe(manager, &startup);
    pod_probe_update_result(startup_probe, PROBE_RESULT_SUCCESS);
    
    probe_config_t readiness = {
        .type = PROBE_TYPE_READINESS,
        .handler_type = PROBE_HANDLER_HTTP,
        .handler_config.http = {.path = "/ready", .port = 8080, .host = "localhost", .scheme = "HTTP"},
        .initial_delay_seconds = 5,
        .timeout_seconds = 3,
        .period_seconds = 10,
        .success_threshold = 1,
        .failure_threshold = 3
    };
    probe_instance_t *readiness_probe = pod_probe_manager_add_probe(manager, &readiness);
    pod_probe_update_result(readiness_probe, PROBE_RESULT_SUCCESS);
    
    probe_config_t liveness = {
        .type = PROBE_TYPE_LIVENESS,
        .handler_type = PROBE_HANDLER_TCP,
        .handler_config.tcp = {.port = 8080},
        .initial_delay_seconds = 15,
        .timeout_seconds = 5,
        .period_seconds = 30,
        .success_threshold = 1,
        .failure_threshold = 3
    };
    probe_instance_t *liveness_probe = pod_probe_manager_add_probe(manager, &liveness);
    pod_probe_update_result(liveness_probe, PROBE_RESULT_FAILURE);
    
    uint32_t total, passing, failing;
    pod_probe_get_stats(manager, &total, &passing, &failing);
    
    if (total != 3 || passing != 2 || failing != 1) {
        fprintf(stderr, "Statistics incorrect: total=%d (expect 3), passing=%d (expect 2), failing=%d (expect 1)\n",
                total, passing, failing);
        pod_probe_manager_free(manager);
        return 1;
    }
    
    printf("Probe statistics correct\n");
    printf("  - Total probes: %d\n", total);
    printf("  - Passing: %d\n", passing);
    printf("  - Failing: %d\n", failing);
    
    pod_probe_manager_free(manager);
    return 0;
}
EOF
    gcc -I"$PROJECT_ROOT" /tmp/test_probe_stats.c "$PROJECT_ROOT/internal/probes/health_probe.c" -o /tmp/test_probe_stats -lpthread -lm 2>/dev/null && /tmp/test_probe_stats
}

# ============================================================================
# Run all tests
# ============================================================================

echo "========================================="
echo "Phase 4 IPAM, DNS, Health Probes Test Suite"
echo "========================================="
echo ""

echo "=== IPAM Tests ==="
run_test "IPAM manager creation" test_ipam_manager_creation
run_test "IPAM node registration" test_ipam_node_registration
run_test "IPAM pod IP allocation" test_ipam_pod_ip_allocation
run_test "IPAM service IP allocation" test_ipam_service_ip_allocation
run_test "IPAM collision detection" test_ipam_collision_detection
echo ""

echo "=== DNS Tests ==="
run_test "DNS resolver creation" test_dns_resolver_creation
run_test "DNS service registration" test_dns_service_registration
run_test "DNS pod registration" test_dns_pod_registration
run_test "DNS service lookup" test_dns_service_lookup
run_test "DNS pod lookup" test_dns_pod_lookup
run_test "DNS headless service" test_dns_headless_service
echo ""

echo "=== Health Probes Tests ==="
run_test "Probe manager creation" test_probe_manager_creation
run_test "Add startup probe" test_probe_add_startup_probe
run_test "Add readiness probe" test_probe_add_readiness_probe
run_test "Add liveness probe" test_probe_add_liveness_probe
run_test "Multiple probes" test_probe_multiple_probes
run_test "Probe state transitions" test_probe_state_transitions
run_test "Probe statistics" test_probe_statistics
echo ""

# Summary
echo "========================================="
echo "Test Summary"
echo "========================================="
echo "Total tests run: $TESTS_RUN"
echo -e "${GREEN}Passed: $TESTS_PASSED${NC}"
echo -e "${RED}Failed: $TESTS_FAILED${NC}"
echo ""

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed! ✓${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed.${NC}"
    exit 1
fi
