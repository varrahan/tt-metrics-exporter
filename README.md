# Tenstorrent Metrics Exporter

This directory contains the Python Tenstorrent metrics exporter described in
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
- Read Tenstorrent `hwmon` sensor inputs when the driver registers a hwmon
  device under the Tenstorrent sysfs node.
- Expose Linux character-device identity from `dev` and `uevent`.
- Expose PCI BDF, bound driver, identity, class, NUMA node, IOMMU group, link
  speed/width, reset method, and non-empty PCI resource ranges from the backing
  `device/` sysfs directory.
- Expose runtime power-management state and counters from `power/`.
- Report actual memory usage, total/free/available capacity, memory type,
  memory bandwidth, and controller topology when safe files expose those
  values.
- Report actual Tensix usage, total/available counts, mesh dimensions,
  topology, and active regions when safe driver, runtime, or profiler files
  expose those values.
- Report fault, reset-required, OOM, and hang counters when safe driver or
  node-local health files expose those values.
- Report scale-out and fabric link state from safe link directories such as
  `scaleout_links/`, `ethernet_links/`, or `fabric_links/`.
- Optionally read Kubernetes DRA allocation ownership from
  `--allocation-state-root` and hardware janitor state from
  `--janitor-state-root`.
- Read fresh, atomically published TT-Metalium workload profiler samples from
  `--metalium-profiler-state-root`, expose per-workload core occupancy, and
  ignore samples after a configurable freshness window.
- Render Prometheus text metrics including `tt_memory_used_bytes`,
  `tt_memory_total_bytes`, `tt_tensix_cores_used`, and
  `tt_tensix_cores_available` only when a safe source reports those values.
- Cache telemetry in a polling loop and serve `/metrics` and `/v1/devices` on
  port `9400`, with shallow liveness at `/healthz` and snapshot readiness at
  `/readyz`.

The workload-side TTNN publisher lives at
`integrations/ttnn/metalium_profiler_publisher.py`. Kubernetes DaemonSet,
monitoring, and environment overlays live under `deploy/kubernetes`.

The exporter does not run `tt-smi` and should not grow a `tt-smi` subprocess
path. If a value only exists through `tt-smi`, treat that as a missing safe
source and add a lower-level sysfs, Kubernetes, or TT-Metalium/profiler source
before depending on it.

The exporter does not synthesize static capacity from a card-type table.
Capacity, memory, core, and bandwidth values must come from the device driver,
firmware telemetry, Kubernetes allocation state, or TT-Metalium/profiler data.

The stable liveness, readiness, snapshot, and critical-source behavior is
defined in [`docs/info/OPERATIONAL_CONTRACT.md`](docs/info/OPERATIONAL_CONTRACT.md).

The current QEMU `ttsim` VM may not expose firmware telemetry attributes. In
that case the exporter still reports device, PCI, BAR, and power-management
metadata, while firmware, hwmon, live memory, and Tensix-utilization metrics
remain empty.

PCIe performance counter files under `pcie_perf_counters/` are available behind
`--collect-pcie-counters`. They are disabled by default because the current
`ttsim` VM disconnected while reading one of those simulator sysfs files
directly.

## Build And Test

Host-side build and unit tests are lightweight checks and do not require VM
hardware:

```bash
scripts/run-tests.py
```

From inside the VM, print one scrape from the actual sysfs root:

```bash
PYTHONPATH=src python3 -m tt_metrics_exporter \
  --sysfs-root /sys/class/tenstorrent \
  --once
```

Print one structured device inventory:

```bash
PYTHONPATH=src python3 -m tt_metrics_exporter \
  --sysfs-root /sys/class/tenstorrent \
  --once \
  --json
```

Run the HTTP exporter from inside the VM:

```bash
PYTHONPATH=src python3 -m tt_metrics_exporter \
  --sysfs-root /sys/class/tenstorrent \
  --max-snapshot-age 15 \
  --shutdown-grace-period 10 \
  --http-request-deadline 2 \
  --log-format text \
  --log-level info \
  --port 9400
```

Include node-local DRA and janitor state when those agents write safe state
files:

```bash
PYTHONPATH=src python3 -m tt_metrics_exporter \
  --sysfs-root /sys/class/tenstorrent \
  --allocation-state-root /var/lib/tt-device-plugin/allocations \
  --janitor-state-root /var/lib/tt-device-plugin/janitor \
  --metalium-profiler-state-root /var/lib/tt-device-plugin/metalium-profiler \
  --port 9400
```

## TT-Metalium Workload Samples

TT-Metalium profiler results are process-local. Instrument the TTNN workload to
publish its latest completed-program core footprint after a synchronized
iteration:

```python
import os
import ttnn
from metalium_profiler_publisher import MetaliumProfilerPublisher

publisher = MetaliumProfilerPublisher(
    state_root=os.environ["TT_METALIUM_PROFILER_STATE_ROOT"],
    workload_id="default/model-worker-0",
)

# Run one or more TTNN operations, then sample from the same process.
ttnn.synchronize_device(device)
publisher.sample(device)
```

The profiler variables must be set before TTNN initializes:

```bash
export TT_METAL_DEVICE_PROFILER=1
export TT_METAL_PROFILER_MID_RUN_DUMP=1
export TT_METAL_PROFILER_CPP_POST_PROCESS=1
export TT_METAL_PROFILER_DISABLE_DUMP_TO_FILES=1
# This path is the workload's isolated <pod-uid> mount, not the shared parent.
export TT_METALIUM_PROFILER_STATE_ROOT=/var/run/tt-profiler-state
```

Device profiling requires a profiler-enabled TT-Metalium source build. The
current `ttnn==0.73.1` wheel installed in the QEMU VM is not Tracy-enabled and
fails fast if `TT_METAL_DEVICE_PROFILER=1` is used. For TT-Metalium v0.73.1,
`./build_metal.sh` enables Tracy by default; do not pass `--disable-profiler`.
The equivalent manual CMake setting is `-DENABLE_TRACY=ON`.

With that source build active, run the included dynamic workload in one VM
terminal:

```bash
python integrations/ttnn/example_dynamic_workload.py \
  --state-root /var/lib/tt-device-plugin/metalium-profiler \
  --pod-uid metalium-dynamic-example \
  --device-key 0 \
  --iterations 30 \
  --interval-seconds 1
```

In another terminal, watch the workload series change and then return to zero
when the publisher exits:

```bash
watch -n 1 'curl -s http://127.0.0.1:9400/metrics | grep ^tt_metalium_workload_'
```

The publisher reports the maximum `ProgramAnalysisData.core_count` in the most
recent read and the corresponding `num_available_cores`. The resulting
`tt_metalium_workload_core_occupancy_ratio` is spatial occupancy of recently
completed programs, not a time-weighted hardware-busy percentage.

Use `--device-key` when TT-Metalium's runtime chip ID differs from the
exporter's sysfs directory name. It accepts a sysfs ID, PCI BDF, or character
device basename. `TT_METAL_PROFILER_DISABLE_DUMP_TO_FILES=1` retains the
in-process results while avoiding profiler CSV artifacts in the workload
container.

Production publication is best-effort and rate-bounded by default so telemetry
failures do not terminate the workload. DRA must mount only the workload's v2
subtree and delete it during Unprepare; see
[`docs/info/STATE_INGESTION_SECURITY.md`](docs/info/STATE_INGESTION_SECURITY.md).

Then scrape:

```bash
curl http://127.0.0.1:9400/metrics
curl http://127.0.0.1:9400/v1/devices
```
