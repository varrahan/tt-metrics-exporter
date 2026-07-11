"""Bounded operational logging with stable text and JSON contracts."""

from __future__ import annotations

from datetime import datetime, timezone
from enum import IntEnum
import json
import re
import sys
from threading import Lock
import time
from typing import Callable, TextIO

import structlog


class LogLevel(IntEnum):
    DEBUG = 0
    INFO = 1
    WARN = 2
    ERROR = 3


_FIELD = re.compile(r"^[A-Za-z0-9_]{1,32}$")


def _bounded(value: object, maximum: int) -> str:
    return str(value).encode()[:maximum].decode(errors="ignore")


class Logger:
    def __init__(self, log_format: str = "text", minimum_level: LogLevel = LogLevel.INFO,
                 output: TextIO = sys.stderr,
                 monotonic: Callable[[], float] = time.monotonic,
                 wall_time: Callable[[], float] = time.time) -> None:
        self._minimum, self._monotonic, self._wall_time = minimum_level, monotonic, wall_time
        self._lock, self._last_emitted = Lock(), {}
        renderer = (
            structlog.processors.JSONRenderer(
                serializer=lambda value, **_: json.dumps(
                    value, ensure_ascii=False, separators=(",", ":")
                )
            )
            if log_format == "json" else self._render_text
        )
        self._logger = structlog.wrap_logger(
            structlog.PrintLogger(output), processors=[self._contract_fields, renderer]
        )

    def _contract_fields(self, _: object, __: str,
                         values: dict[str, object]) -> dict[str, object]:
        instant = datetime.fromtimestamp(self._wall_time(), timezone.utc)
        timestamp = instant.strftime("%Y-%m-%dT%H:%M:%S.") + f"{instant.microsecond // 1000:03d}Z"
        return {
            "timestamp": timestamp,
            "severity": values.pop("severity"),
            "event": _bounded(values.pop("event_name"), 64),
            "message": _bounded(values.pop("event"), 512),
            **{
                name: _bounded(value, 256)
                for name, value in values.items() if _FIELD.fullmatch(name)
            },
        }

    @staticmethod
    def _render_text(_: object, __: str, values: dict[str, object]) -> str:
        def escape(value: object) -> str:
            return str(value).replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")

        head = " ".join(escape(values.pop(name)) for name in
                        ("timestamp", "severity", "event", "message"))
        return head + "".join(f" {name}={escape(value)}" for name, value in values.items())

    def log(self, level: LogLevel, event: str, message: str,
            fields: dict[str, object] | None = None) -> None:
        if level < self._minimum:
            return
        with self._lock:
            self._logger.msg(message, severity=level.name.lower(), event_name=event,
                             **(fields or {}))

    def log_rate_limited(self, level: LogLevel, event: str, message: str,
                         bounded_key: str, interval: float = 60,
                         fields: dict[str, object] | None = None) -> None:
        if level < self._minimum or not 0 < len(bounded_key.encode()) <= 64:
            return
        now = self._monotonic()
        with self._lock:
            previous = self._last_emitted.get(bounded_key)
            if previous is not None and now - previous < interval:
                return
            if previous is None and len(self._last_emitted) >= 64:
                return
            self._last_emitted[bounded_key] = now
            self._logger.msg(message, severity=level.name.lower(), event_name=event,
                             **(fields or {}))
