# Versioning and compatibility

Exporter releases use semantic versioning. The Python wheel, container label,
TTNN publisher package, deployment bundle, and Git tag must carry the same
version.

The v1 JSON schema and existing Prometheus names, types, labels, and meanings
remain backward compatible within a major version. Additive optional JSON
fields and metric families are minor changes. Removing or redefining a field,
metric, label, or endpoint requires a major version or a documented deprecation
window.

Every release records its immutable image digest in `RELEASE-METADATA`.
Kubernetes rollout history supplies the rollback boundary between releases.
