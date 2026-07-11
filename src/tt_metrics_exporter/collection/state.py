"""Secure node-state ingestion for allocation, janitor, and profiler data."""

from __future__ import annotations

from pathlib import Path
import time
from typing import Callable

from ..models import (
    AllocationTelemetry,
    CollectionIssue,
    CollectionResult,
    CollectorConfig,
    DeviceTelemetry,
    JanitorTelemetry,
    MetaliumWorkloadTelemetry,
    SourceDiagnostics,
    TelemetrySource,
)
from .parsers import parse_int64
from .secure_io import SecureDirectory, valid_component


_MAXIMUM_STATE_FILE_BYTES = 16 * 1024
_MAXIMUM_STATE_FILES = 4096
_MAXIMUM_STATE_FIELDS = 32
_MAXIMUM_WORKLOADS = 1024
_MAXIMUM_WORKLOAD_ID_BYTES = 128
_MAXIMUM_NAMESPACE_BYTES = 63
_MAXIMUM_POD_NAME_BYTES = 253
_MAXIMUM_CONTAINER_NAME_BYTES = 63


def _bounded_label(value: str | None, maximum: int, required: bool = False) -> bool:
    if value is None:
        return not required
    try:
        encoded = value.encode("ascii")
    except UnicodeEncodeError:
        return False
    return bool(value) and len(encoded) <= maximum and all(0x20 <= byte <= 0x7E for byte in encoded)


def _parse_key_values(content: str) -> dict[str, str] | None:
    if "\0" in content:
        return None
    values: dict[str, str] = {}
    for line in content.splitlines():
        if "=" not in line:
            continue
        key, value = (part.strip(" \t\n\r\v\f") for part in line.split("=", 1))
        if not key:
            continue
        if len(values) >= _MAXIMUM_STATE_FIELDS or key in values:
            return None
        values[key] = value
    return values


def _state_string(values: dict[str, str], key: str) -> str | None:
    value = values.get(key)
    return value if value else None


def _state_non_negative_int(values: dict[str, str], key: str) -> int | None:
    value = _state_string(values, key)
    parsed = parse_int64(value or "")
    return parsed if parsed is not None and parsed >= 0 else None


