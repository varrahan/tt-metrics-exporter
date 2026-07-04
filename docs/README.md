# Tenstorrent Metrics Exporter Documentation

This documentation is local to the telemetry exporter so `src/telemetry` can be
split into a separate repository without depending on the root project docs.

The exporter is a C++ node-local service for Tenstorrent hosts. It scans
`/sys/class/tenstorrent`, exposes Prometheus metrics at `/metrics`, and exposes
typed DRA-friendly device data at `/v1/devices`.

Runtime validation that depends on `tt-kmd`, `/sys/class/tenstorrent`,
`/dev/tenstorrent`, Docker, `kind`, TT-Metalium, or `tt-smi` must run from the
QEMU `ttsim` VM or a physical Tenstorrent host. Host-side checks are suitable
for CMake builds and unit tests only.

When using QEMU, launch the VM with host CPU passthrough, for example
`-cpu host`. Current Tenstorrent user-space wheels require CPU features such as
AVX/AVX2 and may fail with `Illegal instruction` under QEMU's default virtual
CPU model.

See [TT_METRICS_EXPORTER.md](guides/TT_METRICS_EXPORTER.md) for the local
implementation guide.
