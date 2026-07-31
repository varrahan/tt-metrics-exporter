# Telemetry Guides

## How the Tenstorrent Telemetry System Works

This document explains the repository's components, what each one is
responsible for, and how data moves from a Tenstorrent device or workload to a
Prometheus scrape or JSON response.

## Purpose and design rules

The telemetry system is a node-local observer for Tenstorrent devices on Linux
kernels using `sysfs`; it is distribution-agnostic beyond that.
It has two outputs:

- Prometheus exposition text for monitoring and alerting.
- Structured JSON for inventory, debugging, and DRA-related consumers.

The production node exporter is implemented in Python. Starlette supplies the
ASGI routes, Uvicorn supplies HTTP parsing and service lifecycle, and Click
owns command-line validation. Logging uses the Python standard library. Python
is also used independently by the workload-side TTNN adapter.

The implementation follows four important rules:

1. Read hardware state from safe node-local sources such as `tt-kmd` sysfs,
   backing PCI sysfs, and `hwmon`.
2. Do not run `tt-smi` as a collection subprocess.
3. Do not invent values from SKU tables. If a source does not report a value,
   keep it absent.
4. Read TT-Metalium profiler results inside the workload process that owns
   them, then pass a small language-neutral snapshot to the node exporter.

## System at a glance

```text
tt-kmd and PCI sysfs -----------+
hwmon and power sysfs ----------+
optional DRA allocation state --+
optional janitor state ---------+--> SysfsCollector --> DeviceTelemetry
                                |                           |
TTNN workload                   |                           +--> Prometheus renderer
  -> profiler publisher         |                           |
  -> atomic workload state -----+                           +--> JSON renderer
                                                                    |
                                      polling cache <---------------+
                                            |
                                      Starlette / Uvicorn
                                            |
               /metrics   /v1/devices   /healthz   /readyz
```

The exporter does not open a Tenstorrent device through TT-Metalium. It
observes sysfs and state files, which avoids conflicting with the workload that
owns the device.

The source package is organized by dependency direction:

```text
tt_metrics_exporter/
├── app/          CLI, lifecycle, HTTP, logging, readiness, and snapshots
├── collection/   sysfs/state collection, secure I/O, and input parsers
├── renderers/    Prometheus and JSON output adapters
├── models.py     shared telemetry and diagnostic contracts
└── __init__.py   small public library facade
```

`app` coordinates the other layers. Collection and rendering share only the
domain models and do not depend on application lifecycle code.

## 1. Application entry point and polling loop

Files:

- `src/tt_metrics_exporter/app/cli.py`

The `tt-metrics-exporter` console entry point begins in `cli.py`. It performs the
following work:

1. Parses command-line options.
2. Builds a `CollectorConfig` and creates a `SysfsCollector`.
3. Performs an initial collection.
4. Renders both representations and atomically publishes one immutable
   generation containing the collection summary, Prometheus, and JSON output.
5. Starts a background polling thread.
6. Starts the HTTP server in the main thread.
7. Handles `SIGINT` and `SIGTERM` for an orderly shutdown.

The default polling interval is five seconds. Collection and rendering happen
before publication. One immutable shared snapshot is atomically exchanged, so
Prometheus and JSON always describe the same complete old or new generation.
A failed refresh retains the previous complete payload while runtime failure
counters and readiness continue to update.

The executable also supports a one-shot mode:

```bash
tt-metrics-exporter --once
tt-metrics-exporter --once --json
```

One-shot mode collects directly, writes the selected representation to
standard output, and does not start the poller or HTTP server.

Important options include:

- `--sysfs-root`: Tenstorrent class root; defaults to
  `/sys/class/tenstorrent`.
- `--allocation-state-root`: optional DRA allocation state.
- `--janitor-state-root`: optional hardware janitor state.
- `--metalium-profiler-state-root`: optional workload profiler state.
- There is no dedicated `--dra-agnostic` flag. The exporter runs without DRA
  integration when these roots are omitted.
- `--metalium-profiler-stale-after`: profiler freshness window; defaults to
  15 seconds.
- `--collect-hwmon`: enables optional hardware-monitor sensor reads.
- `--collect-pcie-counters`: enables optional PCIe counter reads.
- `--listen-address`, `--port`, and `--poll-interval`: server controls.
- `--max-snapshot-age`: readiness freshness bound; it must be greater than the
  polling interval.
