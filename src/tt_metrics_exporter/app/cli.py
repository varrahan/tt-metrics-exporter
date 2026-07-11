"""Python exporter command line and service lifecycle."""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import signal
import sys
from threading import Event, Thread
import time

import click

from .._version import __version__ as VERSION
from ..collection import SysfsCollector
from ..models import CollectorConfig
from ..renderers import render_devices_json, render_prometheus
from .http import HttpServer, HttpServerConfig
from .logging import Logger, LogLevel
from .runtime import ReadinessReason, RuntimeStatus
from .snapshot import SnapshotStore


REVISION = os.environ.get("TT_EXPORTER_REVISION", "unknown")
BUILD_TIME = os.environ.get("TT_EXPORTER_BUILD_TIME", "unknown")


@dataclass(slots=True)
class Options:
    listen_address: str = "0.0.0.0"
    port: int = 9400
    poll_interval: int = 5
    sysfs_root: Path = Path("/sys/class/tenstorrent")
    allocation_state_root: Path | None = None
    janitor_state_root: Path | None = None
    metalium_profiler_state_root: Path | None = None
    metalium_profiler_stale_after: int = 15
    max_snapshot_age: int = 15
    shutdown_grace_period: int = 10
    http_request_deadline: int = 2
    http_workers: int = 4
    http_queue_depth: int = 64
    maximum_rendered_payload_bytes: int = 16 * 1024 * 1024
    log_format: str = "text"
    log_level: LogLevel = LogLevel.INFO
    once: bool = False
    json: bool = False
    collect_pcie_counters: bool = False
    require_device: bool = False
    version: bool = False


_PATH = click.Path(path_type=Path)
_POSITIVE = click.IntRange(min=1)


@click.command(add_help_option=False)
@click.option("--sysfs-root", type=_PATH, default=Path("/sys/class/tenstorrent"))
@click.option("--allocation-state-root", type=_PATH)
@click.option("--janitor-state-root", type=_PATH)
@click.option("--metalium-profiler-state-root", type=_PATH)
@click.option("--listen-address", default="0.0.0.0")
@click.option("--port", type=click.IntRange(1, 65535), default=9400)
@click.option("--poll-interval", type=_POSITIVE, default=5)
@click.option("--metalium-profiler-stale-after", type=_POSITIVE, default=15)
@click.option("--max-snapshot-age", type=_POSITIVE, default=15)
@click.option("--shutdown-grace-period", type=_POSITIVE, default=10)
@click.option("--http-request-deadline", type=_POSITIVE, default=2)
@click.option("--http-workers", type=click.IntRange(1, 256), default=4)
@click.option("--http-queue-depth", type=click.IntRange(1, 65535), default=64)
@click.option("--maximum-rendered-payload-bytes",
              type=click.IntRange(1024, 1024 ** 3), default=16 * 1024 * 1024)
@click.option("--log-format", type=click.Choice(("text", "json")), default="text")
@click.option("--log-level", type=click.Choice(("error", "warn", "info", "debug")),
              default="info")
@click.option("--once", is_flag=True)
@click.option("--json", "json_output", is_flag=True)
@click.option("--collect-pcie-counters", is_flag=True)
@click.option("--require-device", is_flag=True)
@click.option("--version", "show_version", is_flag=True)
def _parse_options(**values: object) -> Options:
    values["json"] = values.pop("json_output")
    values["version"] = values.pop("show_version")
    values["log_level"] = LogLevel[str(values["log_level"]).upper()]
    return Options(**values)  # type: ignore[arg-type]


def _usage(program: str) -> str:
    context = click.Context(_parse_options, info_name=program)
    return _parse_options.get_help(context) + "\n"


def parse_args(arguments: list[str]) -> tuple[Options, str | None]:
    if "--help" in arguments or "-h" in arguments:
        return Options(), "help"
    try:
        options = _parse_options.main(args=arguments, standalone_mode=False)
    except click.ClickException as error:
        return Options(), error.format_message()
    if not options.once and not options.version and options.max_snapshot_age <= options.poll_interval:
        return options, "--max-snapshot-age must be greater than --poll-interval"
    return options, None


