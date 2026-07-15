# Tenstorrent Metrics Exporter

This directory contains the Python Tenstorrent metrics exporter described in
[`docs/guides/TT_METRICS_EXPORTER.md`](docs/guides/TT_METRICS_EXPORTER.md).
It is kept self-contained so it can be split into a separate repository.

The exporter is intended to run as a node-local DaemonSet in the QEMU `ttsim`
VM and, later, on physical Tenstorrent hosts. Runtime validation that depends on
`tt-kmd`, `/sys/class/tenstorrent`, `/dev/tenstorrent`, Docker, `kind`, or DRA
APIs must be performed from the VM.

## Setup And Run

Run the commands in this section from the repository root. Real telemetry
collection requires Linux with `tt-kmd` loaded and a readable
`/sys/class/tenstorrent` tree. A development machine without Tenstorrent
hardware can still run the service against an empty synthetic sysfs root.

### 1. Install the prerequisites

Install Git and `curl`, then install `uv`. `uv` creates the project virtual
environment and obtains a compatible Python version when one is not already
available:

```bash
git --version
curl --version
curl -LsSf https://astral.sh/uv/install.sh | sh
```

If the installer asks you to update your `PATH`, follow its printed instruction
or start a new shell. Then verify the installation:

```bash
uv --version
```

