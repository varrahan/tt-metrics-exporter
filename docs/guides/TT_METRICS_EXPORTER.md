# Tenstorrent DRA Metrics Exporter

This service is the telemetry boundary for the Kubernetes DRA driver. It runs
node-local, discovers Tenstorrent devices, and exposes both Prometheus metrics
and structured JSON for provisioning logic.

## Runtime Sources

- `/sys/class/tenstorrent`: device discovery, firmware attributes, hwmon,
  runtime power state, PCI identity, PCI resources, and optional simulator
  counters.
- `/dev/tenstorrent`: character devices that can be mounted into containers.
- `tt-smi`: system management data when installed and functional.
- TT-Metalium: future source for live Tensix core usage and profiler data.

In the current `ttsim` VM, run QEMU with `-cpu host` before installing or
executing Tenstorrent wheels. The default QEMU virtual CPU can lack AVX/AVX2 and
cause `tt-smi` or `ttnn` to fail with `Illegal instruction`.

## Endpoints

- `GET /healthz`: process health.
- `GET /metrics`: Prometheus exposition text.
- `GET /v1/devices`: typed device inventory for DRA and provisioning code.

`/v1/devices` preserves missing values as JSON `null`. The exporter should not
invent runtime memory or core data when the simulator or driver does not expose
that information.

## Build And Test

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

From the QEMU VM or a physical Tenstorrent host:

```bash
./build/tt-metrics-exporter --sysfs-root /sys/class/tenstorrent --once
./build/tt-metrics-exporter --sysfs-root /sys/class/tenstorrent --once --json
./build/tt-metrics-exporter --sysfs-root /sys/class/tenstorrent --port 9400
```

## Current Simulator Caveat

The current `ttsim` VM may only expose PCI, BAR, character-device, and runtime
power-management metadata. Firmware, hwmon, live memory, and Tensix utilization
families remain empty until `tt-kmd`, firmware telemetry, `tt-smi`, or
TT-Metalium exposes those values.

Root-level `tt-smi` probing currently reaches UMD topology discovery and then
crashes this simulator instance. Treat `tt-smi` as installed but unsafe for
runtime collection in this VM profile until the simulator profile or UMD probing
path is fixed.
