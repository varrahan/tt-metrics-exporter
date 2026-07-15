#!/usr/bin/env bash
# Render monitoring resources and validate their Prometheus rules.
set -euo pipefail

rendered=$(mktemp)
rules=$(mktemp)
prometheus_image=prom/prometheus:v3.5.0@sha256:63805ebb8d2b3920190daf1cb14a60871b16fd38bed42b857a3182bc621f4996
cleanup() {
  rm -f "${rendered}" "${rules}"
}
trap cleanup EXIT

kubectl kustomize deploy/kubernetes/monitoring >"${rendered}"
python3 scripts/validation/monitoring.py "${rendered}" "${rules}"
chmod 0644 "${rules}"
docker run --rm --entrypoint /bin/promtool --mount "type=bind,src=${rules},dst=/rules.yaml,readonly" "${prometheus_image}" check rules /rules.yaml
docker run --rm --entrypoint /bin/promtool --mount "type=bind,src=${rules},dst=/rules.yaml,readonly" --mount "type=bind,src=$(realpath scripts/validation/prometheus-rule-tests.yaml),dst=/tests.yaml,readonly" "${prometheus_image}" test rules /tests.yaml
