# How the Tenstorrent Telemetry System Works

This document explains the components under `src/telemetry`, what each one is
responsible for, and how data moves from a Tenstorrent device or workload to a
Prometheus scrape or JSON response.

## Purpose and design rules

The telemetry system is a node-local observer for Tenstorrent devices. It has
two outputs:

- Prometheus exposition text for monitoring and alerting.
- Structured JSON for inventory, debugging, and DRA-related consumers.

The current node exporter is implemented in C++ with a small embedded HTTP
server. Python is used only for the workload-side TTNN adapter; the current
implementation does not use FastAPI.

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
                                      embedded HTTP server
                                            |
                         /metrics   /v1/devices   /healthz
```

The C++ exporter does not open a Tenstorrent device through TT-Metalium. It
observes sysfs and state files, which avoids conflicting with the workload that
owns the device.

## 1. Application entry point and polling loop

Files:

- `src/main.cpp`

The `tt-metrics-exporter` executable begins in `main.cpp`. It performs the
following work:

1. Parses command-line options.
2. Builds a `CollectorConfig` and creates a `SysfsCollector`.
3. Performs an initial collection.
4. Renders and caches both Prometheus and JSON output.
5. Starts a background polling thread.
6. Starts the HTTP server in the main thread.
7. Handles `SIGINT` and `SIGTERM` for an orderly shutdown.

The default polling interval is five seconds. Collection and rendering happen
outside the cache mutex. The completed Prometheus and JSON strings are swapped
into the cache while holding the mutex, so an HTTP request sees a complete old
or complete new snapshot rather than a partially updated response.

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
- `--collect-pcie-counters`: enables optional PCIe counter reads.
- `--listen-address`, `--port`, and `--poll-interval`: server controls.

## 2. Shared telemetry data model

Files:

- `include/tt_metrics_exporter/device.hpp`

`device.hpp` defines the typed model shared by collection and rendering.
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

Most fields use `std::optional`. An absent value means no safe source reported
it. Renderers preserve that distinction rather than filling in a guessed zero.

`CollectorConfig` carries all input roots and feature switches into the
collector.

## 3. Sysfs and node-state collector

Files:

- `src/device.cpp`
- `include/tt_metrics_exporter/device.hpp`

`SysfsCollector::collect()` iterates the directories below the configured
sysfs root. Each directory becomes one `DeviceTelemetry` record.

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

The `hwmon` collector scans input files, associates labels when present, and
assigns units based on the sensor family. Missing or unreadable sensors are
skipped.

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

## 4. DRA allocation-state reader

Files:

- `src/device.cpp`

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

- `src/device.cpp`

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

The default state root is:

```text
/var/lib/tt-device-plugin/metalium-profiler
```

The layout is:

```text
<state-root>/<device-key>/<safe-workload-name>.state
```

The publisher writes a temporary file, flushes and `fsync`s it, changes it to
mode `0644`, atomically renames it over the target, and `fsync`s the directory.
This prevents the exporter from reading a partially written sample.

On a normal process exit, `close()` writes a final inactive sample with zero
cores used. If the process is killed and cannot run cleanup, the exporter's
freshness window eventually marks the last sample stale and inactive.

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

- `src/device.cpp`

The C++ collector searches the configured profiler root using the sysfs device
ID, PCI BDF, and character-device basename. It reads schema-version-1 `.state`
files and validates required fields.

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

- `src/metrics.cpp`
- `include/tt_metrics_exporter/metrics.hpp`

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

## 10. JSON renderer

Files:

- `src/json.cpp`
- `include/tt_metrics_exporter/json.hpp`

`render_devices_json()` emits a versioned document:

```json
{
  "apiVersion": "telemetry.tenstorrent.com/v1",
  "kind": "DeviceList",
  "summary": {"devicesDiscovered": 1},
  "devices": []
}
```

Each device contains structured sections corresponding to the shared C++ data
model. Optional values remain JSON `null` where appropriate, allowing a
consumer to distinguish an unavailable measurement from an actual zero.

## 11. Embedded HTTP server

Files:

- `src/http_server.cpp`
- `include/tt_metrics_exporter/http_server.hpp`

The exporter contains a small IPv4 HTTP/1.1 server implemented with POSIX
sockets. It serves cached strings supplied by callbacks from `main.cpp`.

Endpoints are:

- `GET /metrics`: Prometheus exposition format.
- `GET /v1/devices`: structured JSON inventory.
- `GET /healthz`: process liveness response, `ok`.
- Any other path: `404 Not Found`.

The server processes one accepted connection at a time, closes each connection
after its response, and uses a one-second `select()` timeout so shutdown signals
are observed promptly. It does not currently implement TLS, authentication,
HTTP keep-alive, request concurrency, or source-level readiness checks.

## 12. Build and installation

Files:

- `CMakeLists.txt`

CMake builds a reusable C++ library containing collection, rendering, and HTTP
logic, then links it into the `tt-metrics-exporter` executable.

The install step places:

- `tt-metrics-exporter` under the configured binary install directory.
- The TTNN publisher and dynamic example under
  `share/tt-metrics-exporter/integrations/ttnn`.

Python is optional for building the C++ exporter. When a Python interpreter is
available, CMake also registers the publisher unit tests with CTest.

## 13. Tests

Files:

- `tests/sysfs_collector_test.cpp`
- `tests/metalium_profiler_publisher_test.py`

The C++ test creates temporary sysfs and state trees. It verifies parsers,
device collection, missing-root behavior, Prometheus output, JSON output,
allocation and janitor state, Metalium aggregation, staleness, and invalid
profiler records.

The Python test verifies atomic publication, inactive cleanup, empty reads,
runtime-chip-to-exporter-device mapping, impossible core-count rejection, and
required profiler environment validation.

Run all configured tests with:

```bash
cmake -S src/telemetry -B /tmp/tt-metrics-exporter-build
cmake --build /tmp/tt-metrics-exporter-build
ctest --test-dir /tmp/tt-metrics-exporter-build --output-on-failure
```

## 14. Runtime and simulator boundary

The C++ sysfs exporter can be validated in the QEMU guest after the official
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
current QEMU VM does not mean the C++ exporter failed. It means no compatible
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
