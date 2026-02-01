# Kubernetes API Compatibility Matrix

## Overview

This document tracks the Kubernetes API compatibility of the Unikernel Orchestration Platform. The goal is 100% compatibility with Kubernetes 1.28+ stable APIs, validated through conformance testing.

## Compatibility Principle

**Any existing Kubernetes tool (kubectl, Helm, operators, controllers) works unmodified with this platform.**

## API Version Support

| API Group | Version | Status | Notes |
|-----------|---------|--------|-------|
| Core (v1) | v1 | ✅ Planned | Pods, Services, Nodes, etc. |
| Apps | v1 | ✅ Planned | Deployments, StatefulSets, etc. |
| Batch | v1 | ✅ Planned | Jobs, CronJobs |
| Networking | v1 | ✅ Planned | NetworkPolicies, Ingress |
| Storage | v1 | ✅ Planned | StorageClasses, PVs, PVCs |
| RBAC | v1 | ✅ Planned | Roles, RoleBindings, etc. |
| Policy | v1 | ✅ Planned | PodDisruptionBudgets |
| Autoscaling | v2 | ⏸️ Future | HorizontalPodAutoscaler |
| Certificates | v1 | ⏸️ Future | CertificateSigningRequests |
| Discovery | v1 | ⏸️ Future | EndpointSlices |

Legend:
- ✅ Planned: In scope for v1.0
- ⏸️ Future: Post-v1.0
- ❌ Not Supported: Out of scope
- ✔️ Implemented: Complete and tested

## Core API (v1) Coverage

### Pods
| Field/Feature | Status | Notes |
|---------------|--------|-------|
| spec.containers | ✅ Planned | Full container spec support |
| spec.initContainers | ✅ Planned | Init containers |
| spec.volumes | ✅ Planned | ConfigMap, Secret, PVC, EmptyDir |
| spec.nodeSelector | ✅ Planned | Node selection |
| spec.nodeName | ✅ Planned | Direct node assignment |
| spec.affinity | ✅ Planned | Node/pod affinity |
| spec.tolerations | ✅ Planned | Taint tolerations |
| spec.restartPolicy | ✅ Planned | Always, OnFailure, Never |
| spec.serviceAccountName | ✅ Planned | Pod identity |
| spec.hostNetwork | ⏸️ Future | Host network mode |
| spec.hostPID | ❌ Not Supported | N/A for unikernels |
| spec.hostIPC | ❌ Not Supported | N/A for unikernels |
| spec.securityContext | ✅ Planned | Pod-level security |
| status.phase | ✅ Planned | Pending, Running, Succeeded, Failed |
| status.conditions | ✅ Planned | PodScheduled, Ready, etc. |
| status.containerStatuses | ✅ Planned | Container status reporting |
| livenessProbe | ✅ Planned | HTTP, TCP, Exec probes |
| readinessProbe | ✅ Planned | HTTP, TCP, Exec probes |
| startupProbe | ✅ Planned | Startup health checks |
| resources.requests | ✅ Planned | CPU, memory requests |
| resources.limits | ✅ Planned | CPU, memory limits |

### Services
| Field/Feature | Status | Notes |
|---------------|--------|-------|
| spec.type: ClusterIP | ✅ Planned | Internal service IPs |
| spec.type: NodePort | ✅ Planned | External access via nodes |
| spec.type: LoadBalancer | ⏸️ Future | Cloud provider integration |
| spec.type: ExternalName | ⏸️ Future | DNS CNAME records |
| spec.selector | ✅ Planned | Pod selection |
| spec.ports | ✅ Planned | Port mappings |
| spec.sessionAffinity | ✅ Planned | ClientIP affinity |
| status.loadBalancer | ⏸️ Future | LoadBalancer status |

### ConfigMaps & Secrets
| Field/Feature | Status | Notes |
|---------------|--------|-------|
| data (ConfigMap) | ✅ Planned | Key-value configuration |
| data (Secret) | ✅ Planned | Base64-encoded secrets |
| stringData (Secret) | ✅ Planned | Plain text input |
| type (Secret) | ✅ Planned | Opaque, TLS, etc. |
| Volume mounting | ✅ Planned | Mount as files |
| Environment variables | ✅ Planned | Inject as env vars |