class ExporterService:
    def __init__(self, options: Options, logger: Logger) -> None:
        self.options, self.logger = options, logger
        self.running, self.snapshots = Event(), SnapshotStore()
        self.running.set()
        self._shutdown_requested = False
        self.status = RuntimeStatus(VERSION, REVISION, options.max_snapshot_age,
                                    options.require_device)
        self.collector = SysfsCollector(CollectorConfig(
            options.sysfs_root, options.allocation_state_root,
            options.janitor_state_root, options.metalium_profiler_state_root,
            options.metalium_profiler_stale_after, options.collect_pcie_counters,
        ))
        self.previous_readiness = ReadinessReason.INITIAL_COLLECTION
        self.server = HttpServer(
            options.listen_address, options.port, self.metrics, self.devices,
            self.status, logger, HttpServerConfig(
                options.http_workers, options.http_queue_depth,
                options.http_request_deadline,
                shutdown_grace_period=options.shutdown_grace_period,
            ),
        )

    def metrics(self) -> str:
        snapshot = self.snapshots.load()
        return ("" if snapshot is None else snapshot.prometheus) + self.status.render_prometheus()

    def devices(self) -> str:
        snapshot = self.snapshots.load()
        return "[]\n" if snapshot is None else snapshot.devices_json

    def refresh(self) -> None:
        started = time.monotonic()
        collection = self.collector.collect()
        published = False
        if collection.critical_sources_ok:
            try:
                prometheus, devices_json = (render_prometheus(collection.devices),
                                             render_devices_json(collection.devices))
                limit = self.options.maximum_rendered_payload_bytes
                if len(prometheus.encode()) <= limit and len(devices_json.encode()) <= limit:
                    snapshot = self.snapshots.publish(collection, prometheus, devices_json,
                                                      started, time.time())
                    published = True
            except (MemoryError, ValueError, OverflowError):
                published = False
        duration = time.monotonic() - started
        if published:
            self.status.record_collection(snapshot.collection, duration, True, snapshot.generation)
            self.logger.log(LogLevel.DEBUG, "collector.completed", "collection completed",
                            {"generation": snapshot.generation,
                             "duration_ms": int(duration * 1000),
                             "count": len(snapshot.collection.devices)})
            diagnostics = snapshot.collection.sources
        else:
            self.status.record_collection(collection, duration, False, 0)
            reason = "render_failed" if collection.critical_sources_ok else "critical_source"
            self.logger.log_rate_limited(LogLevel.WARN, "collector.failed",
                                         "collection did not publish a complete snapshot",
                                         f"collector:{reason}", fields={"status": reason})
            diagnostics = collection.sources
        for source, source_diagnostics in diagnostics.items():
            if source_diagnostics.configured and not source_diagnostics.accessible:
                self.logger.log_rate_limited(LogLevel.WARN, "collector.source_degraded",
                                             "configured telemetry source is inaccessible",
                                             f"source:{source.value}", fields={"source": source.value})
            for issue, count in source_diagnostics.issues.items():
                if count:
                    self.logger.log_rate_limited(LogLevel.WARN, "state.record_rejected",
                                                 "telemetry input was rejected",
                                                 f"issue:{source.value}:{issue.value}",
                                                 fields={"source": source.value,
                                                         "status": issue.value, "count": count})
        readiness = self.status.readiness()
        if readiness is not self.previous_readiness:
            if readiness is ReadinessReason.READY:
                self.logger.log(LogLevel.INFO, "exporter.ready", "exporter is ready")
            else:
                self.logger.log(LogLevel.WARN, "exporter.not_ready", "exporter is not ready",
                                {"status": readiness.value})
            self.previous_readiness = readiness

    def request_shutdown(self, *_: object) -> None:
        self._shutdown_requested = True

    def shutdown(self) -> None:
        if not self.running.is_set():
            return
        self.logger.log(LogLevel.INFO, "exporter.shutdown_started", "exporter shutdown started")
        self.status.begin_shutdown()
        self.running.clear()
        self.server.stop()

    def run(self) -> int:
        signal.signal(signal.SIGINT, self.request_shutdown)
        signal.signal(signal.SIGTERM, self.request_shutdown)
        self.logger.log(LogLevel.INFO, "exporter.starting", "Tenstorrent metrics exporter starting",
                        {"address": self.options.listen_address, "port": self.options.port,
                         "version": VERSION, "revision": REVISION})
        self.refresh()

        def poll() -> None:
            deadline = time.monotonic() + self.options.poll_interval
            while self.running.is_set():
                remaining = deadline - time.monotonic()
                if remaining > 0:
                    time.sleep(min(.1, remaining))
                    continue
                self.refresh()
                deadline = time.monotonic() + self.options.poll_interval

        poller = Thread(target=poll)
        poller.start()

        def coordinate_shutdown() -> None:
            while self.running.is_set() and not self._shutdown_requested:
                time.sleep(.05)
            if self._shutdown_requested:
                self.shutdown()

        coordinator = Thread(target=coordinate_shutdown)
        coordinator.start()
        result = self.server.serve(self.running)
        self.shutdown()
        poller.join(timeout=self.options.shutdown_grace_period)
        coordinator.join(timeout=self.options.shutdown_grace_period)
        if poller.is_alive() or coordinator.is_alive():
            os._exit(1)
        self.logger.log(LogLevel.INFO, "exporter.shutdown_completed", "exporter shutdown completed")
        return result


def main(arguments: list[str] | None = None) -> int:
    arguments = sys.argv[1:] if arguments is None else arguments
    options, error = parse_args(arguments)
    if error == "help":
        sys.stderr.write(_usage(Path(sys.argv[0]).name))
        return 0
    logger = Logger(options.log_format, options.log_level)
    if error:
        logger.log(LogLevel.ERROR, "exporter.config_invalid", error)
        return 2
    if options.version:
        print(f"tt-metrics-exporter {VERSION} revision={REVISION} built={BUILD_TIME}")
        return 0
    collector = SysfsCollector(CollectorConfig(
        options.sysfs_root, options.allocation_state_root, options.janitor_state_root,
        options.metalium_profiler_state_root, options.metalium_profiler_stale_after,
        options.collect_pcie_counters,
    ))
    if options.once:
        result = collector.collect()
        sys.stdout.write(render_devices_json(result.devices) if options.json
                         else render_prometheus(result.devices))
        return 0 if result.critical_sources_ok else 1
    return ExporterService(options, logger).run()
