# Support Scripts

These scripts are repository tooling and are not installed with the exporter.

- `ci/` contains fast host checks used by CI and container builds.
- `validation/` qualifies images, Kubernetes resources, monitoring behavior,
  rollouts, enforced NetworkPolicy, bounded soak behavior, and hardware-backed
  VM releases.
- `operations/` contains opt-in workload and soak tools for a running exporter.

Run scripts from the telemetry repository root so deployment and documentation
paths resolve consistently.

`validation/vm.sh` is the complete TTSim entry point. Its default five-minute
soak is a smoke qualification; set `SOAK_DURATION_SECONDS=259200` for the
72-hour physical-hardware release gate.