### Namespaces
| Field/Feature | Status | Notes |
|---------------|--------|-------|
| metadata.name | ✅ Planned | Namespace creation |
| status.phase | ✅ Planned | Active, Terminating |
| Namespace scoping | ✅ Planned | Resource isolation |
| Default namespace | ✅ Planned | Auto-created |

### Nodes
| Field/Feature | Status | Notes |
|---------------|--------|-------|
| metadata.labels | ✅ Planned | Node labels |
| spec.taints | ✅ Planned | Node taints |
| spec.unschedulable | ✅ Planned | Cordon nodes |
| status.conditions | ✅ Planned | Ready, DiskPressure, etc. |
| status.capacity | ✅ Planned | Node resources |
| status.allocatable | ✅ Planned | Available resources |
| status.nodeInfo | ✅ Planned | OS, kernel, runtime info |

### PersistentVolumes & PersistentVolumeClaims
| Field/Feature | Status | Notes |
|---------------|--------|-------|
| PV: spec.capacity | ✅ Planned | Storage size |
| PV: spec.accessModes | ✅ Planned | RWO, ROX, RWX |
| PV: spec.persistentVolumeReclaimPolicy | ✅ Planned | Retain, Delete |
| PV: spec.storageClassName | ✅ Planned | Storage class binding |
| PV: spec.local | ✅ Planned | Local path volumes (v1.0) |
| PV: spec.nfs | ⏸️ Future | NFS volumes |
| PVC: spec.resources.requests | ✅ Planned | Storage request |
| PVC: spec.volumeName | ✅ Planned | Bind to specific PV |
| PVC: status.phase | ✅ Planned | Pending, Bound, Lost |

### Events
| Field/Feature | Status | Notes |
|---------------|--------|-------|
| Event creation | ✅ Planned | Component events |
| Event listing | ✅ Planned | kubectl get events |
| Event filtering | ✅ Planned | By namespace, resource |

## Apps API (apps/v1)

### Deployments
| Field/Feature | Status | Notes |
|---------------|--------|-------|
| spec.replicas | ✅ Planned | Desired replica count |
| spec.selector | ✅ Planned | Pod selector |
| spec.template | ✅ Planned | Pod template |
| spec.strategy.type | ✅ Planned | RollingUpdate, Recreate |
| spec.strategy.rollingUpdate | ✅ Planned | MaxSurge, MaxUnavailable |
| spec.revisionHistoryLimit | ✅ Planned | Rollback history |
| status.replicas | ✅ Planned | Current replicas |
| status.updatedReplicas | ✅ Planned | Updated replicas |
| status.availableReplicas | ✅ Planned | Available replicas |
| Rollout/rollback | ✅ Planned | kubectl rollout commands |

### ReplicaSets
| Field/Feature | Status | Notes |
|---------------|--------|-------|
| spec.replicas | ✅ Planned | Desired replica count |
| spec.selector | ✅ Planned | Pod selector |
| spec.template | ✅ Planned | Pod template |
| status.replicas | ✅ Planned | Current replicas |
| Reconciliation | ✅ Planned | Maintain desired count |

### StatefulSets
| Field/Feature | Status | Notes |
|---------------|--------|-------|
| spec.serviceName | ✅ Planned | Headless service |
| spec.replicas | ✅ Planned | Replica count |
| spec.selector | ✅ Planned | Pod selector |
| spec.template | ✅ Planned | Pod template |
| spec.volumeClaimTemplates | ✅ Planned | PVC templates |
| spec.podManagementPolicy | ✅ Planned | OrderedReady, Parallel |
| spec.updateStrategy | ✅ Planned | RollingUpdate, OnDelete |
| Ordered pod creation | ✅ Planned | Sequential startup |
| Stable network identity | ✅ Planned | pod-0, pod-1, etc. |

