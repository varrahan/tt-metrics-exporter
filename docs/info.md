# Telemetry Information and Operations

## Exporter Operational Contract

This contract defines the stable runtime behavior of the Tenstorrent metrics
exporter. The production manifest targets the Python node service. A separate
Python TTNN workload adapter publishes process-local profiler snapshots.
Supported runtime environments are Linux systems with Tenstorrent sysfs and
trusted node-local state writers; distro packaging is intentionally not assumed.

## Endpoint semantics

- `GET /healthz` is a shallow liveness check. It returns `200` with `ok` while
  the HTTP loop can serve requests. It never reads hardware or state files and
  does not report snapshot readiness.
- `GET /readyz` is a readiness check. It returns `200` only after a successful
  initial collection and while the last complete snapshot is no older than the
  configured maximum age. It returns `503` with a bounded, enumerated reason
  for a critical source failure, an expired snapshot, shutdown, or a failed
  configured device-presence requirement.
- `GET /metrics` returns the most recent complete Prometheus device snapshot
  plus exporter self-metrics. A failed refresh does not replace the last
  complete snapshot.
- `GET /v1/devices` returns the same generation of the most recent complete
  device snapshot as `/metrics`.

Missing optional firmware, `hwmon`, memory, core, topology, or link attributes
do not make the exporter unready. Unsupported values remain absent rather than
being synthesized as zero.

Potentially side-effecting sysfs families are explicit opt-ins. `hwmon` reads
require `--collect-hwmon`, and PCIe performance counters require
`--collect-pcie-counters`. The production physical-hardware deployment enables
only the sources qualified for that platform.

## Source policy

The bounded collection sources are:

| Source | Configuration | Failure behavior |
| --- | --- | --- |
| `sysfs_root` | Always configured and critical | A missing, unreadable, or failed root makes collection unsuccessful and readiness false. |
| `allocation_state` | Optional | An unconfigured root is ignored. Once configured, root unreadability is reported as an operational error, while an absent per-device field remains valid missing data. |
| `janitor_state` | Optional | An unconfigured root is ignored. Once configured, root unreadability is reported as an operational error, while an absent per-device field remains valid missing data. |
| `metalium_profiler_state` | Optional | An unconfigured root is ignored. Once configured, root unreadability is reported as an operational error; rejected, stale, and excessive records use bounded diagnostics. |

Metric labels and readiness responses never contain paths, file contents,
workload identifiers, exception strings, or raw operating-system errors.
HTTP failures are reported separately through bounded route and status
dimensions.

## Validation boundary

Host validation covers builds, unit tests, parser tests, static checks, and
documentation checks. Hardware-dependent validation runs on physical
Tenstorrent hardware or in the official QEMU `ttsim` VM using TCG with
`-cpu max`; KVM and `-cpu host` are not supported recommendations.

QEMU validates PCI enumeration, KMD binding, sysfs collection, container
deployment, controlled alert behavior, CNI-enforced ingress policy, short soak
behavior, and snapshot ingestion. The provisional resource envelope requires a
72-hour soak on physical hardware. Real TTNN profiler execution requires
compatible physical hardware or a simulator implementing the required device
and profiler path.

## Production Container

The multi-stage [`Dockerfile`](../Dockerfile) uses the Docker Official
`python:3.11-slim-bookworm` image for builds and a distroless Python image for
the runtime. Both bases are pinned by digest. Python build packages are pinned
by the hashed development requirements, and the complete contract suite runs
before building the Python wheel. The final runtime uses UID/GID `65532`, has
no shell, compiler, package manager, TTNN, or publisher, and exposes
pre-created read-only mount targets below `/mnt/tt`. It disables bytecode
writes and needs no writable application directory.

Build reproducibly by supplying release metadata:

```bash
docker build \
  --build-arg VERSION=0.1.0 \
  --build-arg REVISION="$(git rev-parse HEAD)" \
  --build-arg SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH}" \
  --build-arg BUILD_DATE=1970-01-01T00:00:00Z \
  -t tt-metrics-exporter:0.1.0 .
```

A plain local `docker build .` uses the package's current version.
Release builds still pass and verify explicit immutable version metadata.

