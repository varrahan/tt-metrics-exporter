# Tenstorrent Metrics Exporter

This directory contains the C++ Tenstorrent metrics exporter described in
[`docs/guides/TT_METRICS_EXPORTER.md`](docs/guides/TT_METRICS_EXPORTER.md).
It is kept self-contained so it can be split into a separate repository.

The exporter is intended to run as a node-local DaemonSet in the QEMU `ttsim`
VM and, later, on physical Tenstorrent hosts. Runtime validation that depends on
`tt-kmd`, `/sys/class/tenstorrent`, `/dev/tenstorrent`, Docker, `kind`, or DRA
APIs must be performed from the VM.

Current implementation scope:

- Scan `/sys/class/tenstorrent` for device entries.
- Read tolerant sysfs telemetry files such as `memory_usage`, architecture,
  board type, health, and optional Tensix count files when present.
- Read documented `tt-kmd` firmware telemetry files such as `tt_card_type`,
  `tt_aiclk`, `tt_heartbeat`, firmware versions, ASIC ID, and thermal trip
  count when ARC firmware exposes those attributes.
- Infer card-level static capacities such as ASIC count, Tensix core count,
  SRAM bytes, memory bytes, and memory bandwidth from firmware-reported card
  type when present.
- Read Tenstorrent `hwmon` sensor inputs when the driver registers a hwmon
  device under the Tenstorrent sysfs node.
- Expose Linux character-device identity from `dev` and `uevent`.
- Expose PCI identity, class, NUMA node, and non-empty PCI resource ranges from
  the backing `device/` sysfs directory.
- Expose runtime power-management state and counters from `power/`.
- Render Prometheus text metrics including `tt_memory_used_bytes`,
  `tt_memory_total_bytes`, `tt_tensix_cores_used`, and
  `tt_tensix_cores_available` when source data is available.
- Cache telemetry in a polling loop and serve `/metrics` and `/v1/devices` on
  port `9400`.

TT-Metalium profiler integration and Kubernetes DaemonSet packaging are the
next implementation layers after this sysfs exporter baseline.

The current QEMU `ttsim` VM may not expose firmware telemetry attributes. In
that case the exporter still reports device, PCI, BAR, and power-management
metadata, while firmware, card-spec, hwmon, live memory, and Tensix-utilization
metrics remain empty.

PCIe performance counter files under `pcie_perf_counters/` are available behind
`--collect-pcie-counters`. They are disabled by default because the current
`ttsim` VM disconnected while reading one of those simulator sysfs files
directly.

## Build And Test

Host-side build and unit tests are lightweight checks and do not require VM
hardware:

```bash
cmake -S src/telemetry -B /tmp/tt-metrics-exporter-build
cmake --build /tmp/tt-metrics-exporter-build
ctest --test-dir /tmp/tt-metrics-exporter-build --output-on-failure
```

When working from `src/telemetry` as a standalone checkout:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

From inside the VM, print one scrape from the actual sysfs root:

```bash
/tmp/tt-metrics-exporter-build/tt-metrics-exporter \
  --sysfs-root /sys/class/tenstorrent \
  --once
```

Print one structured device inventory:

```bash
/tmp/tt-metrics-exporter-build/tt-metrics-exporter \
  --sysfs-root /sys/class/tenstorrent \
  --once \
  --json
```

Run the HTTP exporter from inside the VM:

```bash
/tmp/tt-metrics-exporter-build/tt-metrics-exporter \
  --sysfs-root /sys/class/tenstorrent \
  --port 9400
```

Then scrape:

```bash
curl http://127.0.0.1:9400/metrics
curl http://127.0.0.1:9400/v1/devices
```