### DaemonSets
| Field/Feature | Status | Notes |
|---------------|--------|-------|
| spec.selector | ✅ Planned | Pod selector |
| spec.template | ✅ Planned | Pod template |
| spec.updateStrategy | ✅ Planned | RollingUpdate, OnDelete |
| Node coverage | ✅ Planned | One pod per node |
| Node selectors | ✅ Planned | Target specific nodes |

## Batch API (batch/v1)

### Jobs
| Field/Feature | Status | Notes |
|---------------|--------|-------|
| spec.template | ✅ Planned | Pod template |
| spec.completions | ✅ Planned | Successful completions |
| spec.parallelism | ✅ Planned | Parallel executions |
| spec.backoffLimit | ✅ Planned | Retry limit |
| spec.ttlSecondsAfterFinished | ✅ Planned | Auto-cleanup |
| status.succeeded | ✅ Planned | Success count |
| status.failed | ✅ Planned | Failure count |

### CronJobs
| Field/Feature | Status | Notes |
|---------------|--------|-------|
| spec.schedule | ✅ Planned | Cron schedule |
| spec.jobTemplate | ✅ Planned | Job template |
| spec.successfulJobsHistoryLimit | ✅ Planned | History limit |
| spec.failedJobsHistoryLimit | ✅ Planned | History limit |
| spec.concurrencyPolicy | ✅ Planned | Allow, Forbid, Replace |
| Scheduled execution | ✅ Planned | Time-based triggers |

## RBAC API (rbac.authorization.k8s.io/v1)

### Roles & ClusterRoles
| Field/Feature | Status | Notes |
|---------------|--------|-------|
| rules.apiGroups | ✅ Planned | API group matching |
| rules.resources | ✅ Planned | Resource types |
| rules.verbs | ✅ Planned | get, list, create, etc. |
| rules.resourceNames | ✅ Planned | Specific resource names |
| Namespace-scoped (Role) | ✅ Planned | Namespace permissions |
| Cluster-scoped (ClusterRole) | ✅ Planned | Cluster permissions |

### RoleBindings & ClusterRoleBindings
| Field/Feature | Status | Notes |
|---------------|--------|-------|
| subjects | ✅ Planned | Users, groups, ServiceAccounts |
| roleRef | ✅ Planned | Role reference |
| Authorization checks | ✅ Planned | Permission enforcement |

### ServiceAccounts
| Field/Feature | Status | Notes |
|---------------|--------|-------|
| secrets | ✅ Planned | Token secrets |
| imagePullSecrets | ✅ Planned | Image registry auth |
| Token mounting | ✅ Planned | Auto-mount in pods |

## Storage API (storage.k8s.io/v1)

### StorageClasses
| Field/Feature | Status | Notes |
|---------------|--------|-------|
| provisioner | ✅ Planned | Local path (v1.0) |
| parameters | ✅ Planned | Provisioner config |
| volumeBindingMode | ✅ Planned | Immediate, WaitForFirstConsumer |
| reclaimPolicy | ✅ Planned | Delete, Retain |
| Dynamic provisioning | ✅ Planned | Auto-create PVs |

## Policy API (policy/v1)

### PodDisruptionBudgets
| Field/Feature | Status | Notes |
|---------------|--------|-------|
| spec.minAvailable | ✅ Planned | Minimum pods |
| spec.maxUnavailable | ✅ Planned | Maximum disruption |
| spec.selector | ✅ Planned | Pod selector |
| Eviction protection | ✅ Planned | During drains |

## Networking API (networking.k8s.io/v1)

### NetworkPolicies
| Field/Feature | Status | Notes |
|---------------|--------|-------|
| spec.podSelector | ⏸️ Future | Target pods |
| spec.policyTypes | ⏸️ Future | Ingress, Egress |
| spec.ingress | ⏸️ Future | Ingress rules |
| spec.egress | ⏸️ Future | Egress rules |
| Enforcement | ⏸️ Future | Requires CNI support |

### Ingress
| Field/Feature | Status | Notes |
|---------------|--------|-------|
| spec.rules | ⏸️ Future | Routing rules |
| spec.tls | ⏸️ Future | TLS configuration |
| Controller | ⏸️ Future | Ingress controller |

