# ADR 0002: Use workload-isolated profiler state

- Status: Accepted
- Date: 2026-07-10

Production profiler publication uses the v2 layout and a workload-scoped mount
described in [STATE_INGESTION_SECURITY.md](../info/STATE_INGESTION_SECURITY.md).
Legacy v1 reads end on 2027-01-10. The exporter stays read-only, and DRA
Unprepare owns subtree deletion; exporter-side fallback garbage collection is
disabled because it would require a writable host mount and duplicate the
trusted lifecycle owner's responsibility.
