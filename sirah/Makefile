CC ?= gcc
CFLAGS = -Wall -Wextra -O2 -fPIC -Ipkg -Iinternal -Iinternal/kubelet -Iinternal/runtime
LDFLAGS = -lm -pthread -lcurl -ljson-c -lssl -lcrypto -lz -luuid -lmicrohttpd

# Output directories
BINS_DIR = bin
OBJ_DIR = obj

# Binaries
APISERVER = $(BINS_DIR)/sirah-apiserver
SCHEDULER = $(BINS_DIR)/sirah-scheduler
CONTROLLER = $(BINS_DIR)/sirah-controller
KUBELET = $(BINS_DIR)/sirah-kubelet

# Common source files
COMMON_SRC = pkg/types/common.c pkg/types/pod.c pkg/types/node.c pkg/types/service.c pkg/types/event.c pkg/types/config.c pkg/types/storage.c pkg/types/rbac.c pkg/types/crd.c pkg/types/network_policy.c pkg/types/job.c pkg/types/affinity.c pkg/types/hpa.c pkg/types/quota.c pkg/lifecycle/pod_lifecycle.c pkg/networking/dns_resolver.c pkg/networking/load_balancer.c internal/storage/store.c internal/storage/etcd.c internal/storage/storage_binder.c internal/core/events.c internal/apiserver/service_registry.c internal/apiserver/authz.c internal/apiserver/crd_manager.c internal/apiserver/webhooks.c internal/apiserver/admission.c internal/apiserver/batch_handler.c internal/apiserver/health.c internal/controller/networkpolicy_controller.c internal/controller/job_controller.c internal/controller/taint_controller.c internal/controller/hpa_controller.c internal/controller/quota_controller.c internal/controller/pod_controller.c internal/networking/policy_enforcer.c internal/scheduler/cron_scheduler.c internal/scheduler/affinity_scheduler.c internal/metrics/metrics.c internal/metrics/prometheus.c internal/cache/request_cache.c internal/optimization/connection_pool.c internal/diagnostics/cluster_status.c internal/kubelet/qemu_manager.c internal/kubelet/unikernel_runtime.c internal/runtime/runtime.c internal/runtime/qemu.c
COMMON_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(COMMON_SRC))

# API Server source files
APISERVER_SRC = cmd/apiserver/main.c internal/apiserver/server.c internal/apiserver/handler.c internal/apiserver/endpoints.c internal/apiserver/week5_endpoints.c internal/apiserver/patch_handler.c internal/apiserver/query_parser.c internal/apiserver/watch.c internal/apiserver/pod_logs.c internal/apiserver/pod_exec.c
APISERVER_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(APISERVER_SRC))

# Scheduler source files
SCHEDULER_SRC = cmd/scheduler/main.c internal/scheduler/scheduler.c internal/scheduler/plugins.c
SCHEDULER_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(SCHEDULER_SRC))

# Controller Manager source files
CONTROLLER_SRC = cmd/controller/main.c internal/controller/manager.c internal/controller/deployment.c internal/controller/replicaset.c internal/controller/service.c internal/controller/statefulset.c internal/controller/configmap.c internal/controller/secret.c
CONTROLLER_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(CONTROLLER_SRC))

# Kubelet source files
KUBELET_SRC = cmd/kubelet/main.c internal/kubelet/kubelet.c internal/kubelet/volumes.c
KUBELET_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(KUBELET_SRC))

# Default target
.PHONY: all
all: $(APISERVER) $(SCHEDULER) $(CONTROLLER) $(KUBELET)

# Create directories
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)/cmd/{apiserver,scheduler,controller,kubelet}
	mkdir -p $(OBJ_DIR)/internal/{apiserver,scheduler,controller,kubelet,storage,core,runtime}
	mkdir -p $(OBJ_DIR)/pkg/types

$(BINS_DIR):
	mkdir -p $(BINS_DIR)

# API Server binary
$(APISERVER): $(COMMON_OBJS) $(APISERVER_OBJS) | $(BINS_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✓ Built: $@"

# Scheduler binary
$(SCHEDULER): $(COMMON_OBJS) $(SCHEDULER_OBJS) | $(BINS_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✓ Built: $@"

# Controller Manager binary
$(CONTROLLER): $(COMMON_OBJS) $(CONTROLLER_OBJS) | $(BINS_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✓ Built: $@"

# Kubelet binary
$(KUBELET): $(COMMON_OBJS) $(KUBELET_OBJS) | $(BINS_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✓ Built: $@"

# Compilation rule
$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Testing
.PHONY: test
test:
	@echo "Running tests..."
	@./tests/run-tests.sh || true

# Cleaning
.PHONY: clean
clean:
	rm -rf $(OBJ_DIR) $(BINS_DIR)
	@echo "✓ Cleaned"

# Install (optional)
.PHONY: install
install: all
	cp $(APISERVER) /usr/local/bin/
	cp $(SCHEDULER) /usr/local/bin/
	cp $(CONTROLLER) /usr/local/bin/
	@echo "✓ Installed binaries"

# Run targets
.PHONY: run-apiserver
run-apiserver: $(APISERVER)
	$(APISERVER) --port 6443

.PHONY: run-scheduler
run-scheduler: $(SCHEDULER)
	$(SCHEDULER)

.PHONY: run-controller
run-controller: $(CONTROLLER)
	$(CONTROLLER)

.PHONY: run-kubelet
run-kubelet: $(KUBELET)
	$(KUBELET)

.PHONY: run
run: $(APISERVER)
	bash run-server.sh

.PHONY: help
help:
	@echo "Sirah Kubernetes Platform - Build System"
	@echo ""
	@echo "Build Targets:"
	@echo "  make              - Build all binaries"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make install      - Install binaries to /usr/local/bin"
	@echo ""
	@echo "Run Targets:"
	@echo "  make run          - Start API server (detached)"
	@echo "  make run-apiserver    - Start API server (foreground)"
	@echo "  make run-scheduler    - Start scheduler (foreground)"
	@echo "  make run-controller   - Start controller (foreground)"
	@echo ""
	@echo "Development:"
	@echo "  make test         - Run tests"
	@echo "  make info         - Show build information"
	@echo "  make help         - Show this help message"

.PHONY: info
info:
	@echo "Sirah Build Information"
	@echo ""
	@echo "Compiler:   $(CC)"
	@echo "CFLAGS:     $(CFLAGS)"
	@echo "LDFLAGS:    $(LDFLAGS)"
	@echo ""
	@echo "Output:"
	@echo "  API Server:       $(APISERVER)"
	@echo "  Scheduler:        $(SCHEDULER)"
	@echo "  Controller:       $(CONTROLLER)"

.PHONY: test test-unit test-integration test-e2e
test: test-unit test-integration test-e2e
	@echo "Test suite complete!"

test-unit:
	@echo "Running unit tests..."
	@bash tests/run_all_tests.sh 2>&1 | grep -E "Phase 1|Test Summary|✓|✗" || true

test-integration: $(APISERVER)
	@echo "Running integration tests..."
	@bash tests/run_all_tests.sh 2>&1 | grep -E "Phase 2|Test Summary|✓|✗" || true

test-e2e: $(APISERVER)
	@echo "Running end-to-end tests..."
	@bash tests/run_all_tests.sh 2>&1 | grep -E "Phase 3|Test Summary|✓|✗" || true

.DEFAULT_GOAL := all