## kubectl Compatibility

| Command | Status | Notes |
|---------|--------|-------|
| kubectl get | ✅ Planned | All resources |
| kubectl describe | ✅ Planned | Detailed info |
| kubectl create | ✅ Planned | Imperative creation |
| kubectl apply | ✅ Planned | Declarative management |
| kubectl delete | ✅ Planned | Resource deletion |
| kubectl edit | ✅ Planned | In-place editing |
| kubectl scale | ✅ Planned | Scaling workloads |
| kubectl rollout | ✅ Planned | Deployment management |
| kubectl logs | ✅ Planned | Container logs |
| kubectl exec | ✅ Planned | Command execution |
| kubectl port-forward | ✅ Planned | Port forwarding |
| kubectl proxy | ✅ Planned | API proxy |
| kubectl top | ⏸️ Future | Metrics (requires metrics-server) |
| kubectl drain | ✅ Planned | Node draining |
| kubectl cordon/uncordon | ✅ Planned | Node scheduling control |

## Helm Compatibility

| Feature | Status | Notes |
|---------|--------|-------|
| Helm install | ✅ Planned | Chart installation |
| Helm upgrade | ✅ Planned | Chart upgrades |
| Helm rollback | ✅ Planned | Rollback releases |
| Helm uninstall | ✅ Planned | Release removal |
| Helm list | ✅ Planned | List releases |
| Custom resources | ⏸️ Future | CRD support |
| Hooks | ✅ Planned | Pre/post hooks |
| Templates | ✅ Planned | Go templates |

## Conformance Testing

### Test Categories

| Category | Tests | Target Pass Rate |
|----------|-------|------------------|
| Core APIs | ~150 | >90% |
| Networking | ~40 | >85% |
| Storage | ~30 | >85% |
| RBAC | ~20 | >90% |
| Apps | ~60 | >90% |
| Overall | ~300 | >85% |

### Testing Tool
- **Sonobuoy**: Official Kubernetes conformance testing tool
- **Test Suite**: Kubernetes e2e tests (conformance subset)
- **Validation**: CNCF Certified Kubernetes badge (goal)

## Known Limitations (v1.0)

| Feature | Limitation | Workaround |
|---------|------------|------------|
| Host namespaces | Not supported | N/A for unikernels |
| privilegedContainers | Not supported | Security by design |
| GPU scheduling | Not supported | Post-v1.0 feature |
| Windows containers | Not supported | Linux unikernels only |
| Service mesh | Not supported | Post-v1.0 integration |
| Custom schedulers | Limited support | Default scheduler only |

## API Extensions (Future)

| Extension | Version | Target Release |
|-----------|---------|----------------|
| Custom Resources (CRD) | v1 | v2.0 |
| Metrics Server | v1 | v1.5 |
| Horizontal Pod Autoscaler | v2 | v2.0 |
| Vertical Pod Autoscaler | v1 | v2.0 |
| Cluster Autoscaler | N/A | v2.0 |
| Admission Webhooks (advanced) | v1 | v1.5 |

## Verification Process

1. **Unit Tests**: Component-level API compliance
2. **Integration Tests**: Multi-component API scenarios
3. **E2E Tests**: Full cluster API workflows
4. **Conformance Tests**: Official Kubernetes test suite
5. **Manual Testing**: kubectl/Helm validation
6. **Compatibility Matrix**: Continuous tracking

## Compatibility Commitment

We commit to:
- ✅ 100% stable API compatibility (v1 resources)
- ✅ Pass >85% of Kubernetes conformance tests
- ✅ Support kubectl, Helm, and standard operators
- ✅ Maintain API compatibility across platform releases
- ✅ Document any deviations clearly

## References

- [Kubernetes API Conventions](https://github.com/kubernetes/community/blob/master/contributors/devel/sig-architecture/api-conventions.md)
- [Kubernetes API Reference v1.28](https://kubernetes.io/docs/reference/kubernetes-api/)
- [Kubernetes Conformance](https://github.com/cncf/k8s-conformance)
- [CNCF Certification Program](https://www.cncf.io/certification/software-conformance/)
