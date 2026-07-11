#!/usr/bin/env python3
"""Unit contracts for runtime status and immutable snapshots."""

from dataclasses import dataclass
import unittest

from tt_metrics_exporter.models import (
    CollectionIssue,
    CollectionResult,
    DeviceTelemetry,
    TelemetrySource,
)
from tt_metrics_exporter.app.runtime import HttpRoute, ReadinessReason, RuntimeStatus
from tt_metrics_exporter.app.snapshot import SnapshotStore


@dataclass
class Clock:
    steady: float = 0.0
    wall: float = 0.0

    def advance(self, seconds: float) -> None:
        self.steady += seconds
        self.wall += seconds


def successful_result(devices: int = 1) -> CollectionResult:
    result = CollectionResult(devices=[DeviceTelemetry() for _ in range(devices)])
    result.sources[TelemetrySource.SYSFS_ROOT].configured = True
    result.sources[TelemetrySource.SYSFS_ROOT].accessible = True
    result.critical_sources_ok = True
    return result


class SnapshotTest(unittest.TestCase):
    def test_generations_are_retained_and_inputs_are_copied(self) -> None:
        store = SnapshotStore()
        self.assertIsNone(store.load())
        result = successful_result()
        result.devices[0].id = "one"
        first = store.publish(result, "metric 1\n", "{}\n", 1, 2)
        result.devices[0].id = "mutated"
        self.assertEqual(first.generation, 1)
        self.assertEqual(first.collection.devices[0].id, "one")
        second = store.publish(successful_result(), "metric 2\n", "{}\n", 3, 4)
        self.assertEqual(second.generation, 2)
        self.assertIs(store.load(), second)
        self.assertEqual(first.generation, 1)


class RuntimeTest(unittest.TestCase):
    def test_readiness_failure_staleness_recovery_and_shutdown(self) -> None:
        clock = Clock()
        status = RuntimeStatus("1.2.3", "abc", 15, monotonic=lambda: clock.steady,
                               wall_time=lambda: clock.wall)
        self.assertIs(status.readiness(), ReadinessReason.INITIAL_COLLECTION)
        success = successful_result()
        status.record_collection(success, .025, True, 1)
        self.assertIs(status.readiness(), ReadinessReason.READY)
        clock.advance(5)
        failure = successful_result()
        failure.critical_sources_ok = False
        failure.sources[TelemetrySource.SYSFS_ROOT].accessible = False
        failure.sources[TelemetrySource.SYSFS_ROOT].issues[CollectionIssue.PERMISSION_DENIED] = 1
        status.record_collection(failure, .01, False, 0)
        self.assertIs(status.readiness(), ReadinessReason.CRITICAL_SOURCE)
        self.assertEqual(status.snapshot().generation, 1)
        status.record_collection(success, .02, True, 2)
        self.assertIs(status.readiness(), ReadinessReason.READY)
        self.assertEqual(status.snapshot().issue_totals[CollectionIssue.PERMISSION_DENIED], 1)
        clock.advance(16)
        self.assertIs(status.readiness(), ReadinessReason.SNAPSHOT_STALE)
        status.begin_shutdown()
        self.assertIs(status.readiness(), ReadinessReason.SHUTTING_DOWN)

    def test_device_requirement_and_bounded_self_metrics(self) -> None:
        clock = Clock()
        status = RuntimeStatus('1"2', "rev\\1", 15, True,
                               lambda: clock.steady, lambda: clock.wall)
        status.record_collection(successful_result(0), .002, True, 1)
        self.assertIs(status.readiness(), ReadinessReason.DEVICE_REQUIRED)
        status.connection_opened()
        status.record_http_request(HttpRoute.METRICS, 200, .003)
        status.record_http_request(HttpRoute.OTHER, 404, .004)
        status.connection_closed()
        metrics = status.render_prometheus()
        self.assertIn('version="1\\"2"', metrics)
        self.assertIn('revision="rev\\\\1"', metrics)
        self.assertIn('route="metrics",code_class="2xx"} 1', metrics)
        self.assertIn('route="other",code_class="4xx"} 1', metrics)
        self.assertNotIn("/tmp/", metrics)


if __name__ == "__main__":
    unittest.main()