- `--require-device`: require at least one discovered device for readiness.
- `--shutdown-grace-period`: hard upper bound for graceful termination.
- `--http-workers` and `--http-queue-depth`: define the accepted-connection
  cap; the default cap is their sum, 68 connections.
- `--http-request-deadline`: initial-request deadline; defaults to two seconds.
- `--maximum-rendered-payload-bytes`: per-representation publication bound.

`SIGINT` and `SIGTERM` begin coordinated shutdown. Shutdown marks readiness
false, closes the listener and active client sockets, stops the poller, and
joins all service threads within the configured grace period.

## 2. Shared telemetry data model

Files:

- `src/tt_metrics_exporter/models.py`

`models.py` defines the typed model shared by collection and rendering.
`DeviceTelemetry` is the top-level record for one discovered device. It groups:

- Character-device identity.
- PCI identity, BDF, driver, NUMA and IOMMU information, link state, reset
  method, and BAR resources.
- Runtime power-management state.
- Firmware identity, clocks, heartbeat, serial number, and versions.
- `hwmon` sensors.
- Memory usage, capacity, type, bandwidth, and controller topology.
- Tensix core counts, mesh information, active regions, and source.
- TT-Metalium workload samples.
- Fault, OOM, hang, and reset-required state.
- Scale-out or fabric links.
- Kubernetes allocation ownership.
- Hardware janitor state.

Most fields use `None` for absence, meaning no safe source reported the value.
Renderers preserve that distinction rather than filling in a guessed zero.

`CollectorConfig` carries all input roots and feature switches into the
collector.

## 3. Sysfs and node-state collector

Files:

- `src/tt_metrics_exporter/collection/collector.py`
- `src/tt_metrics_exporter/collection/device_resources.py`
- `src/tt_metrics_exporter/collection/sysfs_io.py`
- `src/tt_metrics_exporter/collection/state.py`
- `src/tt_metrics_exporter/collection/secure_io.py`

`SysfsCollector.collect()` returns a `CollectionResult` containing devices,
bounded per-source diagnostics, and critical-source status. It iterates the
directories below the configured sysfs root; each directory becomes one
`DeviceTelemetry` record.

For every device, the collector attempts to read the following sources.

### Character device

The collector reads `dev` and `uevent` to obtain the major number, minor
number, and device name such as `tenstorrent/0`.

### PCI device

The backing `device/` directory supplies:

- PCI BDF and bound driver.
- Vendor, device, class, revision, and subsystem IDs.
- NUMA node and IOMMU group.
- Current and maximum PCIe link speed and width.
- Reset method.
- Non-empty PCI BAR start, end, flags, and calculated size.

When architecture is missing from the Tenstorrent class directory, the
collector can identify a known architecture from the reported PCI device ID.
It does not infer memory or core capacity from that identity.

### Firmware and sensors

Firmware attributes include card type, ASIC ID, serial number, clocks,
heartbeat, thermal-trip count, and firmware versions when `tt-kmd` exposes
them. A firmware card type takes precedence over a less authoritative generic
board-type file.

When `--collect-hwmon` is enabled, the `hwmon` collector scans input files,
associates labels when present, and assigns units based on the sensor family.
It is disabled by default because simulator-backed sensor reads may have side
effects. The physical-hardware production manifest enables it after platform
qualification. Missing or unreadable sensors are skipped.

### Power and optional PCIe counters

Linux runtime power files provide status, control mode, active and suspended
time, usage count, active-child count, and autosuspend delay.

PCIe performance counters are disabled by default. They are collected only
when `--collect-pcie-counters` is present because some simulator-backed files
can have side effects or incomplete support.

### Memory, Tensix, health, and links

The collector recognizes a set of safe files for actual memory values, Tensix
counts and topology, fault state, and interconnect-link state. It tolerates
files being absent because the exact data exposed depends on `tt-kmd`, firmware,
hardware generation, and simulator fidelity.

### Failure behavior

Collection is deliberately tolerant:

- A missing sysfs root produces no devices and marks the collection
  unsuccessful; one-shot mode exits nonzero and service readiness stays false.
- A missing optional file leaves its field absent.
- Permission errors on individual files or directories do not crash the
  exporter.
- Invalid numeric values are ignored.

This keeps the HTTP process available when a driver exposes only a partial
telemetry surface while preserving failure semantics for the critical root.

