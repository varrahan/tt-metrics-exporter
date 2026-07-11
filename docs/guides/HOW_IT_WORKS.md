# How the Tenstorrent Telemetry System Works

This document explains the components under `src/telemetry`, what each one is
responsible for, and how data moves from a Tenstorrent device or workload to a
Prometheus scrape or JSON response.

## Purpose and design rules

The telemetry system is a node-local observer for Tenstorrent devices. It has
two outputs:

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
- `--metalium-profiler-stale-after`: profiler freshness window; defaults to
  15 seconds.
- `--collect-hwmon`: enables optional hardware-monitor sensor reads.
- `--collect-pcie-counters`: enables optional PCIe counter reads.
- `--listen-address`, `--port`, and `--poll-interval`: server controls.
- `--max-snapshot-age`: readiness freshness bound; it must be greater than the
  polling interval.
- `--require-device`: require at least one discovered device for readiness.
- `--shutdown-grace-period`: hard upper bound for graceful termination.
- `--http-workers` and `--http-queue-depth`: bound accepted connections;
  defaults allow four active requests and 64 queued connections.
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

- A missing sysfs root produces an empty device list.
- A missing optional file leaves its field absent.
- Permission errors on individual files or directories do not crash the
  exporter.
- Invalid numeric values are ignored.

This keeps the node exporter available even when a driver exposes only a
partial telemetry surface.

The configured sysfs root is the sole critical collection source. DRA,
janitor, and profiler roots are optional when omitted; once configured, root
unreadability is an operational error. An absent per-device optional file is
still normal missing data. The complete endpoint and source semantics are in
the [operational contract](../info/OPERATIONAL_CONTRACT.md).

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
[`STATE_INGESTION_SECURITY.md`](../info/STATE_INGESTION_SECURITY.md).

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
cardinality review follow [`METRIC_COMPATIBILITY.md`](../info/METRIC_COMPATIBILITY.md).

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
[`telemetry.tenstorrent.com/v1` schema](../schema/telemetry.tenstorrent.com-v1.schema.json).

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
- Any other path: `404 Not Found`.

The server has a bounded accepted-connection limit and initial-request
deadline. Contract middleware enforces an 8 KiB header limit, accepts exact
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

- `tests/unit/test_collector.py`
- `tests/unit/test_renderers.py`
- `tests/unit/test_runtime.py`
- `tests/unit/test_profiler_publisher.py`
- `tests/integration/`

Pytest tests create temporary sysfs and state trees. They verify parsers, collection,
missing-root behavior, both output formats, state ingestion, runtime status,
HTTP behavior, lifecycle, logging, staleness, and invalid profiler records.

The Python test verifies atomic publication, inactive cleanup, empty reads,
runtime-chip-to-exporter-device mapping, impossible core-count rejection, and
required profiler environment validation.

Run all configured tests with:

```bash
uv sync --locked
uv run scripts/ci/run_tests.py
uv run ruff check src tests scripts
uv run python scripts/ci/check_docs.py
```

## 14. Runtime and simulator boundary

The Python sysfs exporter can be validated in the QEMU guest after the official
TTSim PCI bridge enumerates the Wormhole device and compatible `tt-kmd` binds
to it.

The current official QEMU bridge is not a complete TTNN execution path. TTNN
topology discovery reaches a simulator register that is not implemented. As a
result:

- Use PCI enumeration, BAR layout, KMD binding, sysfs, and
  `/dev/tenstorrent/0` to validate the QEMU bridge.
- Use unit or contract tests to validate profiler-state ingestion in that VM.
- Use compatible physical hardware for real TT-Metalium profiler samples.

This distinction is important: an empty workload-profiler metric family in the
current QEMU VM does not mean the exporter failed. It means no compatible
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
