# Tenstorrent Metrics Exporter Documentation

The exporter is a Python node-local service for Tenstorrent hosts. It scans
`/sys/class/tenstorrent`, exposes Prometheus metrics at `/metrics`, and exposes
device data at `/v1/devices`.

Runtime validation that depends on `tt-kmd`, `/sys/class/tenstorrent`,
`/dev/tenstorrent`, Docker, `kind`, or TT-Metalium must run from the QEMU
`ttsim` VM or a physical Tenstorrent host. Host-side checks are suitable for
package builds and unit tests only.

Do not use `tt-smi` as an exporter source. The exporter collects only from safe
node-local sources: `tt-kmd` sysfs, backing PCI sysfs, `hwmon` when available,
Kubernetes allocation state, janitor state, and atomically published
TT-Metalium workload profiler snapshots.

When using Tenstorrent's QEMU bridge, use its documented TCG configuration with
`-cpu max`; do not enable KVM or use `-cpu host`. The current bridge supports
PCI enumeration and compatible KMD binding but not the complete TTNN device-open
path.

See:

- [OPERATIONAL_CONTRACT.md](info/OPERATIONAL_CONTRACT.md) for stable endpoint,
  source-criticality, and validation semantics.
- [STATE_INGESTION_SECURITY.md](info/STATE_INGESTION_SECURITY.md) for state-root
  ownership, secure traversal, profiler isolation, and cleanup policy.
- [METRIC_COMPATIBILITY.md](info/METRIC_COMPATIBILITY.md) and the
  [v1 JSON Schema](schema/telemetry.tenstorrent.com-v1.schema.json) for stable
  monitoring and API contracts.
- [CONTAINER.md](info/CONTAINER.md) for reproducible image builds and final-image
  security/lifecycle validation.
- [KUBERNETES.md](info/KUBERNETES.md) for base/overlay deployment, monitoring,
  NetworkPolicy, and cluster-validation boundaries.
- [HOW_IT_WORKS.md](guides/HOW_IT_WORKS.md) for the component architecture and
  end-to-end data flow.
- [TT_METRICS_EXPORTER.md](guides/TT_METRICS_EXPORTER.md) for telemetry source
  and state-file contracts. Setup and run instructions are in the repository
  [README](../README.md).
