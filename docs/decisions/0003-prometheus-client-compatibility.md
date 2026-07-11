# ADR 0003: Retain the contract-specific Prometheus renderer

## Status

Accepted.

## Decision

Keep the exporter-owned Prometheus renderer. Do not add `prometheus-client` as
a runtime dependency unless a future metric-version boundary permits exposition
changes.

## Rationale

A July 2026 compatibility spike parsed and regenerated the representative
renderer contract through `prometheus-client`. The output was not byte-compatible:
integer samples such as `1` became `1.0`, and the representative payload changed
from 1,611,219 to 1,618,395 bytes. The library also owns counter suffix and default
collector conventions that do not match this exporter's explicit schema.

The current renderer has golden contract coverage for names, labels, omission,
ordering, and bounded cardinality. Replacing it would create monitoring churn
without reducing the domain-specific mapping that accounts for most of its code.
