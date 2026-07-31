# Tenstorrent Metrics Exporter

This repository contains the Python Tenstorrent metrics exporter. Its telemetry
source and state-file contracts are described in
[`docs/guides.md`](docs/guides.md).

The exporter is intended to run as a node-local DaemonSet in the QEMU `ttsim`
VM or on physical Tenstorrent hosts. Runtime validation that depends on
`tt-kmd`, `/sys/class/tenstorrent`, `/dev/tenstorrent`, Docker, `kind`, or DRA
APIs must be performed from the VM.

QEMU VM setup and launch guidance is in [`docs/VM.md`](docs/VM.md).
Runtime environment variables used by this program are documented in
[`docs/ENV.md`](docs/ENV.md).

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

### 4b. Prepare the TTSim VM for this repo

Set up the QEMU VM and validate the Tenstorrent bridge path before running
telemetry collection:

```bash
# on host
./tests/vm/scripts/launch_ttsim_qemu.sh
./tests/vm/scripts/verify_ttsim_qemu.sh
```

The launch script exposes SSH on host port `2222` by default, as user `ubuntu`.
Connect and verify the root path:

```bash
ssh -p 2222 ubuntu@127.0.0.1
test -d /sys/class/tenstorrent
```

Inside the repository on the host, copy the VM-only VM test helpers if the guest
doesn’t already have them:

```bash
./tests/vm/scripts/sync_test_vm.sh
```

### 4c. Simulate 32 devices and inject workloads

Run the local workload simulator in the guest to generate synthetic hardware and
TT-Metalium workload state for validation:

```bash
cd /home/ubuntu/tt-telemetry
python3 tests/vm/ttsim_fake_hardware.py \
  --sysfs-root /tmp/tt-sim-sysfs \
  --state-root /tmp/tt-sim-state \
  --device-count 32 \
  --interval 1 \
  --iterations 0 \
  --simulate-workloads
```

This starts an indefinite loop so leave it running in a terminal and open a second
terminal for the exporter. Then start collection using the injected roots:

```bash
cd /home/ubuntu/tt-telemetry
uv run tt-metrics-exporter \
  --sysfs-root /tmp/tt-sim-sysfs/class/tenstorrent \
  --metalium-profiler-state-root /tmp/tt-sim-state \
  --collect-hwmon \
  --listen-address 127.0.0.1 \
  --port 9400 \
  --poll-interval 1 \
  --max-snapshot-age 10 \
  --log-level info
```

Validate the injected paths are being rendered:

