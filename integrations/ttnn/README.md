# TT-Metalium Profiler Publisher

This separately versioned Python module publishes bounded process-local TTNN
profiler summaries into a workload-scoped v2 state mount. It is best-effort by
default so telemetry failures do not terminate model execution.

The publisher is a standalone Python module; it does not install TTNN and does
not open devices on behalf of the exporter. Install it from this directory when
building a workload image:

```bash
python3 -m pip install .
```

Use `example_dynamic_workload.py` as the integration example. The node-visible
layout, ownership, and lifecycle contract is documented in
[`docs/info/STATE_INGESTION_SECURITY.md`](../../docs/info/STATE_INGESTION_SECURITY.md).