class StateIngestor:
    """Own configured root descriptors for one complete collection generation."""

    def __init__(
        self,
        roots: dict[TelemetrySource, SecureDirectory],
        result: CollectionResult,
        stale_after_seconds: int,
        now_seconds: Callable[[], int] | None = None,
    ) -> None:
        self._roots = roots
        self._result = result
        self._stale_after_seconds = stale_after_seconds
        self._now_seconds = now_seconds or (lambda: int(time.time()))

    @classmethod
    def open(cls, config: CollectorConfig, result: CollectionResult) -> StateIngestor:
        roots: dict[TelemetrySource, SecureDirectory] = {}
        for source, path in (
            (TelemetrySource.ALLOCATION_STATE, config.allocation_state_root),
            (TelemetrySource.JANITOR_STATE, config.janitor_state_root),
            (
                TelemetrySource.METALIUM_PROFILER_STATE,
                config.metalium_profiler_state_root,
            ),
        ):
            diagnostics = result.sources[source]
            diagnostics.configured = path is not None
            if path is None:
                continue
            directory = SecureDirectory.open_root(path, diagnostics)
            if directory is not None:
                roots[source] = directory
        return cls(roots, result, config.metalium_profiler_stale_after_seconds)

    def close(self) -> None:
        for root in self._roots.values():
            root.close()
        self._roots.clear()

    def __enter__(self) -> StateIngestor:
        return self

    def __exit__(self, *_unused: object) -> None:
        self.close()

    def populate(self, device: DeviceTelemetry) -> None:
        allocation_root = self._roots.get(TelemetrySource.ALLOCATION_STATE)
        if allocation_root is not None:
            device.allocation = self._read_allocation(device, allocation_root)
        janitor_root = self._roots.get(TelemetrySource.JANITOR_STATE)
        if janitor_root is not None:
            device.janitor = self._read_janitor(device, janitor_root)
        profiler_root = self._roots.get(TelemetrySource.METALIUM_PROFILER_STATE)
        if profiler_root is not None:
            device.metalium_workloads = self._read_workloads(device, profiler_root)
            self._apply_workloads(device)

    @staticmethod
    def _device_keys(device: DeviceTelemetry) -> list[str]:
        candidates = [device.id]
        if device.pci.bdf is not None:
            candidates.append(device.pci.bdf)
        if device.character_device is not None and device.character_device.dev_name is not None:
            candidates.append(Path(device.character_device.dev_name).name)
        return [candidate for candidate in candidates if valid_component(candidate)]

    def _first_device_directory(
        self,
        root: SecureDirectory,
        device: DeviceTelemetry,
        diagnostics: SourceDiagnostics,
    ) -> SecureDirectory | None:
        for candidate in self._device_keys(device):
            directory = root.open_directory(candidate, diagnostics)
            if directory is not None:
                return directory
        return None

    @staticmethod
    def _validate_label(value: str | None, maximum: int, diagnostics: SourceDiagnostics) -> str | None:
        if value is not None and not _bounded_label(value, maximum):
            diagnostics.issues[CollectionIssue.INVALID_VALUE] += 1
            return None
        return value

    def _read_allocation(self, device: DeviceTelemetry, root: SecureDirectory) -> AllocationTelemetry:
        diagnostics = self._result.sources[TelemetrySource.ALLOCATION_STATE]
        directory = self._first_device_directory(root, device, diagnostics)
        if directory is None:
            return AllocationTelemetry()
        with directory:
            allocation = AllocationTelemetry(
                claim_namespace=directory.read_text("claim_namespace", _MAXIMUM_NAMESPACE_BYTES, diagnostics),
                claim_name=directory.read_text("claim_name", _MAXIMUM_POD_NAME_BYTES, diagnostics),
                claim_uid=directory.read_text("claim_uid", _MAXIMUM_WORKLOAD_ID_BYTES, diagnostics),
                pod_namespace=directory.read_text("pod_namespace", _MAXIMUM_NAMESPACE_BYTES, diagnostics),
                pod_name=directory.read_text("pod_name", _MAXIMUM_POD_NAME_BYTES, diagnostics),
                container_name=directory.read_text("container_name", _MAXIMUM_CONTAINER_NAME_BYTES, diagnostics),
            )
        allocation.claim_namespace = self._validate_label(allocation.claim_namespace, _MAXIMUM_NAMESPACE_BYTES, diagnostics)
        allocation.claim_name = self._validate_label(allocation.claim_name, _MAXIMUM_POD_NAME_BYTES, diagnostics)
        allocation.claim_uid = self._validate_label(allocation.claim_uid, _MAXIMUM_WORKLOAD_ID_BYTES, diagnostics)
        allocation.pod_namespace = self._validate_label(allocation.pod_namespace, _MAXIMUM_NAMESPACE_BYTES, diagnostics)
        allocation.pod_name = self._validate_label(allocation.pod_name, _MAXIMUM_POD_NAME_BYTES, diagnostics)
        allocation.container_name = self._validate_label(allocation.container_name, _MAXIMUM_CONTAINER_NAME_BYTES, diagnostics)
        diagnostics.records_accepted += 1
        return allocation

    @staticmethod
    def _read_state_int(directory: SecureDirectory, name: str, diagnostics: SourceDiagnostics) -> int | None:
        value = directory.read_text(name, _MAXIMUM_STATE_FILE_BYTES, diagnostics)
        if value is None:
            return None
        parsed = parse_int64(value)
        if parsed is None:
            diagnostics.issues[CollectionIssue.INVALID_VALUE] += 1
        return parsed

    def _read_janitor(self, device: DeviceTelemetry, root: SecureDirectory) -> JanitorTelemetry:
        diagnostics = self._result.sources[TelemetrySource.JANITOR_STATE]
        directory = self._first_device_directory(root, device, diagnostics)
        if directory is None:
            return JanitorTelemetry()
        with directory:
            janitor = JanitorTelemetry(
                state=directory.read_text("state", 128, diagnostics),
                quarantine_reason=directory.read_text("quarantine_reason", 256, diagnostics),
                last_scrub_status=directory.read_text("last_scrub_status", 128, diagnostics),
                last_reset_status=directory.read_text("last_reset_status", 128, diagnostics),
                scrub_count=self._read_state_int(directory, "scrub_count", diagnostics),
                reset_count=self._read_state_int(directory, "reset_count", diagnostics),
                last_scrub_timestamp_seconds=self._read_state_int(directory, "last_scrub_timestamp_seconds", diagnostics),
                last_reset_timestamp_seconds=self._read_state_int(directory, "last_reset_timestamp_seconds", diagnostics),
            )
        for attribute, maximum in (
            ("state", 128),
            ("quarantine_reason", 256),
            ("last_scrub_status", 128),
            ("last_reset_status", 128),
        ):
            setattr(
                janitor,
                attribute,
                self._validate_label(getattr(janitor, attribute), maximum, diagnostics),
            )
        diagnostics.records_accepted += 1
        return janitor

    def _parse_workload(self, content: str, expected_schema: int, diagnostics: SourceDiagnostics) -> MetaliumWorkloadTelemetry | None:
        values = _parse_key_values(content)
        if values is None:
            diagnostics.issues[CollectionIssue.INVALID_VALUE] += 1
            return None
        if _state_non_negative_int(values, "schema_version") != expected_schema:
            diagnostics.issues[CollectionIssue.UNSUPPORTED_SCHEMA] += 1
            return None
        workload = MetaliumWorkloadTelemetry(
            workload_id=_state_string(values, "workload_id") or "",
            pod_namespace=_state_string(values, "pod_namespace"),
            pod_name=_state_string(values, "pod_name"),
            container_name=_state_string(values, "container_name"),
            active=_state_non_negative_int(values, "active"),
            programs_observed=_state_non_negative_int(values, "programs_observed"),
            tensix_cores_used=_state_non_negative_int(values, "tensix_cores_used"),
            tensix_cores_total=_state_non_negative_int(values, "tensix_cores_total"),
            sample_timestamp_seconds=_state_non_negative_int(values, "sample_timestamp_seconds"),
        )
        invalid = not _bounded_label(workload.workload_id, _MAXIMUM_WORKLOAD_ID_BYTES, required=True) or not _bounded_label(workload.pod_namespace, _MAXIMUM_NAMESPACE_BYTES) or not _bounded_label(workload.pod_name, _MAXIMUM_POD_NAME_BYTES) or not _bounded_label(workload.container_name, _MAXIMUM_CONTAINER_NAME_BYTES) or workload.active is None or workload.active > 1 or workload.programs_observed is None or workload.tensix_cores_used is None or workload.sample_timestamp_seconds is None or (workload.tensix_cores_total is not None and (workload.tensix_cores_total == 0 or workload.tensix_cores_used > workload.tensix_cores_total))
        if invalid:
            diagnostics.issues[CollectionIssue.INVALID_VALUE] += 1
            return None
        now = self._now_seconds()
        workload.stale = self._stale_after_seconds > 0 and (workload.sample_timestamp_seconds < now - self._stale_after_seconds or workload.sample_timestamp_seconds > now + self._stale_after_seconds)
        if workload.stale:
            diagnostics.issues[CollectionIssue.STALE_RECORD] += 1
        diagnostics.records_accepted += 1
        return workload

    def _read_workloads(self, device: DeviceTelemetry, root: SecureDirectory) -> list[MetaliumWorkloadTelemetry]:
        diagnostics = self._result.sources[TelemetrySource.METALIUM_PROFILER_STATE]
        workloads: list[MetaliumWorkloadTelemetry] = []
        v2_identities: set[str] = set()

        v2 = root.open_directory("v2", diagnostics)
        if v2 is not None:
            with v2:
                workload_root = v2.open_directory("workloads", diagnostics)
                if workload_root is not None:
                    with workload_root:
                        self._read_v2_workloads(
                            device,
                            workload_root,
                            workloads,
                            v2_identities,
                            diagnostics,
                        )

        v1_device = self._first_device_directory(root, device, diagnostics)
        if v1_device is not None:
            with v1_device:
                for filename in v1_device.entries(_MAXIMUM_STATE_FILES, diagnostics):
                    if len(filename) <= 6 or not filename.endswith(".state"):
                        continue
                    content = v1_device.read_text(filename, _MAXIMUM_STATE_FILE_BYTES, diagnostics)
                    if content is None:
                        continue
                    workload = self._parse_workload(content, 1, diagnostics)
                    if workload is not None and workload.workload_id not in v2_identities:
                        workloads.append(workload)

        workloads.sort(
            key=lambda workload: (
                workload.stale,
                -(workload.sample_timestamp_seconds or 0),
                workload.workload_id,
            )
        )
        unique: list[MetaliumWorkloadTelemetry] = []
        identities: set[str] = set()
        for workload in workloads:
            if workload.workload_id in identities:
                diagnostics.issues[CollectionIssue.INVALID_VALUE] += 1
                continue
            identities.add(workload.workload_id)
            unique.append(workload)
        if len(unique) > _MAXIMUM_WORKLOADS:
            del unique[_MAXIMUM_WORKLOADS:]
            diagnostics.issues[CollectionIssue.CARDINALITY_LIMIT] += 1
        return unique

    def _read_v2_workloads(
        self,
        device: DeviceTelemetry,
        workload_root: SecureDirectory,
        workloads: list[MetaliumWorkloadTelemetry],
        identities: set[str],
        diagnostics: SourceDiagnostics,
    ) -> None:
        for pod_uid in workload_root.entries(_MAXIMUM_STATE_FILES, diagnostics):
            if not valid_component(pod_uid, _MAXIMUM_WORKLOAD_ID_BYTES):
                diagnostics.issues[CollectionIssue.INVALID_VALUE] += 1
                continue
            pod = workload_root.open_directory(pod_uid, diagnostics)
            if pod is None:
                continue
            with pod:
                device_directory = self._first_device_directory(pod, device, diagnostics)
                if device_directory is None:
                    continue
                with device_directory:
                    content = device_directory.read_text("snapshot.state", _MAXIMUM_STATE_FILE_BYTES, diagnostics)
            if content is None:
                continue
            workload = self._parse_workload(content, 2, diagnostics)
            if workload is not None:
                identities.add(workload.workload_id)
                workload.workload_id = pod_uid
                workloads.append(workload)

    @staticmethod
    def _apply_workloads(device: DeviceTelemetry) -> None:
        fresh = [workload for workload in device.metalium_workloads if not workload.stale]
        if not fresh:
            return
        cores_used = sum(workload.tensix_cores_used or 0 for workload in fresh if workload.active)
        totals = [workload.tensix_cores_total for workload in fresh if workload.tensix_cores_total is not None]
        cores_total = max(totals) if totals else None
        if cores_total is not None:
            cores_used = min(cores_used, cores_total)
        if device.tensix.used is None:
            device.tensix.used = cores_used
            device.tensix.source = "metalium_profiler"
        if device.tensix.total is None and cores_total is not None:
            device.tensix.total = cores_total
            device.tensix.source = "metalium_profiler"
        if device.tensix.available is None and cores_total is not None:
            device.tensix.available = max(0, cores_total - cores_used)
            device.tensix.source = "metalium_profiler"
