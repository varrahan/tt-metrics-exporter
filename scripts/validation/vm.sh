#!/usr/bin/env bash
# Qualify a release image in TTSim or on physical Tenstorrent hardware.
set -euo pipefail

image=${1:?usage: validate-vm.sh RELEASE_IMAGE EVIDENCE_DIR}
evidence_dir=${2:?usage: validate-vm.sh RELEASE_IMAGE EVIDENCE_DIR}
mkdir -p "${evidence_dir}"
exec > >(tee "${evidence_dir}/vm-validation.log") 2>&1

test "${TT_QEMU_TCG_CPU_MAX_CONFIRMED:-}" = "1" || {
  echo "Set TT_QEMU_TCG_CPU_MAX_CONFIRMED=1 only after confirming TCG with -cpu max" >&2
  exit 1
}
command -v lspci
command -v docker
command -v kubectl
command -v kind

lspci -nn | tee "${evidence_dir}/lspci.txt"
lspci -nn | grep -i '1e52:'
lsmod | tee "${evidence_dir}/lsmod.txt" | grep tenstorrent
test -d /sys/class/tenstorrent
find /sys/class/tenstorrent -maxdepth 1 -mindepth 1 -printf '%f\n' \
  | tee "${evidence_dir}/sysfs-devices.txt" | grep .
find /dev -maxdepth 2 -name 'tenstorrent*' -print \
  | tee "${evidence_dir}/character-devices.txt" | grep .

docker run --rm \
  --mount type=bind,src=/sys/class/tenstorrent,dst=/mnt/tt/sysfs/class/tenstorrent,readonly \
  --mount type=bind,src=/sys/devices,dst=/mnt/tt/sysfs/devices,readonly \
  "${image}" --sysfs-root /mnt/tt/sysfs/class/tenstorrent \
  --once >/dev/null
SKIP_SUPPLY_CHAIN=1 scripts/validation/image.sh "${image}" "${evidence_dir}/image"

kubectl version -o yaml | tee "${evidence_dir}/kubernetes-version.yaml"
server_minor=$(kubectl version -o json | python3 -c \
  'import json,sys; print(json.load(sys.stdin)["serverVersion"]["minor"].rstrip("+"))')
test "${server_minor}" -ge 34

scripts/validation/manifests.sh
scripts/validation/monitoring.sh
docker tag "${image}" tt-metrics-exporter:ttsim
kind load docker-image tt-metrics-exporter:ttsim
kubectl label node --all tenstorrent.com/ttsim=true --overwrite
kubectl apply -k deploy/kubernetes/overlays/ttsim
kubectl rollout status daemonset/tt-metrics-exporter --timeout=5m
kubectl get daemonset,pod,service,networkpolicy -o wide \
  | tee "${evidence_dir}/kubernetes-resources.txt"
kubectl auth can-i --as=system:serviceaccount:default:tt-metrics-exporter \
  --list | tee "${evidence_dir}/service-account-access.txt"
kubectl rollout restart daemonset/tt-metrics-exporter
kubectl rollout status daemonset/tt-metrics-exporter --timeout=5m
kubectl rollout undo daemonset/tt-metrics-exporter
kubectl rollout status daemonset/tt-metrics-exporter --timeout=5m
test "$(kubectl get daemonset tt-metrics-exporter -o jsonpath='{.spec.template.metadata.labels.telemetry\.tenstorrent\.com/implementation}')" = python
test "$(kubectl get pod -l app.kubernetes.io/name=tt-metrics-exporter -o jsonpath='{.items[0].status.containerStatuses[0].ready}')" = true

scripts/ci/run_tests.py 2>&1 | tee "${evidence_dir}/contract-tests.txt"

echo "VM validation completed; attach CNI NetworkPolicy and controlled alert evidence manually."
