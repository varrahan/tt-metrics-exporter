#!/usr/bin/env bash
# Render and validate every supported Kubernetes overlay.
set -euo pipefail

for environment in base ttsim production; do
  manifest=$(mktemp)
  if test "${environment}" = base; then
    source_dir=deploy/kubernetes/base
  else
    source_dir="deploy/kubernetes/overlays/${environment}"
  fi
  kubectl kustomize "${source_dir}" >"${manifest}"
  python3 scripts/validation/manifests.py "${manifest}" "${environment}"
  chmod 0644 "${manifest}"
  docker run --rm \
    --mount "type=bind,src=${manifest},dst=/manifest.yaml,readonly" \
    ghcr.io/yannh/kubeconform@sha256:85dbef6b4b312b99133decc9c6fc9495e9fc5f92293d4ff3b7e1b30f5611823c \
    -kubernetes-version 1.34.0 -strict -summary /manifest.yaml
  rm -f "${manifest}"
done