The configured sysfs root is the sole critical collection source. DRA,
janitor, and profiler roots are optional when omitted; once configured, root
unreadability is an operational error. An absent per-device optional file is
still normal missing data. The complete endpoint and source semantics are in
the [operational contract](info.md).

## 4. DRA allocation-state reader

Files:

- `src/tt_metrics_exporter/collection/state.py`

The exporter can enrich a device with Kubernetes ownership from a node-local
state directory. It searches for a directory keyed by the device's sysfs ID,
PCI BDF, or character-device basename.

Recognized files are:

- `claim_namespace`
- `claim_name`
- `claim_uid`
- `pod_namespace`
- `pod_name`
- `container_name`

The DRA driver or another trusted node-local component is responsible for
writing these files. The telemetry exporter only reads them.

## 5. Hardware janitor-state reader

Files:

- `src/tt_metrics_exporter/collection/state.py`

The janitor reader uses the same device-key matching approach as the allocation
reader. It collects:

- Current janitor state and quarantine reason.
- Last scrub and reset status.
- Scrub and reset counts.
- Last scrub and reset timestamps.

This separates hardware lifecycle actions from telemetry: the janitor performs
reset or sanitization work, while the exporter reports the resulting state.

## 6. TTNN profiler publisher

Files:

- `integrations/ttnn/metalium_profiler_publisher.py`

TT-Metalium profiler results are process-local. A standalone node exporter
cannot retrieve the latest program records from a different TTNN process.
`MetaliumProfilerPublisher` therefore runs inside the instrumented workload.

A typical workload integration is:

```python
publisher = MetaliumProfilerPublisher(workload_id="default/model-worker-0")

# Execute TTNN operations.
ttnn.synchronize_device(device)
publisher.sample(device)
```

`sample()` performs these steps:

1. Verifies that the required profiler environment variables were set before
   TTNN initialization.
2. Calls `ttnn.ReadDeviceProfiler(device)`.
3. Reads `ttnn.get_latest_programs_perf_data()`.
4. Groups records by runtime chip ID.
5. Reports the maximum `ProgramAnalysisData.core_count` in the read, the
   maximum `num_available_cores`, and the number of observed programs.
6. Maps the runtime chip ID to a sysfs ID, PCI BDF, or character-device name
   when a mapping is configured.
7. Atomically publishes the summarized state.

The required environment variables are:

```bash
export TT_METAL_DEVICE_PROFILER=1
export TT_METAL_PROFILER_MID_RUN_DUMP=1
export TT_METAL_PROFILER_CPP_POST_PROCESS=1
```

The publisher can obtain workload identity from explicit arguments or the
`TT_WORKLOAD_ID`, `POD_UID`, `POD_NAME`, `POD_NAMESPACE`, and
`CONTAINER_NAME` environment variables.

### Atomic state publication

The node-visible trusted root is:

```text
/var/lib/tt-device-plugin/metalium-profiler
```

The production layout is:

```text
<state-root>/v2/workloads/<pod-uid>/<device-key>/snapshot.state
```

The trusted node service mounts only `<pod-uid>` into its workload and the
publisher treats that scoped mount as its `state_root`. The publisher writes a
temporary file, flushes and `fsync`s it, changes it to
mode `0644`, atomically renames it over the target, and `fsync`s the directory.
This prevents the exporter from reading a partially written sample.

On a normal process exit, `close()` writes a final inactive sample with zero
cores used. If the process is killed and cannot run cleanup, the exporter's
freshness window eventually marks the last sample stale and inactive.

Publication is best-effort by default: failures warn once and update bounded
failure/last-success state without terminating the model. `strict=True` is
available for tests and development. The full trust and cleanup contract is in
[`STATE_INGESTION_SECURITY.md`](info.md).

### What the profiler signal means

The signal is recent spatial core occupancy. For example, 24 used cores out of
80 available cores produces an occupancy ratio of `0.3`.

It is not a time-weighted hardware-busy percentage. It does not mean the ASIC
was executing 30 percent of the wall-clock interval.

## 7. Dynamic workload example

Files:

- `integrations/ttnn/example_dynamic_workload.py`

This file is an integration example and validation utility, not a node daemon.
It:

1. Validates profiler configuration before importing TTNN.
2. Opens a TTNN device.
3. Creates a small tiled tensor.
4. Repeatedly performs an addition.
5. Synchronizes the device after each iteration.
6. Publishes and prints the resulting profiler summary.
7. Marks the workload inactive when the context manager exits.

