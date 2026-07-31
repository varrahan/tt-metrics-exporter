# Environment Variables

This document defines the environment variables used by the telemetry system.

## Exporter runtime

| Variable | Used by | Required | Default | Meaning |
| --- | --- | --- | --- | --- |
| `TT_EXPORTER_REVISION` | `tt_metrics_exporter.app.cli` | No | `"unknown"` | Exporter build revision used in version response (`--version`). |
| `TT_EXPORTER_BUILD_TIME` | `tt_metrics_exporter.app.cli` | No | `"unknown"` | Exporter build timestamp used in version response (`--version`). |

## TT-Metalium profiler publish path

These are consumed by `integrations/ttnn/metalium_profiler_publisher.py` (and
workloads using it):

| Variable | Required | Default | Meaning |
| --- | --- | --- | --- |
| `TT_METAL_DEVICE_PROFILER` | Yes (must be `"1"` for profiling) | N/A | Enables TT-Metalium device profiler mode. Must be set before TTNN initializes. |
| `TT_METAL_PROFILER_MID_RUN_DUMP` | Yes (must be `"1"` for profiling) | N/A | Enables mid-run profiler dumps expected by the TT-Metalium workflow. |
| `TT_METAL_PROFILER_CPP_POST_PROCESS` | Yes (must be `"1"` for profiling) | N/A | Enables required TT-Metalium post-process path for latest program data extraction. |
| `TT_METAL_PROFILER_DISABLE_DUMP_TO_FILES` | No | unset unless set by caller | Prevents profiler CSV/file dump side effects. Setting `"1"` keeps in-process data available to the publisher. |
| `TT_METALIUM_PROFILER_STATE_ROOT` | No | `/var/lib/tt-device-plugin/metalium-profiler` | Base directory used by publisher when a `state_root` argument is not supplied. |

`MetaliumProfilerPublisher.validate_profiler_environment()` checks only the three required profiler flags listed above; if any are not exactly `"1"` it raises a runtime error before TTNN initialization.

### Workload identity propagation

The publisher uses identity variables when creating state records:

| Variable | Used for |
| --- | --- |
| `TT_WORKLOAD_ID` | Explicit workload identifier passed when `workload_id` is not provided in constructor or CLI args. |
| `POD_UID` | Fallback identity when `TT_WORKLOAD_ID` is not set. |
| `POD_NAME` | Secondary fallback identity and optional metadata field in exported profiler state. |
| `POD_NAMESPACE` | Exported workload namespace metadata in profiler state. |
| `CONTAINER_NAME` | Exported workload container metadata in profiler state. |

## Host/runtime usage notes

- The exporter runtime itself does not read `TT_SYSFS_ROOT`, `TT_DEVICE_PATH`, or
  `TT_KMD_MODULE`; those are used only in test/setup shell workflows.
- For local development, setting a shell variable like `TT_SYSFS_ROOT` is
  optional helper notation in docs/examples and is not required by the exporter
  process.
