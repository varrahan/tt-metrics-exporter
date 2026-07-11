"""Publish process-local TT-Metalium profiler samples for the node exporter.

TT-Metalium profiler records live in the workload process.  This module turns
the latest records into a small, atomically replaced state file that the
node-local exporter can consume without loading TT-Metalium itself.
"""

from __future__ import annotations

import atexit
import hashlib
import logging
import os
from pathlib import Path
import re
import socket
import threading
import time
from typing import Callable, Iterable, Mapping, Optional, Sequence


_REQUIRED_PROFILER_ENV = (
    "TT_METAL_DEVICE_PROFILER",
    "TT_METAL_PROFILER_MID_RUN_DUMP",
    "TT_METAL_PROFILER_CPP_POST_PROCESS",
)

__version__ = "0.1.0"


def _safe_component(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_.-]+", "-", value).strip("-.")
    if not cleaned:
        cleaned = "workload"
    if cleaned != value:
        digest = hashlib.sha256(value.encode("utf-8")).hexdigest()[:8]
        cleaned = f"{cleaned}-{digest}"
    return cleaned[:160]


def _state_value(value: object) -> str:
    return str(value).replace("\r", " ").replace("\n", " ")


def _non_negative_int(value: object, field: str) -> int:
    parsed = int(value)
    if parsed < 0:
        raise ValueError(f"{field} must be non-negative")
    return parsed


