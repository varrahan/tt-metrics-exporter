#!/usr/bin/env bash
# Validate the final runtime image and its lifecycle contract.
set -euo pipefail

image=${1:?usage: validate-image.sh IMAGE [REPORT_DIR]}
report_dir=${2:-image-reports}
mkdir -p "${report_dir}"
report_dir=$(realpath "${report_dir}")
syft_image=${SYFT_IMAGE:-anchore/syft:v1.44.0@sha256:86fde6445b483d902fe011dd9f68c4987dd94e07da1e9edc004e3c2422650de6}
trivy_image=${TRIVY_IMAGE:-aquasec/trivy:0.70.0@sha256:be1190afcb28352bfddc4ddeb71470835d16462af68d310f9f4bca710961a41e}
sysfs_root=$(mktemp -d)
image_archive=$(mktemp)
container_name="tt-exporter-image-test-$$"
cleanup() {
  docker rm -f "${container_name}" >/dev/null 2>&1 || true
  rm -rf "${sysfs_root}"
  rm -f "${image_archive}"
}
trap cleanup EXIT

user=$(docker image inspect "${image}" --format '{{.Config.User}}')
test "${user}" = "65532:65532"
test "$(docker image inspect "${image}" --format '{{index .Config.Labels "org.opencontainers.image.version"}}')" != ""
test "$(docker image inspect "${image}" --format '{{index .Config.Labels "org.opencontainers.image.revision"}}')" != ""
test "$(docker image inspect "${image}" --format '{{index .Config.Labels "org.opencontainers.image.licenses"}}')" = "Apache-2.0"
test "$(docker image inspect "${image}" --format '{{json .Config.Entrypoint}}')" = \
  '["python3","-m","tt_metrics_exporter"]'
docker image inspect "${image}" --format '{{range .Config.Env}}{{println .}}{{end}}' \
  | grep -qx 'PYTHONDONTWRITEBYTECODE=1'

docker run --rm "${image}" --version
inspection_container=$(docker create "${image}")
filesystem_listing=$(mktemp)
docker export "${inspection_container}" | tar -tf - >"${filesystem_listing}"
docker rm "${inspection_container}" >/dev/null
if grep -Eq '(^|/)(bin/(sh|bash)|usr/bin/(g\+\+|gcc|cmake)|usr/bin/apt|var/lib/dpkg/status)$' "${filesystem_listing}"; then
  echo "runtime image contains a forbidden shell, compiler, or package manager" >&2
  rm -f "${filesystem_listing}"
  exit 1
fi
grep -qx 'usr/bin/python3' "${filesystem_listing}"
grep -qx 'app/tt_metrics_exporter/__main__.py' "${filesystem_listing}"
grep -qx 'licenses/LICENSE' "${filesystem_listing}"
if grep -Eq '^app/.*(__pycache__|\.py[co]$)' "${filesystem_listing}"; then
  echo "runtime application contains generated Python bytecode" >&2
  rm -f "${filesystem_listing}"
  exit 1
fi
rm -f "${filesystem_listing}"
if test "${SKIP_SUPPLY_CHAIN:-0}" != "1"; then
  docker save --output "${image_archive}" "${image}"
  docker run --rm --mount "type=bind,src=${image_archive},dst=/image.tar,readonly" "${syft_image}" docker-archive:/image.tar --output spdx-json >"${report_dir}/sbom.spdx.json"
  docker run --rm --mount "type=bind,src=${image_archive},dst=/image.tar,readonly" "${trivy_image}" image --input /image.tar --scanners vuln --format sarif --severity CRITICAL --ignore-unfixed --exit-code 1 >"${report_dir}/vulnerabilities.sarif"
  test -s "${report_dir}/sbom.spdx.json"
  test -s "${report_dir}/vulnerabilities.sarif"
fi

mkdir -p "${sysfs_root}/0"
chmod 0755 "${sysfs_root}" "${sysfs_root}/0"

docker run --detach --name "${container_name}" \
  --read-only --cap-drop ALL --security-opt no-new-privileges \
  --mount "type=bind,src=${sysfs_root},dst=/mnt/tt/sysfs,readonly" \
  --publish 127.0.0.1::9400 "${image}" \
  --sysfs-root /mnt/tt/sysfs \
  --poll-interval 1 --max-snapshot-age 3 --require-device \
  --shutdown-grace-period 3 --log-format json >/dev/null

host_port=$(docker port "${container_name}" 9400/tcp | sed 's/.*://')
for _ in $(seq 1 30); do
  if curl --fail --silent "http://127.0.0.1:${host_port}/healthz" >/dev/null \
     && curl --fail --silent "http://127.0.0.1:${host_port}/readyz" >/dev/null \
     && curl --fail --silent "http://127.0.0.1:${host_port}/metrics" >/dev/null; then
    break
  fi
  sleep 1
done
curl --fail --silent "http://127.0.0.1:${host_port}/healthz" >/dev/null
curl --fail --silent "http://127.0.0.1:${host_port}/readyz" >/dev/null
curl --fail --silent "http://127.0.0.1:${host_port}/metrics" >/dev/null

started=$(date +%s)
grace_period=3
docker stop --time "${grace_period}" "${container_name}" >/dev/null
elapsed=$(($(date +%s) - started))
test "${elapsed}" -le $((grace_period + 2))
test "$(docker inspect "${container_name}" --format '{{.State.ExitCode}}')" = "0"
