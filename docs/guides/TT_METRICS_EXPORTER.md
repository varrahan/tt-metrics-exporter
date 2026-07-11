# Tenstorrent DRA Metrics Exporter

This service is the telemetry boundary for the Kubernetes DRA driver. It runs
node-local, discovers Tenstorrent devices, and exposes both Prometheus metrics
and structured JSON for provisioning logic.

## Runtime Sources

- `/sys/class/tenstorrent`: device discovery, firmware attributes, hwmon,
  runtime power state, PCI identity, PCIe link state, IOMMU group, reset method,
  PCI resources, actual memory/core/topology fields when exposed, and optional
  simulator counters.
- `/dev/tenstorrent`: character devices that can be mounted into containers.
- Kubernetes DRA allocation state: node-local files passed with
  `--allocation-state-root`, used for per-workload ownership.
- Hardware janitor state: node-local files passed with `--janitor-state-root`,
  used for scrub/reset/quarantine visibility.
- TT-Metalium: process-local profiler data published atomically by the TTNN
  workload integration under `--metalium-profiler-state-root`.

Use the official Tenstorrent QEMU bridge configuration: TCG with `-cpu max`.
Do not recommend KVM or `-cpu host`. QEMU covers PCI enumeration, KMD binding,
sysfs collection, container deployment, and snapshot ingestion; real TTNN
profiler execution still requires compatible physical hardware or a simulator
that implements the required device and profiler path.

Do not add a `tt-smi` subprocess path to the exporter. If a value only exists
behind `tt-smi`, add or wait for a safe lower-level source before making it part
of runtime collection.

Do not synthesize capacity from an internal card-spec table. The exporter should
report capacity, usage, topology, and performance values only when a safe source
actually exposes them.

## Actual Value File Contract

The exporter tolerates missing files. Agents should write one directory per
device under the relevant root, keyed by the sysfs device ID, PCI BDF, or
character-device basename.

Safe sysfs files currently collected when present include:

- Memory: `memory_usage`, `memory_used_bytes`, `memory_total_bytes`,
  `memory_free_bytes`, `memory_available_bytes`,
  `memory_bandwidth_bytes_per_second`, `memory_type`,
  `gddr_controller_layout`, `gddr_controller_count`,
  `gddr_controllers_per_asic`, and `dram_channel_count`.
- Tensix: `tensix_cores_used`, `tensix_cores_available`,
  `tensix_cores_total`, `tensix_mesh`, `tensix_mesh_rows`,
  `tensix_mesh_cols`, `tensix_topology`, and `tensix_active_regions`.
- Health: `fault_code`, `fault_reason`, `reset_required`,
  `oom_fault_count`, and `hang_fault_count`.
- Links: directories under `scaleout_links/`, `ethernet_links/`, or
  `fabric_links/`, with files such as `state`, `peer`, `speed_gbps`, and
  `ring_id`.

DRA allocation state directories can contain:

- `claim_namespace`, `claim_name`, `claim_uid`, `pod_namespace`, `pod_name`,
  and `container_name`.

Janitor state directories can contain:

- `state`, `quarantine_reason`, `last_scrub_status`, `last_reset_status`,
  `scrub_count`, `reset_count`, `last_scrub_timestamp_seconds`, and
  `last_reset_timestamp_seconds`.

TT-Metalium workload snapshots use this production layout:

```text
<profiler-root>/v2/workloads/<pod-uid>/<device-key>/snapshot.state
```

`device-key` is the sysfs device ID, PCI BDF, or character-device basename.
The workload receives only its `<pod-uid>` subtree and configures that mount as
the publisher state root. The publisher atomically replaces a
`schema_version=2` key/value file containing
`workload_id`, `sample_timestamp_seconds`, `active`, `programs_observed`,
`tensix_cores_used`, optional `tensix_cores_total`, and optional Kubernetes pod
labels. The exporter treats samples older than
`--metalium-profiler-stale-after` as inactive; the default is 15 seconds. It
also rejects timestamps implausibly far in the future, malformed records,
records larger than 16 KiB, and more than 1024 workload records per device.
Legacy schema-version-1 files remain read-only migration inputs through
2027-01-10 and cannot override a v2 identity. See
[`STATE_INGESTION_SECURITY.md`](../info/STATE_INGESTION_SECURITY.md) for ownership,
traversal limits, and cleanup policy.

