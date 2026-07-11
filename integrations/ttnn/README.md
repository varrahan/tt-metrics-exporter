# TT-Metalium Profiler Publisher

This separately versioned Python module publishes bounded process-local TTNN
profiler summaries into a workload-scoped v2 state mount. It is best-effort by
default so telemetry failures do not terminate model execution. See the parent
exporter's `docs/STATE_INGESTION_SECURITY.md` for the node-visible layout,
ownership, and lifecycle contract.
