#!/usr/bin/env bash
# Prove exporter ingress policy with a NetworkPolicy-capable Kind CNI.
set -euo pipefail

image=${1:-tt-metrics-exporter:python-local}
cluster=${KIND_CLUSTER_NAME:-tt-telemetry-policy}
node_image=kindest/node:v1.34.0@sha256:7416a61b42b1662ca6ca89f02028ac133a309a2a30ba309614e8ec94d976dc5a
calico_url=https://raw.githubusercontent.com/projectcalico/calico/0ca9d1b93644778cafdf1812f3dda02ac0c361e8/manifests/calico.yaml
calico_sha256=a1df919d9721cf667accdc3e72848911b0cb25cfab7d2478ad0c996302c95744
timeout=${KIND_POLICY_TIMEOUT:-15m}
work=$(mktemp -d)
cleanup() {
  rm -rf "${work}"
  if test "${KEEP_KIND_CLUSTER:-0}" != 1; then
    kind delete cluster --name "${cluster}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

kind delete cluster --name "${cluster}" >/dev/null 2>&1 || true
kind create cluster --name "${cluster}" --image "${node_image}" --config scripts/validation/kind-calico.yaml
node="${cluster}-control-plane"
# Keep TCG scheduling delays from recycling the Kubernetes control plane while
# Calico consumes all emulated CPUs during its first initialization.
docker exec "${node}" sed -i -e '/- --leader-elect=true/a\    - --leader-elect-lease-duration=120s\n    - --leader-elect-renew-deadline=90s\n    - --leader-elect-retry-period=20s' -e 's/failureThreshold: 8$/failureThreshold: 10/' -e 's/initialDelaySeconds: 10$/initialDelaySeconds: 180/' -e 's/periodSeconds: 10$/periodSeconds: 30/' -e 's/periodSeconds: 1$/periodSeconds: 30/' -e 's/timeoutSeconds: 15$/timeoutSeconds: 30/' /etc/kubernetes/manifests/kube-controller-manager.yaml /etc/kubernetes/manifests/kube-scheduler.yaml
docker exec "${node}" grep --quiet -- '--leader-elect-renew-deadline=90s' /etc/kubernetes/manifests/kube-controller-manager.yaml
docker exec "${node}" grep --quiet -- '--leader-elect-renew-deadline=90s' /etc/kubernetes/manifests/kube-scheduler.yaml
curl --fail --silent --show-error --location "${calico_url}" --output "${work}/calico.yaml"
echo "${calico_sha256}  ${work}/calico.yaml" | sha256sum --check
kubectl apply -f "${work}/calico.yaml"
# TCG can need minutes to initialize Felix/BIRD. Preserve the steady-state probes,
# but prevent kubelet from restarting Calico while that one-time work completes.
kubectl patch daemonset calico-node --namespace kube-system --type=strategic --patch '{"spec":{"template":{"spec":{"containers":[{"name":"calico-node","livenessProbe":{"initialDelaySeconds":180,"periodSeconds":30,"timeoutSeconds":30,"failureThreshold":10},"readinessProbe":{"periodSeconds":30,"timeoutSeconds":30,"failureThreshold":10}}]}}}}'
kubectl patch deployment calico-kube-controllers --namespace kube-system --type=strategic --patch '{"spec":{"progressDeadlineSeconds":1800,"template":{"spec":{"containers":[{"name":"calico-kube-controllers","livenessProbe":{"initialDelaySeconds":180,"periodSeconds":30,"timeoutSeconds":30,"failureThreshold":10},"readinessProbe":{"periodSeconds":30,"timeoutSeconds":30,"failureThreshold":10}}]}}}}'
kubectl rollout status daemonset/calico-node --namespace kube-system --timeout="${timeout}"
kubectl rollout status deployment/calico-kube-controllers --namespace kube-system --timeout="${timeout}"
kubectl wait nodes --all --for=condition=Ready --timeout="${timeout}"

docker exec "${node}" mkdir -p /tmp/tt-sysfs/0 /tmp/tt-sys-devices
kubectl label node "${node}" tenstorrent.com/ttsim=true --overwrite
kind load docker-image --name "${cluster}" "${image}"
kubectl create namespace monitoring
test "$(kubectl get namespace monitoring -o jsonpath='{.metadata.labels.kubernetes\.io/metadata\.name}')" = monitoring
kubectl kustomize deploy/kubernetes/overlays/ttsim >"${work}/exporter.yaml"
sed -i -e "s#image: tt-metrics-exporter:ttsim#image: ${image}#" -e 's#/sys/class/tenstorrent#/tmp/tt-sysfs#' -e 's#/sys/devices#/tmp/tt-sys-devices#' "${work}/exporter.yaml"
kubectl apply -f "${work}/exporter.yaml"
kubectl rollout status daemonset/tt-metrics-exporter --timeout="${timeout}"

pod_ip=$(kubectl get pod -l app.kubernetes.io/name=tt-metrics-exporter -o jsonpath='{.items[0].status.podIP}')
kubectl run allowed-client --namespace monitoring --image "${image}" --image-pull-policy Never --restart Never --attach --rm --pod-running-timeout="${timeout}" --command -- python3 -c "import urllib.request; print(urllib.request.urlopen('http://${pod_ip}:9400/healthz', timeout=5).read().decode(), end='')"
if kubectl run denied-client --image "${image}" --image-pull-policy Never --restart Never --attach --rm --pod-running-timeout="${timeout}" --command -- python3 -c "import urllib.request; urllib.request.urlopen('http://${pod_ip}:9400/healthz', timeout=5).read()"; then
  echo "default namespace unexpectedly reached the exporter" >&2
  exit 1
fi
kubectl get networkpolicy tt-metrics-exporter -o yaml