class MetaliumProfilerPublisher:
    """Write fresh core-footprint samples from a profiled TTNN workload.

    Call :meth:`sample` after a synchronized workload iteration.  The sample is
    deliberately described as core occupancy rather than time-weighted
    utilization: ``ProgramAnalysisData.core_count`` reports the spatial core
    footprint of a completed program.
    """

    def __init__(
        self,
        state_root: os.PathLike[str] | str | None = None,
        workload_id: Optional[str] = None,
        *,
        device_keys: Sequence[str | int] = ("0",),
        device_key_map: Optional[Mapping[str | int, str | int]] = None,
        pod_namespace: Optional[str] = None,
        pod_name: Optional[str] = None,
        container_name: Optional[str] = None,
        layout_version: int = 2,
        strict: bool = False,
        minimum_sample_interval_seconds: float = 1.0,
        warning_callback: Optional[Callable[[str], None]] = None,
    ) -> None:
        self.state_root = Path(state_root or os.environ.get("TT_METALIUM_PROFILER_STATE_ROOT") or "/var/lib/tt-device-plugin/metalium-profiler")
        self.workload_id = workload_id or os.environ.get("TT_WORKLOAD_ID") or os.environ.get("POD_UID") or os.environ.get("POD_NAME") or f"{socket.gethostname()}-{os.getpid()}"
        self.pod_namespace = pod_namespace or os.environ.get("POD_NAMESPACE")
        self.pod_name = pod_name or os.environ.get("POD_NAME")
        self.container_name = container_name or os.environ.get("CONTAINER_NAME")
        if layout_version not in (1, 2):
            raise ValueError("layout_version must be 1 or 2")
        if minimum_sample_interval_seconds < 0:
            raise ValueError("minimum_sample_interval_seconds must be non-negative")
        self.layout_version = layout_version
        self.strict = strict
        self.minimum_sample_interval_seconds = minimum_sample_interval_seconds
        self._warning_callback = warning_callback or logging.getLogger(__name__).warning
        self.device_keys = tuple(str(device_key) for device_key in device_keys)
        self.device_key_map = {str(chip_id): str(device_key) for chip_id, device_key in (device_key_map or {}).items()}
        self._known_totals: dict[str, int] = {}
        self._published_device_keys: set[str] = set()
        self._lock = threading.Lock()
        self._closed = False
        self._disabled = False
        self._warned = False
        self._last_sample_monotonic: Optional[float] = None
        self.failure_count = 0
        self.last_failure: Optional[str] = None
        self.last_success_timestamp: Optional[float] = None
        atexit.register(self.close)

    @staticmethod
    def validate_profiler_environment() -> None:
        missing = [name for name in _REQUIRED_PROFILER_ENV if os.environ.get(name) != "1"]
        if missing:
            joined = ", ".join(missing)
            raise RuntimeError(f"TT-Metalium profiling requires these variables to equal 1 before TTNN initializes: {joined}")

    def sample(self, device: object) -> Mapping[str, Mapping[str, int]]:
        """Read the latest process-local profiler data and publish it.

        ``ttnn.ReadDeviceProfiler`` is intentionally invoked inside the
        workload process.  A standalone exporter process cannot read these
        process-local records from another TTNN runtime.
        """

        if self._disabled:
            return {}
        now = time.monotonic()
        if self._last_sample_monotonic is not None and now - self._last_sample_monotonic < self.minimum_sample_interval_seconds:
            return {}
        self._last_sample_monotonic = now
        try:
            self.validate_profiler_environment()
            import ttnn  # Imported only after the profiler environment is validated.

            ttnn.ReadDeviceProfiler(device)
            return self._publish_profiler_data(ttnn.get_latest_programs_perf_data())
        except Exception as error:  # Telemetry must not terminate model execution.
            self._handle_failure(error, disable=isinstance(error, RuntimeError))
            if self.strict:
                raise
            return {}

    def publish_profiler_data(self, profiler_data: Mapping[object, Iterable[object]]) -> Mapping[str, Mapping[str, int]]:
        """Publish already-read records; separated for testing and adapters."""

        if self._disabled:
            return {}
        try:
            return self._publish_profiler_data(profiler_data)
        except Exception as error:  # Publication is best effort by default.
            self._handle_failure(error)
            if self.strict:
                raise
            return {}

    def _publish_profiler_data(self, profiler_data: Mapping[object, Iterable[object]]) -> Mapping[str, Mapping[str, int]]:
        summaries: dict[str, dict[str, int]] = {}
        seen_device_keys: set[str] = set()

        for chip_id, records_iterable in profiler_data.items():
            device_key = self.device_key_map.get(str(chip_id), str(chip_id))
            records = list(records_iterable)
            seen_device_keys.add(device_key)
            core_counts = [_non_negative_int(record.core_count, "core_count") for record in records]
            core_totals = [_non_negative_int(record.num_available_cores, "num_available_cores") for record in records]
            cores_used = max(core_counts, default=0)
            if core_totals:
                self._known_totals[device_key] = max(core_totals)
            if device_key in self._known_totals and cores_used > self._known_totals[device_key]:
                raise ValueError(f"core_count {cores_used} exceeds num_available_cores {self._known_totals[device_key]} for device {device_key}")
            summary = self._summary(device_key, int(bool(records)), len(records), cores_used)
            self._publish(device_key, summary)
            summaries[device_key] = summary

        for device_key in self.device_keys:
            if device_key in seen_device_keys:
                continue
            summary = self._summary(device_key)
            self._publish(device_key, summary)
            summaries[device_key] = summary

        self.last_failure = None
        self.last_success_timestamp = time.time()
        return summaries

    def _summary(self, device_key: str, active: int = 0, programs: int = 0, cores: int = 0) -> dict[str, int]:
        summary = {
            "active": active,
            "programs_observed": programs,
            "tensix_cores_used": cores,
        }
        if device_key in self._known_totals:
            summary["tensix_cores_total"] = self._known_totals[device_key]
        return summary

    def _handle_failure(self, error: Exception, *, disable: bool = False) -> None:
        self.failure_count += 1
        self.last_failure = type(error).__name__
        if disable:
            self._disabled = True
        if not self._warned:
            self._warning_callback("TT-Metalium telemetry publication disabled or degraded")
            self._warned = True

    def close(self) -> None:
        """Mark previously published devices inactive on a normal process exit."""

        with self._lock:
            if self._closed:
                return
            self._closed = True
            if self._disabled:
                return
            device_keys = self._published_device_keys or set(self.device_keys)

        for device_key in device_keys:
            try:
                self._publish(device_key, self._summary(device_key), allow_closed=True)
            except Exception as error:
                self._handle_failure(error)
                if self.strict:
                    raise

    def _publish(self, device_key: str, summary: Mapping[str, int], *, allow_closed: bool = False) -> None:
        if not device_key or device_key in {".", ".."} or "/" in device_key:
            raise ValueError(f"invalid device key: {device_key!r}")

        with self._lock:
            if self._closed and not allow_closed:
                raise RuntimeError("publisher is closed")
            self._published_device_keys.add(device_key)

            state_dir = self.state_root / device_key
            state_dir.mkdir(parents=True, exist_ok=True)
            target = state_dir / "snapshot.state" if self.layout_version == 2 else state_dir / f"{_safe_component(self.workload_id)}.state"
            temporary = target.with_name(f".{target.name}.{os.getpid()}.{threading.get_ident()}.tmp")

            fields: list[tuple[str, object]] = [
                ("schema_version", self.layout_version),
                ("workload_id", self.workload_id),
                ("sample_timestamp_seconds", int(time.time())),
                ("active", summary["active"]),
                ("programs_observed", summary["programs_observed"]),
                ("tensix_cores_used", summary["tensix_cores_used"]),
            ]
            if "tensix_cores_total" in summary:
                fields.append(("tensix_cores_total", summary["tensix_cores_total"]))
            for key, value in (
                ("pod_namespace", self.pod_namespace),
                ("pod_name", self.pod_name),
                ("container_name", self.container_name),
            ):
                if value:
                    fields.append((key, value))

            payload = "".join(f"{key}={_state_value(value)}\n" for key, value in fields)
            try:
                with temporary.open("w", encoding="utf-8") as state_file:
                    state_file.write(payload)
                    state_file.flush()
                    os.fsync(state_file.fileno())
                os.chmod(temporary, 0o644)
                os.replace(temporary, target)
                directory_fd = os.open(state_dir, os.O_RDONLY | os.O_DIRECTORY)
                try:
                    os.fsync(directory_fd)
                finally:
                    os.close(directory_fd)
            finally:
                temporary.unlink(missing_ok=True)

    def __enter__(self) -> "MetaliumProfilerPublisher":
        return self

    def __exit__(self, _exc_type: object, _exc: object, _traceback: object) -> None:
        self.close()