```bash
curl --fail --silent --show-error http://127.0.0.1:9400/healthz
curl --fail --silent --show-error http://127.0.0.1:9400/readyz
curl --fail --silent --show-error 'http://127.0.0.1:9400/metrics' | grep '^tt_devices_discovered'
curl --fail --silent --show-error http://127.0.0.1:9400/v1/devices | uv run python -m json.tool
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
[operational contract](docs/info.md) for the exact
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
[`docs/info.md`](docs/info.md).

Current data collection scope:

Discovery:

- Scan `/sys/class/tenstorrent` and create one device record per entry.
- Read Linux character-device identity from `<device>/dev` and `<device>/uevent`, exposing `major`, `minor`, `DEVNAME`, and derived `/dev/<name>`.

PCI/device identity and topology:

- Read PCI and bridge identity from `<device>/device` and its siblings, including `bdf` (`PCI_SLOT_NAME`), `driver`, `vendor_id`, `device_id`, `class_id`, `revision`, `subsystem_vendor_id`, `subsystem_device_id`, `numa_node`, `iommu_group`, `current_link_speed`, `current_link_width`, `max_link_speed`, `max_link_width`, and `reset_method`.
- Read PCI BAR/resource layout from `<device>/device/resource` using `start/end/flags` ranges, ignoring empty/invalid ranges.

Safe sysfs fields collected when present:

- Identity/static descriptors from sysfs aliases: `architecture`, `arch`, `chip_arch`, `chip_architecture`, `device_arch`, `chip`, `board_type`, `board`, `card_type`, `card_series`, `product_name`, `health`, `status`, and `device_status`.
- Firmware telemetry: `tt_aiclk`, `tt_axiclk`, `tt_arcclk`, `tt_heartbeat`, `tt_therm_trip_count`, `tt_serial`, `tt_card_type`, `tt_asic_id`, `tt_fw_bundle_ver`, `tt_m3app_fw_ver`, `tt_m3bl_fw_ver`, `tt_arc_fw_ver`, `tt_eth_fw_ver`, and `tt_ttflash_ver`.
- Runtime power fields from `power/`: `runtime_status`, `control`, `runtime_enabled`, `runtime_active_time`, `runtime_suspended_time`, `runtime_usage`, `runtime_active_kids`, `autosuspend_delay_ms`.
- Memory fields from sysfs: `memory_usage`, `dram_usage`, `tt_memory_usage`, `memory_used_bytes`, `dram_used_bytes`, `allocated_memory_bytes`, `memory_total_bytes`, `memory_capacity_bytes`, `memory_size_bytes`, `dram_total_bytes`, `dram_capacity_bytes`, `dram_size_bytes`, `memory_free_bytes`, `dram_free_bytes`, `memory_available_bytes`, `dram_available_bytes`, `memory_bandwidth_bytes_per_second`, `dram_bandwidth_bytes_per_second`, `gddr_bandwidth_bytes_per_second`, `memory_type`, `dram_type`, `gddr_type`, `gddr_controller_layout`, `dram_controller_layout`, `memory_controller_layout`, `gddr_controller_count`, `dram_controller_count`, `memory_controller_count`, `gddr_controllers_per_asic`, `dram_controllers_per_asic`, `memory_controllers_per_asic`, `dram_channel_count`, `gddr_channel_count`, `memory_channel_count`.
- Tensix/core fields from sysfs aliases: `tensix_cores_used`, `tensix_used`, `active_tensix_cores`, `tensix_cores_available`, `tensix_available`, `available_tensix_cores`, `tensix_cores_total`, `total_tensix_cores`, `tensix_total`, `tensix_core_count`, `tensix_mesh_rows`, `tensix_grid_rows`, `tensix_rows`, `core_grid_rows`, `tensix_mesh_cols`, `tensix_grid_cols`, `tensix_cols`, `core_grid_cols`, `tensix_mesh`, `tensix_grid`, `core_grid`, `worker_grid`, `tensix_topology`, `tensix_layout`, `tensix_active_regions`, `active_core_ranges`, `active_core_grids`.
- Health detail counters from sysfs aliases: `fault_code`, `device_fault_code`, `last_fault_code`, `fault_reason`, `device_fault_reason`, `last_fault_reason`, `reset_reason`, `reset_required`, `needs_reset`, `requires_reset`, `oom_fault_count`, `oom_count`, `out_of_memory_count`, `hang_fault_count`, `hang_count`, and `device_hang_count`.
- Interconnect/link state from directories `scaleout_links/`, `ethernet_links/`, `fabric_links/` using fields `type`, `link_type`, `state`, `status`, `peer`, `remote`, `remote_device`, `remote_bdf`, `speed_gbps`, `link_speed_gbps`, `rate_gbps`, `ring_id`, and `ring`.

Optional sources (disabled unless configured):

- `--collect-hwmon`: reads safe hwmon files from `hwmon/*` and `device/hwmon/*` input files and emits normalized sensor value records.
- `--collect-pcie-counters`: reads safe counters under `pcie_perf_counters/`.
- `--allocation-state-root`: reads node-local DRA-style ownership metadata: `claim_namespace`, `claim_name`, `claim_uid`, `pod_namespace`, `pod_name`, `container_name`.
- `--janitor-state-root`: reads node-local health/ops metadata: `state`, `quarantine_reason`, `last_scrub_status`, `last_reset_status`, `scrub_count`, `reset_count`, `last_scrub_timestamp_seconds`, and `last_reset_timestamp_seconds`.
- `--metalium-profiler-state-root`: reads TT-Metalium profiler snapshots from `v2/workloads/<pod-uid>/<device-key>/snapshot.state` (legacy `*.state` files are also supported during migration). Parsed fields include `workload_id`, `pod_namespace`, `pod_name`, `container_name`, `active`, `programs_observed`, `tensix_cores_used`, `tensix_cores_total`, and `sample_timestamp_seconds`.

Derived outputs:

- Polling loop renders Prometheus metrics via `/metrics` on port `9400` and structured JSON snapshots via `/v1/devices`.
- Exporter liveness/readiness endpoints are `/healthz` and `/readyz`, and both are local HTTP reporting endpoints.

The workload-side TTNN publisher lives at
`integrations/ttnn/metalium_profiler_publisher.py`. Kubernetes DaemonSet,
monitoring, and environment overlays live under `deploy/kubernetes`.

The exporter does not run `tt-smi` and should not grow a `tt-smi` subprocess
path. If a value only exists through `tt-smi`, treat that as a missing safe
source and add a lower-level sysfs, Kubernetes, or TT-Metalium/profiler source
before depending on it.

The exporter does not synthesize static capacity from a card-type table.
Capacity, memory, core, and bandwidth values must come from safe driver,
firmware, sysfs, or TT-Metalium profiler data.

The stable liveness, readiness, snapshot, and critical-source behavior is
defined in [`docs/info.md`](docs/info.md).

The current QEMU `ttsim` VM may not expose firmware telemetry attributes. In
that case the exporter still reports device, PCI, BAR, and power-management
metadata, while firmware, hwmon, live memory, and Tensix-utilization metrics
remain empty.

`hwmon` inputs and PCIe performance counters are explicit opt-ins through
`--collect-hwmon` and `--collect-pcie-counters`. They are disabled by default
because the current `ttsim` bridge has unsafe or incomplete implementations of
both interfaces. The production physical-hardware manifest enables `hwmon`;
enable either source elsewhere only after validating that environment.

## Environment variables

The exporter has no required environment variables for normal daemon operation when
using CLI flags.

Use these variables when running and validating this program:

- Required for TT-Metalium profiling workloads:
  - `TT_METAL_DEVICE_PROFILER=1`
  - `TT_METAL_PROFILER_MID_RUN_DUMP=1`
  - `TT_METAL_PROFILER_CPP_POST_PROCESS=1`
- Optional TT-Metalium controls:
  - `TT_METAL_PROFILER_DISABLE_DUMP_TO_FILES=1`
  - `TT_METALIUM_PROFILER_STATE_ROOT` (defaults to
    `/var/lib/tt-device-plugin/metalium-profiler` unless overridden by script arg)
- Optional workload identity metadata:
  - `TT_WORKLOAD_ID`
  - `POD_UID`
  - `POD_NAME`
  - `POD_NAMESPACE`
  - `CONTAINER_NAME`
- Build metadata (read by `--version` output):
  - `TT_EXPORTER_REVISION`
  - `TT_EXPORTER_BUILD_TIME`

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
[`docs/info.md`](docs/info.md).

Then scrape:

```bash
curl http://127.0.0.1:9400/metrics
curl http://127.0.0.1:9400/v1/devices
```

## Repository structure quick reference

### Tests

The test tree is split by scope:

- `tests/unit/` has deterministic contracts for parsing, collection, rendering, HTTP/runtime behavior, and publisher state handling.
- `tests/integration/` exercises the packaged service through subprocesses, sockets, signals, HTTP, and lifecycle behavior.
- `tests/fixtures/` contains bounded regression inputs.
- `tests/support/` contains helper utilities and is not collected by `pytest`.

Run the full suite with:

```bash
uv run scripts/ci/run_tests.py
```

For a faster loop:

```bash
uv run pytest tests/unit
```

### Scripts

Repository scripts are not shipped with the exporter package and are used for
repo-level validation and operations.

- `scripts/ci/` contains host checks used by CI and container validation.
- `scripts/validation/` validates images, manifests, monitoring, rollouts, NetworkPolicy, soak behavior, and VM qualification.
- `scripts/operations/` contains opt-in workload and soak tooling for a running exporter.

`scripts/validation/vm.sh` is the consolidated TTSim entry path. Its default
five-minute soak is smoke qualification; `SOAK_DURATION_SECONDS=259200` enables
the 72-hour physical-hardware release gate.

### TT-Metalium profiler package

## License

This project is licensed under the [Apache License 2.0](LICENSE).