`SOURCE_DATE_EPOCH` is exposed by `--version`; version and revision are also in
`tt_exporter_build_info`. The OCI license label is `Apache-2.0`, and the final
image carries the complete license text at `/licenses/LICENSE`.

Validate the final image with:

```bash
scripts/validation/image.sh tt-metrics-exporter:0.1.0 image-reports
```

The validation checks metadata and non-root identity, rejects runtime shell,
compiler, package-manager, and generated application bytecode paths, requires
the Python module entry point and Apache-2.0 license metadata/text, creates
SPDX JSON with pinned Syft and SARIF critical-vulnerability results with pinned
Trivy, and runs the final image with a read-only root, `no-new-privileges`, all
capabilities dropped, read-only telemetry input, health/readiness/metrics
checks, and bounded graceful termination.

The scanners read a temporary `docker save` archive. They do not require Docker
SBOM or Scout plugins and are not given the Docker socket.

## Metric and API Compatibility Policy

The first production release freezes metric names, types, units, label names,
and meanings for its major version.

- A label cannot be removed or change meaning without a major release.
- Every new label must use a bounded enumeration or bounded resource identity
  and pass a worst-case cardinality review.
- Experimental families use the `tt_experimental_` prefix or a disabled-by-
  default feature flag.
- A deprecated family remains available for at least one supported release
  window and is called out in release notes before removal.
- Counters end in `_total`; durations and timestamps end in `_seconds`; byte
  values end in `_bytes`; ratios use `_ratio`.
- Rapidly changing measurements are sample values, never labels.

The JSON API follows
[`telemetry.tenstorrent.com-v1.schema.json`](schema/telemetry.tenstorrent.com-v1.schema.json).
New optional fields may be added within v1, so consumers must tolerate unknown
properties. Existing fields cannot be removed or change meaning. Incompatible
changes require a new API version.

`tt_metalium_workload_core_occupancy_ratio` is recent spatial core occupancy,
not wall-clock utilization. Generic Tensix metrics include a `source` identity
through `tt_tensix_info` so consumers can distinguish direct sysfs values from
profiler-derived values. A time-utilization metric will not be introduced until
its sampling window, overlap, concurrency, and duration semantics are verified
on physical hardware.

## Kubernetes Deployment and Monitoring

Render and validate the portable base and overlays with:

```bash
scripts/validation/manifests.sh
scripts/validation/monitoring.sh
```

With the locally built image, validate a rollout and undo on a disposable
Kubernetes 1.34 Kind cluster:

```bash
scripts/validation/kind_rollout.sh tt-metrics-exporter:python-local
scripts/validation/kind_network_policy.sh tt-metrics-exporter:python-local
```

The rollout test allows ten minutes for Kind and exporter readiness. The policy
test allows fifteen minutes and widens only its disposable cluster's
control-plane leader-election and startup budgets because nested TCG
virtualization can pause the Kubernetes API for tens of seconds.

These synthetic Kind tests use a read-only sysfs directory. They prove image
loading, scheduling, probes, rollout history, undo, and policy behavior; they
do not prove KMD or hardware telemetry behavior.

The NetworkPolicy test creates a separate single-node Kind cluster with the
pinned Calico manifest, proves an HTTP request from the `monitoring` namespace
succeeds, and proves the same request from `default` is denied. Run both tests
inside the QEMU VM or on a physical validation host. Both scripts delete their
disposable clusters unless `KEEP_KIND_CLUSTER=1` is set.

The base creates a tokenless ServiceAccount with no RBAC, an unprivileged
DaemonSet, a headless per-pod Service, and default-deny ingress allowing only
the `monitoring` namespace on port 9400. The CNI must enforce NetworkPolicy; if
it does not, equivalent management-network isolation is required.

The startup probe allows up to five minutes for the initial VM or hardware
collection before liveness checks can restart the process. Readiness remains
false until the exporter has published a complete snapshot.

The two sysfs mounts preserve relative class-to-device links without mounting
all host sysfs into the application namespace. Production additionally mounts
DRA, janitor, and profiler roots read-only. No `/dev`, device node, runtime
socket, writable system path, host networking, host PID/IPC, privilege, or
capability is used.

