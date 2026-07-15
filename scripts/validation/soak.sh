#!/usr/bin/env bash
# Run a bounded real-sysfs exporter soak and preserve its evidence.
set -euo pipefail

image=${1:?usage: soak.sh IMAGE OUTPUT_CSV}
output=${2:?usage: soak.sh IMAGE OUTPUT_CSV}
duration=${SOAK_DURATION_SECONDS:-300}
if test "${output%.csv}" = "${output}"; then
  echo "soak output must end in .csv" >&2
  exit 2
fi
summary=${output%.csv}.json
container="tt-exporter-soak-$$"
cleanup() {
  docker rm -f "${container}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

mkdir -p "$(dirname "${output}")"
sudo -n true
docker run --detach --name "${container}" --read-only --cap-drop ALL --security-opt no-new-privileges --mount type=bind,src=/sys/class/tenstorrent,dst=/mnt/tt/sysfs/class/tenstorrent,readonly --mount type=bind,src=/sys/devices,dst=/mnt/tt/sysfs/devices,readonly --publish 127.0.0.1::9400 "${image}" --sysfs-root /mnt/tt/sysfs/class/tenstorrent --poll-interval 1 --max-snapshot-age 5 --require-device --shutdown-grace-period 3 --log-format json >/dev/null
port=$(docker port "${container}" 9400/tcp | sed 's/.*://')
for _ in $(seq 1 60); do
  if curl --fail --silent "http://127.0.0.1:${port}/readyz" >/dev/null; then
    break
  fi
  sleep 2
done
curl --fail --silent "http://127.0.0.1:${port}/readyz" >/dev/null
pid=$(docker inspect "${container}" --format '{{.State.Pid}}')
sudo -n python3 scripts/operations/soak_test.py --endpoint "http://127.0.0.1:${port}/metrics" --pid "${pid}" --duration-seconds "${duration}" --output "${output}"
sudo -n chown "$(id -u):$(id -g)" "${output}" "${summary}"
docker stop --time 3 "${container}" >/dev/null
test "$(docker inspect "${container}" --format '{{.State.ExitCode}}')" = 0
