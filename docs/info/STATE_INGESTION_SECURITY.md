# Node-State Ingestion Security and Lifecycle

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
63-byte namespace/container names, a 253-byte pod name, 4,096 inspected files
per device, and 1,024 exported workloads per device. Valid fresh records are
prioritized before stale records when applying the export limit.

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
