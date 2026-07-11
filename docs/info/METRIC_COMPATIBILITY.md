# Metric and API Compatibility Policy

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
[`telemetry.tenstorrent.com-v1.schema.json`](../schema/telemetry.tenstorrent.com-v1.schema.json).
New optional fields may be added within v1, so consumers must tolerate unknown
properties. Existing fields cannot be removed or change meaning. Incompatible
changes require a new API version.

`tt_metalium_workload_core_occupancy_ratio` is recent spatial core occupancy,
not wall-clock utilization. Generic Tensix metrics include a `source` identity
through `tt_tensix_info` so consumers can distinguish direct sysfs values from
profiler-derived values. A time-utilization metric will not be introduced until
its sampling window, overlap, concurrency, and duration semantics are verified
on physical hardware.