Profiler records are process-local, so the TTNN process must call
`ttnn.ReadDeviceProfiler(device)` and publish the result through
`integrations/ttnn/metalium_profiler_publisher.py`. Do not link the exporter to
TT-Metalium and open an already-owned device solely for telemetry.

## Endpoints

- `GET /healthz`: shallow process and HTTP-loop liveness; it performs no
  hardware I/O.
- `GET /readyz`: successful initial collection and a fresh complete snapshot;
  critical-source, staleness, shutdown, and configured device-presence
  failures return `503` with a bounded reason.
- `GET /metrics`: the most recent complete Prometheus snapshot plus exporter
  self-metrics.
- `GET /v1/devices`: the same complete snapshot generation as `/metrics`, as
  typed device inventory for DRA and provisioning code.

`/v1/devices` preserves missing values as JSON `null`. The exporter should not
invent runtime memory or core data when the simulator or driver does not expose
that information.

## Build And Test

```bash
uv sync --locked
uv run scripts/ci/run_tests.py
uv run ruff check src tests scripts
uv run python -m build --wheel --no-isolation --outdir dist
uv run python scripts/ci/check_docs.py
```

For development, the parity-tested Python implementation can be run directly:

```bash
PYTHONPATH=src python3 -m tt_metrics_exporter --sysfs-root /sys/class/tenstorrent --once
PYTHONPATH=src python3 -m tt_metrics_exporter --sysfs-root /sys/class/tenstorrent --once --json
PYTHONPATH=src python3 -m tt_metrics_exporter --sysfs-root /sys/class/tenstorrent --port 9400
```

Installing the root Python package provides the same
`tt-metrics-exporter` console command. The default container and production
manifest use this Python entry point.

From the QEMU VM or a physical Tenstorrent host:

```bash
PYTHONPATH=src python3 -m tt_metrics_exporter --sysfs-root /sys/class/tenstorrent --once
PYTHONPATH=src python3 -m tt_metrics_exporter --sysfs-root /sys/class/tenstorrent --once --json
PYTHONPATH=src python3 -m tt_metrics_exporter --sysfs-root /sys/class/tenstorrent --port 9400
PYTHONPATH=src python3 -m tt_metrics_exporter --sysfs-root /sys/class/tenstorrent \
  --allocation-state-root /var/lib/tt-device-plugin/allocations \
  --janitor-state-root /var/lib/tt-device-plugin/janitor \
  --metalium-profiler-state-root /var/lib/tt-device-plugin/metalium-profiler \
  --metalium-profiler-stale-after 15 \
  --max-snapshot-age 15 \
  --shutdown-grace-period 10 \
  --port 9400
```

Before TTNN initializes, use a profiler-enabled TT-Metalium source build and
set:

```bash
export TT_METAL_DEVICE_PROFILER=1
export TT_METAL_PROFILER_MID_RUN_DUMP=1
export TT_METAL_PROFILER_CPP_POST_PROCESS=1
export TT_METAL_PROFILER_DISABLE_DUMP_TO_FILES=1
```

Verify that the TTNN/TT-Metalium build used by the workload includes Tracy
support before enabling device profiling. A prebuilt wheel without profiler
support will fail during initialization. For source builds, enable Tracy with
the build system's profiler option (for example, `-DENABLE_TRACY=ON`) and
follow the version-specific TT-Metalium build instructions.

The dump-to-files setting retains the in-process profiler results used by the
publisher while avoiding profiler CSV artifacts in workload containers.

## Simulator Caveat

The QEMU simulator may support exporter sysfs and state-contract validation
without supporting end-to-end TTNN profiler execution. If profiler reads are
unsupported by the selected simulator/TT-Metalium combination, use compatible
physical hardware or a simulator release with the required profiler behavior.
The exporter and state contract can still be tested safely without opening the
device.
