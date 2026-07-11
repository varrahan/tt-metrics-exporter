"""Readiness and bounded exporter self-observability state."""

from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass
from enum import Enum
from threading import Lock
import time
from typing import Callable

from ..models import (
    CollectionIssue,
    CollectionResult,
    SourceDiagnostics,
    TelemetrySource,
)


class HttpRoute(str, Enum):
    METRICS = "metrics"
    DEVICES = "devices"
    HEALTH = "health"
    READY = "ready"
    OTHER = "other"


class ReadinessReason(str, Enum):
    READY = "ready"
    INITIAL_COLLECTION = "initial_collection"
    CRITICAL_SOURCE = "critical_source"
    SNAPSHOT_STALE = "snapshot_stale"
    DEVICE_REQUIRED = "device_required"
    SHUTTING_DOWN = "shutting_down"


@dataclass(frozen=True, slots=True)
class RuntimeStatusSnapshot:
    collection_attempts: int
    successful_collections: int
    failed_collections: int
    consecutive_critical_failures: int
    generation: int
    device_count: int
    shutting_down: bool
    issue_totals: dict[CollectionIssue, int]


class RuntimeStatus:
    def __init__(
        self,
        version: str,
        revision: str,
        max_snapshot_age: float,
        require_device: bool = False,
        monotonic: Callable[[], float] = time.monotonic,
        wall_time: Callable[[], float] = time.time,
    ) -> None:
        self._version, self._revision = version, revision
        self._max_snapshot_age, self._require_device = max_snapshot_age, require_device
        self._monotonic, self._wall_time = monotonic, wall_time
        self._process_start = monotonic()
        self._lock = Lock()
        self._last_success: float | None = None
        self._last_success_wall: float | None = None
        self._last_collection_duration = 0.0
        self._successes = self._failures = self._consecutive_failures = 0
        self._generation = self._device_count = 0
        self._shutting_down = False
        self._sources = {source: SourceDiagnostics(source=source) for source in TelemetrySource}
        self._issue_totals = {source: {issue: 0 for issue in CollectionIssue} for source in TelemetrySource}
        self._http_requests = {route: {code: 0 for code in ("2xx", "4xx", "5xx")} for route in HttpRoute}
        self._http_durations = {route: 0.0 for route in HttpRoute}
        self._active_connections = self._rejected_connections = 0

    def record_collection(self, result: CollectionResult, duration: float, snapshot_published: bool, generation: int) -> None:
        steady_now, wall_now = self._monotonic(), self._wall_time()
        with self._lock:
            self._last_collection_duration = duration
            self._sources = deepcopy(result.sources)
            for source, diagnostics in result.sources.items():
                for issue, count in diagnostics.issues.items():
                    self._issue_totals[source][issue] += count
            if result.critical_sources_ok and snapshot_published:
                self._successes += 1
                self._consecutive_failures = 0
                self._last_success, self._last_success_wall = steady_now, wall_now
                self._generation, self._device_count = generation, len(result.devices)
            else:
                self._failures += 1
                self._consecutive_failures += 1

    def record_http_request(self, route: HttpRoute, status_code: int, duration: float) -> None:
        code = "5xx" if status_code >= 500 else "4xx" if status_code >= 400 else "2xx"
        with self._lock:
            self._http_requests[route][code] += 1
            self._http_durations[route] = duration

    def connection_opened(self) -> None:
        with self._lock:
            self._active_connections += 1

    def connection_closed(self) -> None:
        with self._lock:
            self._active_connections = max(0, self._active_connections - 1)

    def connection_rejected(self) -> None:
        with self._lock:
            self._rejected_connections += 1

    def begin_shutdown(self) -> None:
        with self._lock:
            self._shutting_down = True

    def _readiness(self, now: float) -> ReadinessReason:
        if self._shutting_down:
            return ReadinessReason.SHUTTING_DOWN
        if self._last_success is None or self._generation == 0:
            return ReadinessReason.INITIAL_COLLECTION
        if not self._sources[TelemetrySource.SYSFS_ROOT].accessible:
            return ReadinessReason.CRITICAL_SOURCE
        if now - self._last_success > self._max_snapshot_age:
            return ReadinessReason.SNAPSHOT_STALE
        if self._require_device and self._device_count == 0:
            return ReadinessReason.DEVICE_REQUIRED
        return ReadinessReason.READY

    def readiness(self) -> ReadinessReason:
        now = self._monotonic()
        with self._lock:
            return self._readiness(now)

    def snapshot(self) -> RuntimeStatusSnapshot:
        with self._lock:
            totals = {issue: sum(source[issue] for source in self._issue_totals.values()) for issue in CollectionIssue}
            return RuntimeStatusSnapshot(
                self._successes + self._failures,
                self._successes,
                self._failures,
                self._consecutive_failures,
                self._generation,
                self._device_count,
                self._shutting_down,
                totals,
            )

    @staticmethod
    def _number(value: float) -> str:
        return format(value, ".9g")

    @staticmethod
    def _escape(value: str) -> str:
        return value.replace("\\", "\\\\").replace("\n", "\\n").replace('"', '\\"')

    def render_prometheus(self) -> str:
        now = self._monotonic()
        with self._lock:
            lines = [
                "# HELP tt_exporter_build_info Exporter build information.",
                "# TYPE tt_exporter_build_info gauge",
                f'tt_exporter_build_info{{version="{self._escape(self._version)}",revision="{self._escape(self._revision)}"}} 1',
                "# HELP tt_exporter_uptime_seconds Seconds since process start.",
                "# TYPE tt_exporter_uptime_seconds gauge",
                f"tt_exporter_uptime_seconds {self._number(now - self._process_start)}",
                "# HELP tt_exporter_ready Whether the exporter is ready.",
                "# TYPE tt_exporter_ready gauge",
                f"tt_exporter_ready {int(self._readiness(now) is ReadinessReason.READY)}",
                "# HELP tt_exporter_collection_attempts_total Collection attempts by result.",
                "# TYPE tt_exporter_collection_attempts_total counter",
                f'tt_exporter_collection_attempts_total{{result="success"}} {self._successes}',
                f'tt_exporter_collection_attempts_total{{result="failure"}} {self._failures}',
                "# HELP tt_exporter_collection_duration_seconds Last collection duration.",
                "# TYPE tt_exporter_collection_duration_seconds gauge",
                f"tt_exporter_collection_duration_seconds {self._number(self._last_collection_duration)}",
                "# HELP tt_exporter_last_success_timestamp_seconds Unix timestamp of the last successful collection.",
                "# TYPE tt_exporter_last_success_timestamp_seconds gauge",
                f"tt_exporter_last_success_timestamp_seconds {self._number(self._last_success_wall or 0)}",
                "# HELP tt_exporter_snapshot_age_seconds Age of the current complete snapshot.",
                "# TYPE tt_exporter_snapshot_age_seconds gauge",
                f"tt_exporter_snapshot_age_seconds {self._number(max(0.0, now - self._last_success) if self._last_success is not None else 0)}",
                "# HELP tt_exporter_source_accessible Whether a configured source root is accessible.",
                "# TYPE tt_exporter_source_accessible gauge",
            ]
            lines += [f'tt_exporter_source_accessible{{source="{source.value}"}} {int(self._sources[source].accessible)}' for source in TelemetrySource]
            lines += [
                "# HELP tt_exporter_collection_issues_total Collection issues by bounded source and reason.",
                "# TYPE tt_exporter_collection_issues_total counter",
            ]
            lines += [f'tt_exporter_collection_issues_total{{source="{source.value}",reason="{issue.value}"}} {self._issue_totals[source][issue]}' for source in TelemetrySource for issue in CollectionIssue]
            lines += [
                "# HELP tt_exporter_state_records Current state records by source and status.",
                "# TYPE tt_exporter_state_records gauge",
            ]
            for source in TelemetrySource:
                diagnostics = self._sources[source]
                rejected = sum(count for issue, count in diagnostics.issues.items() if issue is not CollectionIssue.STALE_RECORD)
                lines += [
                    f'tt_exporter_state_records{{source="{source.value}",status="accepted"}} {diagnostics.records_accepted}',
                    f'tt_exporter_state_records{{source="{source.value}",status="rejected"}} {rejected}',
                ]
            lines += [
                "# HELP tt_exporter_http_requests_total HTTP requests by bounded route and status class.",
                "# TYPE tt_exporter_http_requests_total counter",
            ]
            lines += [f'tt_exporter_http_requests_total{{route="{route.value}",code_class="{code}"}} {self._http_requests[route][code]}' for route in HttpRoute for code in ("2xx", "4xx", "5xx")]
            lines += [
                "# HELP tt_exporter_http_request_duration_seconds Last HTTP request duration by route.",
                "# TYPE tt_exporter_http_request_duration_seconds gauge",
            ]
            lines += [f'tt_exporter_http_request_duration_seconds{{route="{route.value}"}} {self._number(self._http_durations[route])}' for route in HttpRoute]
            lines += [
                "# HELP tt_exporter_http_connections_active Active HTTP connections.",
                "# TYPE tt_exporter_http_connections_active gauge",
                f"tt_exporter_http_connections_active {self._active_connections}",
                "# HELP tt_exporter_http_connections_rejected_total Rejected HTTP connections.",
                "# TYPE tt_exporter_http_connections_rejected_total counter",
                f"tt_exporter_http_connections_rejected_total {self._rejected_connections}",
            ]
            return "\n".join(lines) + "\n"