Its command-line options control the runtime device ID, exporter device key,
state root, workload ID, iteration count, and interval.

## 8. Workload-state ingestion and aggregation

Files:

- `src/tt_metrics_exporter/collection/state.py`

The collector searches the configured profiler root using the sysfs device ID,
PCI BDF, and character-device basename. It reads current v2 workload snapshots
and legacy schema-version-1 migration inputs and validates required fields.

Defensive limits include:

- Maximum state-file size of 16 KiB.
- Maximum of 1024 workload records per device.
- Strict non-negative numeric parsing.
- `active` must be zero or one.
- Used cores cannot exceed total cores.
- Samples too old or implausibly far in the future are stale.

Fresh active workload core counts are summed and capped at the reported total.
Profiler-derived used, total, and available counts populate the generic Tensix
fields only when a more direct sysfs source did not already provide them. The
generic Tensix source is then identified as `metalium_profiler`.

## 9. Prometheus renderer

Files:

- `src/tt_metrics_exporter/renderers/prometheus.py`

`render_prometheus()` converts a complete `DeviceTelemetry` snapshot to
Prometheus text format. It emits `HELP` and `TYPE` metadata and escapes label
values before writing them.

Metric families cover:

- Device discovery and identity.
- Firmware identity, clocks, heartbeat, and thermal trips.
- `hwmon` sensors.
- PCI resources, link state, and optional performance counters.
- Runtime power management.
- Memory capacity, usage, bandwidth, type, and controllers.
- Tensix core counts and topology.
- Per-workload Metalium activity, staleness, cores, occupancy ratio, programs,
  and sample timestamp.
- Health faults, reset requirement, OOMs, and hangs.
- Interconnect-link identity and speed.
- DRA allocation ownership.
- Janitor status, counters, and timestamps.

Metrics that depend on an absent value are omitted. Information metrics use
labels and a constant value of `1`.

Metric names, labels, types, units, deprecation, occupancy semantics, and
cardinality review follow [`METRIC_COMPATIBILITY.md`](info.md).

## 10. JSON renderer

Files:

- `src/tt_metrics_exporter/renderers/json.py`

`render_devices_json()` emits a versioned document:

```json
{
  "apiVersion": "telemetry.tenstorrent.com/v1",
  "kind": "DeviceList",
  "summary": {"devicesDiscovered": 1},
  "devices": []
}
```

Each device contains structured sections corresponding to the shared Python
data model. Optional values remain JSON `null` where appropriate, allowing a
consumer to distinguish an unavailable measurement from an actual zero.
Rendered fixtures are validated against the machine-readable
[`telemetry.tenstorrent.com/v1` schema](schema/telemetry.tenstorrent.com-v1.schema.json).

## 11. Embedded HTTP server

Files:

- `src/tt_metrics_exporter/app/http.py`
- `src/tt_metrics_exporter/app/runtime.py`

The exporter runs a small Starlette ASGI application under Uvicorn. A narrow
protocol adapter preserves the exporter's connection cap, initial-request
deadline, header limit, and connection counters. Handlers serve cached strings
supplied by callbacks from `app/cli.py`.

Endpoints are:

- `GET /metrics`: Prometheus exposition format.
- `GET /v1/devices`: structured JSON inventory.
- `GET /healthz`: process liveness response, `ok`.
- `GET /readyz`: snapshot readiness and a bounded reason.
- Any other path: `404 Not Found`.

The server caps accepted connections at `--http-workers` plus
`--http-queue-depth` and enforces an initial-request deadline. Contract
middleware enforces an 8 KiB header limit, accepts exact
`GET` routes, ignores a query string for routing, rejects request bodies, and
returns bounded `400`, `404`, `405`, or `431` responses. All responses include `Content-Length`,
`Content-Type`, `Connection: close`, and `X-Content-Type-Options: nosniff`.

Handlers only copy immutable cached payloads; they never collect sysfs.
Connection saturation returns `503`, closes the excess connection, and
increments a rejection counter.
TLS and authentication remain outside the application trust boundary and must
be supplied by Kubernetes NetworkPolicy or an authenticated TLS proxy for
non-cluster exposure.

### Structured logging

Operational logs are written to standard error. `--log-format text|json` and
`--log-level error|warn|info|debug` select the representation and threshold.
JSON records always contain `timestamp`, `severity`, `event`, and `message`.
Source/reason warnings use bounded values and a 60-second rate limit; state
contents, workload identifiers, raw paths, and exception strings are never
logged. Successful scrape details are metrics, while successful collection
details appear only at `debug`.

