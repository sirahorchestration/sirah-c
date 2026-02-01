# Makefile for Kubernetes-Compatible Unikernel Platform

.PHONY: all build-components build-unikernels test clean install deps lint fmt vet conformance

# Variables
GO := go
KRAFT := kraft
KUBECTL := kubectl

# Directories
COMPONENTS_DIR := components
BIN_DIR := bin
BUILD_DIR := build
DOCS_DIR := docs
TESTS_DIR := tests

# Components
COMPONENTS := api-server scheduler controller-manager kubelet container-runtime kube-proxy etcd coredns

# Build targets
all: deps build-components build-unikernels

deps:
	@echo "Installing dependencies..."
	$(GO) mod download
	$(GO) mod verify

build-components:
	@echo "Building all components..."
	@for component in $(COMPONENTS); do \
		echo "Building $$component..."; \
		cd $(COMPONENTS_DIR)/$$component && $(GO) build -o ../../$(BIN_DIR)/$$component ./cmd/main.go || exit 1; \
		cd ../..; \
	done

build-api-server:
	@echo "Building API Server..."
	cd $(COMPONENTS_DIR)/api-server && $(GO) build -o ../../$(BIN_DIR)/api-server ./cmd/main.go

build-scheduler:
	@echo "Building Scheduler..."
	cd $(COMPONENTS_DIR)/scheduler && $(GO) build -o ../../$(BIN_DIR)/scheduler ./cmd/main.go

build-controller:
	@echo "Building Controller Manager..."
	cd $(COMPONENTS_DIR)/controller-manager && $(GO) build -o ../../$(BIN_DIR)/controller-manager ./cmd/main.go

build-kubelet:
	@echo "Building kubelet..."
	cd $(COMPONENTS_DIR)/kubelet && $(GO) build -o ../../$(BIN_DIR)/kubelet ./cmd/main.go

build-runtime:
	@echo "Building Container Runtime..."
	cd $(COMPONENTS_DIR)/container-runtime && $(GO) build -o ../../$(BIN_DIR)/container-runtime ./cmd/main.go

build-proxy:
	@echo "Building kube-proxy..."
	cd $(COMPONENTS_DIR)/kube-proxy && $(GO) build -o ../../$(BIN_DIR)/kube-proxy ./cmd/main.go

build-unikernels:
	@echo "Building unikernels..."
	@for component in $(COMPONENTS); do \
		echo "Building $$component unikernel..."; \
		./tools/unikernel-build/build_component.sh $$component || exit 1; \
	done

test:
	@echo "Running tests..."
	$(GO) test -v -race -coverprofile=coverage.out ./...

test-unit:
	@echo "Running unit tests..."
	$(GO) test -v -race -short ./...

test-integration:
	@echo "Running integration tests..."
	$(GO) test -v -race -run Integration ./tests/integration/...

test-e2e:
	@echo "Running E2E tests..."
	$(GO) test -v -timeout 30m ./tests/e2e/...

conformance:
	@echo "Running Kubernetes conformance tests..."
	./hack/run-conformance.sh

lint:
	@echo "Running linters..."
	golangci-lint run ./...

fmt:
	@echo "Formatting code..."
	$(GO) fmt ./...
	gofmt -s -w .

vet:
	@echo "Running go vet..."
	$(GO) vet ./...

clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BIN_DIR)/*
	rm -rf $(BUILD_DIR)/*
	rm -f coverage.out

install:
	@echo "Installing to cluster..."
	./tools/cluster-bootstrap/deploy.sh

bootstrap-cluster:
	@echo "Bootstrapping new cluster..."
	./tools/cluster-bootstrap/create_cluster.sh

verify:
	@echo "Verifying cluster..."
	$(KUBECTL) cluster-info
	$(KUBECTL) get nodes
	$(KUBECTL) get pods --all-namespaces

help:
	@echo "Kubernetes-Compatible Unikernel Platform - Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all                - Build everything (default)"
	@echo "  deps               - Install dependencies"
	@echo "  build-components   - Build all Go components"
	@echo "  build-unikernels   - Build unikernel images"
	@echo "  test               - Run all tests"
	@echo "  test-unit          - Run unit tests only"
	@echo "  test-integration   - Run integration tests"
	@echo "  test-e2e           - Run end-to-end tests"
	@echo "  conformance        - Run Kubernetes conformance tests"
	@echo "  lint               - Run linters"
	@echo "  fmt                - Format code"
	@echo "  vet                - Run go vet"
	@echo "  clean              - Remove build artifacts"
	@echo "  install            - Deploy to cluster"
	@echo "  bootstrap-cluster  - Create new cluster"
	@echo "  verify             - Verify cluster health"
	@echo "  help               - Show this help"
