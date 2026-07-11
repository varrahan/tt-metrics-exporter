# Profiler-state alerts

## Rejected records

Use the bounded source/reason metrics to distinguish invalid values,
unsupported schemas, oversized files, and permission failures. Verify the TTNN
publisher version and atomic file ownership; never log the rejected contents.

## Cardinality

Find workloads publishing more than the documented per-device limit. Confirm
terminated pod state is removed by its owning cleanup controller and that each
pod writes only beneath its isolated v2 subtree.
