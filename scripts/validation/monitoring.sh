#!/usr/bin/env bash
# Render monitoring resources and validate their Prometheus rules.
set -euo pipefail

rendered=$(mktemp)
rules=$(mktemp)
cleanup() {
  rm -f "${rendered}" "${rules}"
}
trap cleanup EXIT

kubectl kustomize deploy/kubernetes/monitoring >"${rendered}"
python3 scripts/validation/monitoring.py "${rendered}" "${rules}"
chmod 0644 "${rules}"
docker run --rm --entrypoint /bin/promtool \
  --mount "type=bind,src=${rules},dst=/rules.yaml,readonly" \
  prom/prometheus:v3.5.0 check rules /rules.yaml