Other supported `uv` installation methods are documented at
[docs.astral.sh/uv/getting-started/installation](https://docs.astral.sh/uv/getting-started/installation/).

### 2. Clone the exporter

```bash
git clone https://github.com/varrahan/tt-metrics-exporter.git
cd tt-metrics-exporter
```

### 3. Create the environment and install locked dependencies

```bash
uv sync --locked
uv run tt-metrics-exporter --version
```

`uv sync --locked` creates `.venv`, installs the exporter, and installs the
exact dependency set recorded by the repository lockfile.

### 4. Select the telemetry source

Inside the QEMU `ttsim` VM or on a physical Tenstorrent host, confirm that the
driver has exposed at least one device and select the real sysfs root:

```bash
test -d /sys/class/tenstorrent
find /sys/class/tenstorrent -maxdepth 1 -mindepth 1 -printf '%f\n'
export TT_SYSFS_ROOT=/sys/class/tenstorrent
```

For development without Tenstorrent hardware, create and select an empty root.
The exporter will run and expose its own health metrics, but the device list
will be empty:

```bash
mkdir -p /tmp/tt-exporter-sysfs
export TT_SYSFS_ROOT=/tmp/tt-exporter-sysfs
```

### 5. Inspect one snapshot

Print one Prometheus snapshot:

```bash
uv run tt-metrics-exporter --sysfs-root "${TT_SYSFS_ROOT}" --once
```

Print the same collection as structured JSON:

```bash
uv run tt-metrics-exporter --sysfs-root "${TT_SYSFS_ROOT}" --once --json
```

### 6. Start the HTTP service

```bash
uv run tt-metrics-exporter \
  --sysfs-root "${TT_SYSFS_ROOT}" \
  --listen-address 127.0.0.1 \
  --port 9400 \
  --poll-interval 5 \
  --max-snapshot-age 15 \
  --shutdown-grace-period 10 \
  --http-request-deadline 2 \
  --log-format text \
  --log-level info
```

On a production hardware node, add `--require-device` so readiness fails when
no Tenstorrent device is discovered. Enable `--collect-hwmon` or
`--collect-pcie-counters` only after those interfaces have been qualified on
that platform.

When DRA, janitor, and workload-profiler agents publish their node-local state,
start the exporter with those optional roots as well:

```bash
uv run tt-metrics-exporter \
  --sysfs-root /sys/class/tenstorrent \
  --allocation-state-root /var/lib/tt-device-plugin/allocations \
  --janitor-state-root /var/lib/tt-device-plugin/janitor \
  --metalium-profiler-state-root /var/lib/tt-device-plugin/metalium-profiler \
  --require-device \
  --listen-address 127.0.0.1 \
  --port 9400
```

### 7. Verify the running service

Keep the exporter running and use a second terminal:

```bash
curl --fail --silent --show-error http://127.0.0.1:9400/healthz
curl --fail --silent --show-error http://127.0.0.1:9400/readyz
curl --fail --silent --show-error http://127.0.0.1:9400/metrics | sed -n '1,40p'
curl --fail --silent --show-error http://127.0.0.1:9400/v1/devices \
  | uv run python -m json.tool
```

`/readyz` can briefly return `503` before the first complete snapshot. A
persistent `503` means a critical source is inaccessible, the snapshot is
stale, or `--require-device` was set and no device was found. See the
[operational contract](docs/info/OPERATIONAL_CONTRACT.md) for the exact
semantics.

### 8. Stop the service

Press `Ctrl-C` in the exporter terminal. The process stops accepting new
requests, completes its bounded graceful shutdown, and exits.

### Run the production container

Docker execution must happen inside the TTSim VM or on a physical Tenstorrent
host so the real sysfs paths can be mounted. Build the image:

```bash
docker build -t tt-metrics-exporter:local .
```

Run it with a read-only root filesystem and read-only telemetry mounts:

```bash
docker run --rm --name tt-metrics-exporter \
  --read-only \
  --cap-drop ALL \
  --security-opt no-new-privileges \
  --mount type=bind,src=/sys/class/tenstorrent,dst=/mnt/tt/sysfs/class/tenstorrent,readonly \
  --mount type=bind,src=/sys/devices,dst=/mnt/tt/sysfs/devices,readonly \
  --publish 127.0.0.1:9400:9400 \
  tt-metrics-exporter:local \
  --sysfs-root /mnt/tt/sysfs/class/tenstorrent \
  --require-device \
  --listen-address 0.0.0.0 \
  --port 9400
```

Verify the four HTTP endpoints with the commands from step 7. Press `Ctrl-C`
to stop and remove the container.

### Deploy to Kubernetes

The production overlay is intentionally configured with an example registry
and image digest. Do not apply it unchanged. First publish the qualified image,
replace `newName` and `digest` in
`deploy/kubernetes/overlays/production/kustomization.yaml`, and prepare every
selected Tenstorrent node:

```bash
sudo mkdir -p /var/lib/tt-device-plugin/allocations \
  /var/lib/tt-device-plugin/janitor \
  /var/lib/tt-device-plugin/metalium-profiler
kubectl label node <node-name> tenstorrent.com/accelerator=true
```

Render, review, and apply the production resources:

```bash
kubectl kustomize deploy/kubernetes/overlays/production
kubectl apply -k deploy/kubernetes/overlays/production
kubectl rollout status daemonset/tt-metrics-exporter --timeout=10m
kubectl get pods -l app.kubernetes.io/name=tt-metrics-exporter -o wide
```

The default NetworkPolicy permits ingress only from the `monitoring` namespace.
For monitoring resources and complete rollout/NetworkPolicy qualification, see
[`docs/info/KUBERNETES.md`](docs/info/KUBERNETES.md).

Current implementation scope:

- Scan `/sys/class/tenstorrent` for device entries.
- Read tolerant sysfs telemetry files such as `memory_usage`, architecture,
  board type, health, and optional Tensix count files when present.
- Read documented `tt-kmd` firmware telemetry files such as `tt_card_type`,
  `tt_aiclk`, `tt_heartbeat`, firmware versions, ASIC ID, and thermal trip
  count when ARC firmware exposes those attributes.
- Optionally read Tenstorrent `hwmon` sensor inputs with `--collect-hwmon`
  when the driver and environment have been validated for safe sensor reads.
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

`hwmon` inputs and PCIe performance counters are explicit opt-ins through
`--collect-hwmon` and `--collect-pcie-counters`. They are disabled by default
because the current `ttsim` bridge has unsafe or incomplete implementations of
both interfaces. The production physical-hardware manifest enables `hwmon`;
enable either source elsewhere only after validating that environment.

## Build And Test

Host-side build and unit tests are lightweight checks and do not require VM
hardware:

```bash
uv sync --locked
uv run scripts/ci/run_tests.py
uv run ruff check src tests scripts
uv run python -m build --wheel --no-isolation --outdir dist
uv run python scripts/ci/check_docs.py
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

Device profiling requires a profiler-enabled TT-Metalium source build. Verify
that the TTNN/TT-Metalium build used by the workload includes Tracy support
before setting `TT_METAL_DEVICE_PROFILER=1`; a prebuilt wheel without profiler
support will fail during initialization. For source builds, enable Tracy with
the build system's profiler option (for example, `-DENABLE_TRACY=ON`) and
follow the version-specific TT-Metalium build instructions.

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

## License

This project is licensed under the [Apache License 2.0](LICENSE).
