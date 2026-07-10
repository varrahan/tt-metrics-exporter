# Tenstorrent Metrics Exporter Documentation

This documentation is local to the telemetry exporter so `src/telemetry` can be
split into a separate repository without depending on the root project docs.

The exporter is a C++ node-local service for Tenstorrent hosts. It scans
`/sys/class/tenstorrent`, exposes Prometheus metrics at `/metrics`, and exposes
typed DRA-friendly device data at `/v1/devices`.

Runtime validation that depends on `tt-kmd`, `/sys/class/tenstorrent`,
`/dev/tenstorrent`, Docker, `kind`, or TT-Metalium must run from the QEMU
`ttsim` VM or a physical Tenstorrent host. Host-side checks are suitable for
CMake builds and unit tests only.

Do not use `tt-smi` as an exporter source. The exporter collects only from safe
node-local sources: `tt-kmd` sysfs, backing PCI sysfs, `hwmon` when available,
Kubernetes allocation state, janitor state, and atomically published
TT-Metalium workload profiler snapshots.

Shared VM prerequisites are tracked outside this component in the parent
repository's `vm/` directory.

When using Tenstorrent's QEMU bridge, use its documented TCG configuration with
`-cpu max`; do not enable KVM or use `-cpu host`. The current bridge supports
PCI enumeration and compatible KMD binding but not the complete TTNN device-open
path.

See:

- [HOW_IT_WORKS.md](guides/HOW_IT_WORKS.md) for the component architecture and
  end-to-end data flow.
- [TT_METRICS_EXPORTER.md](guides/TT_METRICS_EXPORTER.md) for the build and
  operation guide.
