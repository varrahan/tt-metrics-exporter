# Device health alerts

## Device missing

Compare the expected node inventory with `/v1/devices`, then verify PCI
enumeration, KMD binding, sysfs entries, and DaemonSet host mounts. Avoid device
reset until workload ownership is known.

## Reset required

Drain or stop the owning workload, follow the hardware janitor reset policy,
and confirm the reset-required signal clears before returning the node to
service.

## Quarantine

Inspect the bounded janitor state and quarantine reason. Keep the node
unschedulable until the approved scrub/reset workflow succeeds.

## Fault counters

Correlate counter increases with workload and kernel logs without copying raw
state contents into labels. Escalate repeated OOM or hang faults to the device
owner.

## Heartbeat

Confirm the heartbeat is present and increasing across scrapes. A missing
optional heartbeat alone does not imply a dead device; use health and KMD state
as corroborating signals.
