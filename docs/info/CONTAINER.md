# Production Container

The multi-stage [`Dockerfile`](../../Dockerfile) uses the Docker Official
`python:3.11-slim-bookworm` image for builds and a distroless Python image for
the runtime. Both bases are pinned by digest. Python build packages are pinned
by the hashed development requirements, and the complete contract suite runs
before building the Python wheel. The final runtime uses UID/GID `65532`, has
no shell, compiler, package manager, TTNN, or publisher, and exposes
pre-created read-only mount targets below `/mnt/tt`. It disables bytecode
writes and needs no writable application directory.

Build reproducibly by supplying release metadata:

```bash
docker build \
  --build-arg VERSION=0.1.0 \
  --build-arg REVISION="$(git rev-parse HEAD)" \
  --build-arg SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH}" \
  --build-arg BUILD_DATE=1970-01-01T00:00:00Z \
  -t tt-metrics-exporter:0.1.0 .
```

A plain local `docker build .` uses the package's current version.
Release builds still pass and verify explicit immutable version metadata.

`SOURCE_DATE_EPOCH` is exposed by `--version`; version and revision are also in
`tt_exporter_build_info`. The OCI license label is `Apache-2.0`, and the final
image carries the complete license text at `/licenses/LICENSE`.

Validate the final image with:

```bash
scripts/validation/image.sh tt-metrics-exporter:0.1.0 image-reports
```

The validation checks metadata and non-root identity, rejects runtime shell,
compiler, package-manager, and generated application bytecode paths, requires
the Python module entry point and Apache-2.0 license metadata/text, creates
SPDX JSON with pinned Syft and SARIF
critical-vulnerability results with pinned Trivy, and runs the final image with a read-only root,
`no-new-privileges`, all capabilities dropped, read-only telemetry input,
health/readiness/metrics checks, and bounded graceful termination.

The scanners read a temporary `docker save` archive. They do not require Docker
SBOM or Scout plugins and are not given the Docker socket.