The `ttsim` overlay does not require a device. The production overlay requires
one, includes node affinity/toleration and approved state roots, identifies the
Python implementation, and pins the locally tested image by digest. Release
automation replaces the example registry and digest with the pushed, signed
artifact. The initial resource envelope is
provisional until physical-hardware load and soak testing validates it.

The optional monitoring package requires Prometheus Operator CRDs and supplies
a 15-second ServiceMonitor, recording/alert rules, a bounded sample limit, and
a dashboard. The alert annotations link to the exporter contracts in
`info.md` rather than separate runbook documents. Clusters without the
Operator can add their
approved Prometheus pod annotations to the DaemonSet template and scrape the
named `metrics` port at `/metrics`; those annotations are intentionally absent
from the portable base.

`scripts/validation/monitoring.sh` checks all rules with pinned `promtool` and
executes controlled firing and recovery scenarios for the core availability,
device-presence, stale-snapshot, reset, and quarantine alerts.

Live apply, CNI enforcement, per-node discovery, rolling update, and rollback
must be tested on Kubernetes v1.34+ inside the official QEMU VM or on physical
hardware. Static manifest rendering alone is not evidence for those gates.

For an emergency rollback, use
`kubectl rollout undo daemonset/tt-metrics-exporter` after confirming that the
previous revision references the intended signed release digest.

## Node-State Ingestion Security and Lifecycle

## Ownership and mounts

| Root | Writer | Exporter access |
| --- | --- | --- |
| Tenstorrent and backing PCI sysfs | Kernel and `tt-kmd` | Read-only |
| Allocation state | Trusted DRA node service | Read-only |
| Janitor state | Trusted janitor node service | Read-only |
| Profiler state parent | Trusted DRA/node service | Read-only |
| One profiler workload subtree | Exactly one assigned workload identity | Read/write only to that subtree; no parent or sibling mount |

The exporter opens each configured state root once, then uses `openat` relative
to that descriptor with `O_DIRECTORY`, `O_NOFOLLOW`, `O_CLOEXEC`, regular-file
`fstat` checks, a one-link requirement, and one bounded read. Components cannot
be empty, `.`, `..`, contain slash/NUL/control bytes, or exceed their limit.
FIFOs, sockets, devices, directories, symlinks, and unexpected hard links are
rejected.

## Profiler layout and migration

The production layout is:

```text
<trusted-root>/v2/workloads/<pod-uid>/<device-key>/snapshot.state
```

The trusted DRA/node service creates `<pod-uid>`, assigns ownership, and mounts
only that directory into the pod. `MetaliumProfilerPublisher.state_root` is
that workload-scoped mount, so the publisher writes
`<state_root>/<device-key>/snapshot.state` with `schema_version=2`. Labels in
the file are descriptive; the trusted pod-UID directory supplies the exported
workload identity. Duplicate legacy identities are reduced to the freshest
valid record so one scrape cannot contain duplicate series.

The exporter reads legacy `schema_version=1` files at
`<trusted-root>/<device-key>/*.state` during the migration window ending
2027-01-10. A v1 record cannot replace a v2 record with the same workload ID.

Limits are 16 KiB per state file, 32 fields, 128-byte workload/device keys,
63-byte namespace/container names, a 253-byte pod name, 4,096 entries per
bounded directory scan, and 1,024 exported workloads per device. Valid fresh
records are prioritized before stale records when applying the export limit.

## Cleanup ownership

DRA `UnprepareResourceClaims` owns deletion of the complete pod/claim workload
subtree. This is required for normal termination and is idempotent for abrupt
pod termination. The exporter remains read-only and has no fallback garbage
collector; a cleanup retention policy is therefore not silently duplicated in
the observer. If a deployment cannot guarantee DRA cleanup, it must add a
separately reviewed trusted-node janitor with a retention period much longer
than profiler freshness before enabling fallback deletion.

## Publisher failure policy

The publisher is best-effort by default. Profiler, validation, and atomic
publication failures increment `failure_count`, set bounded `last_failure`,
warn once, and return an empty summary without terminating the workload.
`last_success_timestamp` exposes recovery state. Invalid profiler environment
disables later sampling. A minimum sample interval defaults to one second.
Tests and development can pass `strict=True` to raise failures immediately.
Temporary files are removed after write, `fsync`, chmod, or rename failures.
