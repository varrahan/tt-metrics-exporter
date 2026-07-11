# Production Container

The multi-stage [`Dockerfile`](../../Dockerfile) pins build and runtime bases by
digest, pins Python build packages, and runs the complete contract suite before
building the Python wheel. The final distroless Python runtime uses UID/GID `65532`, has no
shell, compiler, package manager, TTNN, or publisher, and exposes pre-created
read-only mount targets below `/mnt/tt`. It disables bytecode writes and needs
no writable application directory.

Build reproducibly by supplying release metadata:

```bash
docker build \
  --build-arg VERSION=0.1.0 \
  --build-arg REVISION="$(git rev-parse HEAD)" \
  --build-arg SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH}" \
  --build-arg BUILD_DATE=1970-01-01T00:00:00Z \
  -t tt-metrics-exporter:0.1.0 .
```

`SOURCE_DATE_EPOCH` is exposed by `--version`; version and revision are also in
`tt_exporter_build_info`. Production should replace `NOASSERTION` in the OCI
license label when the repository adopts a license.

Validate the final image with:

```bash
scripts/validation/image.sh tt-metrics-exporter:0.1.0 image-reports
```

The validation checks metadata and non-root identity, rejects runtime shell,
compiler, package-manager, and generated application bytecode paths, requires
the Python module entry point, creates SPDX JSON and SARIF vulnerability
reports, and runs the final image with a read-only root,
`no-new-privileges`, all capabilities dropped, read-only telemetry input,
health/readiness/metrics checks, and bounded graceful termination.
