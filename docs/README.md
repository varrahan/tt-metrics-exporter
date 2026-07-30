# Tenstorrent Metrics Exporter Documentation

This documentation is organized to reduce duplication:

- [VM setup and validation](VM.md)
- [System guides](guides.md)
- [Operational contracts and policy](info.md)
- [JSON schema](schema/telemetry.tenstorrent.com-v1.schema.json)

Core concepts:
- The exporter reads node-local, safe data sources (not `tt-smi`).
- Runtime contracts are defined in `info.md`.
- Source collection and workloads are described in `guides.md`.
- VM-specific launch and verification is in `VM.md`.