## 12. Build and installation

Files:

- `pyproject.toml`
- `Dockerfile`

The Python package builds a pure-Python wheel and defines the
`tt-metrics-exporter` console entry point. `uv.lock` locks development and
runtime resolution; hashed requirements exports feed the container build. The
multi-stage image runs pytest and Ruff, builds the wheel, and installs only
runtime dependencies into a pinned distroless Python image.

## 13. Tests

Files:

- `tests/unit/`
- `tests/integration/`
- `tests/fixtures/`

Pytest tests create temporary sysfs and state trees. They verify parsers, collection,
missing-root behavior, both output formats, state ingestion, runtime status,
HTTP behavior, lifecycle, logging, staleness, and invalid profiler records.

Publisher tests verify atomic publication, inactive cleanup, empty reads,
runtime-chip-to-exporter-device mapping, impossible core-count rejection, and
required profiler environment validation.

Setup and test commands are maintained in the repository
[README](../README.md#build-and-test).

## 14. Runtime and simulator boundary

The Python sysfs exporter can be validated in the QEMU guest after the official
TTSim PCI bridge enumerates the Wormhole device and compatible `tt-kmd` binds
to it.

The qualified QEMU bridge is not a complete TTNN execution path. TTNN
topology discovery reaches a simulator register that is not implemented. As a
result:

- Use PCI enumeration, BAR layout, KMD binding, sysfs, and
  `/dev/tenstorrent/0` to validate the QEMU bridge.
- Use unit or contract tests to validate profiler-state ingestion in that VM.
- Use compatible physical hardware for real TT-Metalium profiler samples.

This distinction is important: an empty workload-profiler metric family in the
QEMU VM does not mean the exporter failed. It means no compatible
TTNN workload produced a profiler snapshot.

## 15. End-to-end lifecycle

For a physical or otherwise compatible Tenstorrent node, the complete lifecycle
is:

1. `tt-kmd` exposes device and PCI state.
2. The exporter discovers the device and collects available telemetry.
3. The DRA driver and janitor optionally publish ownership and lifecycle state.
4. A TTNN workload executes operations and periodically invokes the profiler
   publisher after synchronization.
5. The publisher atomically updates its workload state file.
6. The next exporter poll reads and validates all sources.
7. The exporter renders a new Prometheus and JSON snapshot.
8. Prometheus or another client retrieves the cached data over HTTP.
9. When the workload exits, the publisher marks it inactive; after an abnormal
   exit, the freshness window performs the same safety function.

## Telemetry Sources and State Files

The exporter runs node-local, discovers Tenstorrent devices, and exposes both
Prometheus metrics and structured JSON. This guide records the safe telemetry
sources and node-state file formats. Use the repository [README](../README.md)
for setup, test, container, and deployment commands.

## Runtime Sources

- `/sys/class/tenstorrent`: device discovery, firmware attributes, optional hwmon,
  runtime power state, PCI identity, PCIe link state, IOMMU group, reset method,
  PCI resources, actual memory/core/topology fields when exposed, and optional
  simulator counters.
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

Potentially side-effecting sources are opt-in. Use `--collect-hwmon` only on a
qualified physical platform; it is intentionally absent from the `ttsim`
overlay. PCIe performance counters follow the same policy through
`--collect-pcie-counters`.

## Actual Value File Contract

The exporter tolerates missing files. Allocation and janitor writers create one
directory per device under their configured root, keyed by the sysfs device
ID, PCI BDF, or character-device basename.

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
also treats timestamps implausibly far in the future as stale, rejects
malformed records and records larger than 16 KiB, and exports at most 1024
workload records per device.

Legacy schema-version-1 files remain read-only migration inputs through
2027-01-10 and cannot override a v2 identity. See
[`STATE_INGESTION_SECURITY.md`](info.md) for ownership,
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
  typed device inventory for API consumers.

`/v1/devices` preserves missing values as JSON `null`. The exporter should not
invent runtime memory or core data when the simulator or driver does not expose
that information.

## Simulator Caveat

The QEMU simulator may support exporter sysfs and state-contract validation
without supporting end-to-end TTNN profiler execution. If profiler reads are
unsupported by the selected simulator/TT-Metalium combination, use compatible
physical hardware or a simulator release with the required profiler behavior.
The exporter and state contract can still be tested safely without opening the
device.
