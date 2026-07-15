# Exporter Operational Contract

This contract defines the stable runtime behavior of the Tenstorrent metrics
exporter. The production manifest targets the Python node service. A separate
Python TTNN workload adapter publishes process-local profiler snapshots.

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
