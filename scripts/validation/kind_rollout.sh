#!/usr/bin/env bash
# Validate Kubernetes rollout and rollback behavior in kind.
set -euo pipefail

python_image=${1:-tt-metrics-exporter:python-local}
cluster=${KIND_CLUSTER_NAME:-tt-telemetry-rollout}
node_image=kindest/node:v1.34.0@sha256:7416a61b42b1662ca6ca89f02028ac133a309a2a30ba309614e8ec94d976dc5a
work=$(mktemp -d)

cleanup() {
  rm -rf "${work}"
  if test "${KEEP_KIND_CLUSTER:-0}" != 1; then
    kind delete cluster --name "${cluster}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

kind delete cluster --name "${cluster}" >/dev/null 2>&1 || true
kind create cluster --name "${cluster}" --image "${node_image}" --wait 120s
node="${cluster}-control-plane"
docker exec "${node}" mkdir -p /tmp/tt-sysfs/0 /tmp/tt-sys-devices
kubectl label node "${node}" tenstorrent.com/ttsim=true --overwrite
kind load docker-image --name "${cluster}" "${python_image}"

kubectl kustomize deploy/kubernetes/overlays/ttsim >"${work}/python.yaml"
sed -i \
  -e "s#image: tt-metrics-exporter:ttsim#image: ${python_image}#" \
  -e 's#/sys/class/tenstorrent#/tmp/tt-sysfs#' \
  -e 's#/sys/devices#/tmp/tt-sys-devices#' \
  "${work}/python.yaml"
kubectl apply -f "${work}/python.yaml"
kubectl rollout status daemonset/tt-metrics-exporter --timeout=120s
test "$(kubectl get daemonset tt-metrics-exporter -o jsonpath='{.spec.template.metadata.labels.telemetry\.tenstorrent\.com/implementation}')" = python
test "$(kubectl get pod -l app.kubernetes.io/name=tt-metrics-exporter -o jsonpath='{.items[0].status.containerStatuses[0].ready}')" = true

kubectl patch daemonset tt-metrics-exporter --type merge --patch \
  '{"spec":{"template":{"metadata":{"labels":{"telemetry.tenstorrent.com/validation-generation":"two"}}}}}'
kubectl rollout status daemonset/tt-metrics-exporter --timeout=120s
test "$(kubectl get daemonset tt-metrics-exporter -o jsonpath='{.spec.template.metadata.labels.telemetry\.tenstorrent\.com/validation-generation}')" = two

kubectl rollout undo daemonset/tt-metrics-exporter --to-revision=1
kubectl rollout status daemonset/tt-metrics-exporter --timeout=120s
test "$(kubectl get daemonset tt-metrics-exporter -o jsonpath='{.spec.template.metadata.labels.telemetry\.tenstorrent\.com/implementation}')" = python
test -z "$(kubectl get daemonset tt-metrics-exporter -o jsonpath='{.spec.template.metadata.labels.telemetry\.tenstorrent\.com/validation-generation}')"
test "$(kubectl get pod -l app.kubernetes.io/name=tt-metrics-exporter -o jsonpath='{.items[0].status.containerStatuses[0].ready}')" = true
