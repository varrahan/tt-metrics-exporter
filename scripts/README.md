# Support Scripts

These scripts are repository tooling and are not installed with the exporter.

- `ci/` contains fast host checks used by CI and container builds.
- `validation/` qualifies images, Kubernetes resources, monitoring, rollouts,
  and hardware-backed VM releases.
- `operations/` contains opt-in workload and soak tools for a running exporter.

Run scripts from the telemetry repository root so deployment and documentation
paths resolve consistently.
