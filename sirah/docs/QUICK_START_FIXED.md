# Quick Start - API Server & Monitoring

## Start the API Server
```bash
cd /mnt/c/projects/k8s_unikernels/sirah
./bin/sirah-apiserver
```

## Create a Pod
```bash
curl -X POST \
  -H 'Content-Type: application/json' \
  -u admin:admin \
  -d '{
    "apiVersion":"v1",
    "kind":"Pod",
    "metadata":{"name":"my-app","namespace":"default"},
    "spec":{"containers":[{"name":"app","image":"nginx"}]}
  }' \
  http://localhost:6443/api/v1/namespaces/default/pods
```

## List Pods
```bash
curl http://localhost:6443/api/v1/namespaces/default/pods | python3 -m json.tool
```

## Monitor QEMU Pods
```bash
bash scripts/view-qemu-pods.sh
# or with namespace
bash scripts/view-qemu-pods.sh default
```

## Check Cluster Health
```bash
curl http://localhost:6443/healthz
```

## Run All Tests
```bash
bash comprehensive-test.sh
```

---

## What Was Fixed

✅ **Pod Creation Crashes** - Memory allocation for container arrays  
✅ **API Server Stability** - Null pointer safety checks in pod listing  
✅ **Monitoring Script** - Fixed JSON parsing in bash heredocs  

All API and monitoring functionality now works reliably!
