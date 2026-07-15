# Kubernetes Deployment and Monitoring

Render and validate the portable base and overlays with:

```bash
scripts/validation/manifests.sh
scripts/validation/monitoring.sh
```

With the locally built image, validate a rollout and undo on a disposable
Kubernetes 1.34 Kind cluster:

```bash
scripts/validation/kind_rollout.sh tt-metrics-exporter:python-local
scripts/validation/kind_network_policy.sh tt-metrics-exporter:python-local
```

The rollout test allows ten minutes for Kind and exporter readiness. The policy
test allows fifteen minutes and widens only its disposable cluster's
control-plane leader-election and startup budgets because nested TCG
virtualization can pause the Kubernetes API for tens of seconds.

These synthetic Kind tests use a read-only sysfs directory. They prove image
loading, scheduling, probes, rollout history, undo, and policy behavior; they
do not prove KMD or hardware telemetry behavior.

The NetworkPolicy test creates a separate single-node Kind cluster with the
pinned Calico manifest, proves an HTTP request from the `monitoring` namespace
succeeds, and proves the same request from `default` is denied. Run both tests
inside the QEMU VM or on a physical validation host. Both scripts delete their
disposable clusters unless `KEEP_KIND_CLUSTER=1` is set.

The base creates a tokenless ServiceAccount with no RBAC, an unprivileged
DaemonSet, a headless per-pod Service, and default-deny ingress allowing only
the `monitoring` namespace on port 9400. The CNI must enforce NetworkPolicy; if
it does not, equivalent management-network isolation is required.

The startup probe allows up to five minutes for the initial VM or hardware
collection before liveness checks can restart the process. Readiness remains
false until the exporter has published a complete snapshot.

The two sysfs mounts preserve relative class-to-device links without mounting
all host sysfs into the application namespace. Production additionally mounts
DRA, janitor, and profiler roots read-only. No `/dev`, device node, runtime
socket, writable system path, host networking, host PID/IPC, privilege, or
capability is used.

The `ttsim` overlay does not require a device. The production overlay requires
one, includes node affinity/toleration and approved state roots, identifies the
Python implementation, and pins the locally tested image by digest. Release
automation replaces the example registry and digest with the pushed, signed
artifact. The initial resource envelope is
provisional until physical-hardware load and soak testing validates it.

The optional monitoring package requires Prometheus Operator CRDs and supplies
a 15-second ServiceMonitor, recording/alert rules, a bounded sample limit, and
a dashboard. The alert annotations link to the exporter contracts in
`docs/info/` rather than separate runbook documents. Clusters without the
Operator can add their
approved Prometheus pod annotations to the DaemonSet template and scrape the
named `metrics` port at `/metrics`; those annotations are intentionally absent
from the portable base.

`scripts/validation/monitoring.sh` checks all rules with pinned `promtool` and
executes controlled firing and recovery scenarios for the core availability,
device-presence, stale-snapshot, reset, and quarantine alerts.

Live apply, CNI enforcement, per-node discovery, rolling update, and rollback
must be tested on Kubernetes v1.34+ inside the official QEMU VM or on physical
hardware. Static manifest rendering alone is not evidence for those gates.

For an emergency rollback, use
`kubectl rollout undo daemonset/tt-metrics-exporter` after confirming that the
previous revision references the intended signed release digest.
