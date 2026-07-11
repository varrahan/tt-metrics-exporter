# Exporter alerts

## Target absent

Confirm the DaemonSet pod is scheduled on the node, then inspect pod status,
events, NetworkPolicy enforcement, and the ServiceMonitor selector. Verify
`GET /healthz` from an allowed monitoring pod. Roll back if absence began with
the current rollout.

## Not ready

Read `GET /readyz` and use its bounded `reason`. Check
`tt_exporter_source_accessible`, collection failures, and pod events. Do not
restart repeatedly when the reason is a missing host source.

## Stale collection

Check collection duration, CPU throttling, filesystem responsiveness, and
`tt_exporter_collection_attempts_total`. A retained `/v1/devices` response is
expected while refreshes fail. Roll back to the previous signed release if the
new version cannot publish fresh snapshots.

## Critical source

Verify the `/sys/class/tenstorrent` host mount, KMD binding, permissions, and
node labels. Optional state roots do not cause this readiness reason.
