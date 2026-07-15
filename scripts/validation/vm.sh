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
command -v curl

lspci -nn | tee "${evidence_dir}/lspci.txt"
lspci -nn | grep -i '1e52:'
lsmod | tee "${evidence_dir}/lsmod.txt" | grep tenstorrent
test -d /sys/class/tenstorrent
find /sys/class/tenstorrent -maxdepth 1 -mindepth 1 -printf '%f\n' | tee "${evidence_dir}/sysfs-devices.txt" | grep .
find /dev -maxdepth 2 -name 'tenstorrent*' -print | tee "${evidence_dir}/character-devices.txt" | grep .

docker run --rm --mount type=bind,src=/sys/class/tenstorrent,dst=/mnt/tt/sysfs/class/tenstorrent,readonly --mount type=bind,src=/sys/devices,dst=/mnt/tt/sysfs/devices,readonly "${image}" --sysfs-root /mnt/tt/sysfs/class/tenstorrent --once --json >"${evidence_dir}/devices.json"
python3 -c 'import json,sys; data=json.load(open(sys.argv[1])); assert data["summary"]["devicesDiscovered"] > 0; assert all(device["pci"]["vendorId"] == "0x1e52" and device["pci"]["bdf"] and device["characterDevice"] for device in data["devices"])' "${evidence_dir}/devices.json"
scripts/validation/image.sh "${image}" "${evidence_dir}/image"

scripts/validation/manifests.sh
scripts/validation/monitoring.sh
scripts/validation/kind_rollout.sh "${image}" 2>&1 | tee "${evidence_dir}/kind-rollout.log"
scripts/validation/kind_network_policy.sh "${image}" 2>&1 | tee "${evidence_dir}/network-policy.log"
scripts/validation/soak.sh "${image}" "${evidence_dir}/soak.csv"

echo "VM validation completed."
