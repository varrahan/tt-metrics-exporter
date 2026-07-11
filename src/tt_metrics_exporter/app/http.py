"""Bounded ASGI transport for exporter endpoints."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
from threading import Event
import time
from typing import Callable

from starlette.applications import Starlette
from starlette.requests import Request
from starlette.responses import Response
from starlette.routing import Route
import uvicorn
from uvicorn.protocols.http.h11_impl import H11Protocol

from .logging import Logger, LogLevel
from .runtime import HttpRoute, ReadinessReason, RuntimeStatus


@dataclass(slots=True)
class HttpServerConfig:
    workers: int = 4
    queue_depth: int = 64
    request_deadline: float = 2.0
    maximum_header_bytes: int = 8 * 1024
    shutdown_grace_period: float = 10.0


def _response(status: int, content_type: str, body: str,
              headers: dict[str, str] | None = None) -> Response:
    response_headers = {
        "Content-Type": content_type,
        "Connection": "close",
        "X-Content-Type-Options": "nosniff",
        **(headers or {}),
    }
    return Response(body.encode(), status_code=status, headers=response_headers)


def _route(path: str) -> HttpRoute:
    return {
        "/metrics": HttpRoute.METRICS,
        "/v1/devices": HttpRoute.DEVICES,
        "/healthz": HttpRoute.HEALTH,
        "/readyz": HttpRoute.READY,
    }.get(path, HttpRoute.OTHER)


class _ContractMiddleware:
    def __init__(self, app: object, status: RuntimeStatus | None,
                 maximum_header_bytes: int) -> None:
        self.app, self.status = app, status
        self.maximum_header_bytes = maximum_header_bytes

    async def __call__(self, scope: dict, receive: Callable, send: Callable) -> None:
        if scope["type"] != "http":
            await self.app(scope, receive, send)  # type: ignore[operator]
            return
        started, status_code = time.monotonic(), 500

        async def record(message: dict) -> None:
            nonlocal status_code
            if message["type"] == "http.response.start":
                status_code = message["status"]
            await send(message)

        method, headers = scope["method"], scope["headers"]
        target_bytes = len(scope["raw_path"]) + len(scope["query_string"])
        header_bytes = len(method) + target_bytes + len(scope["http_version"]) + 12
        header_bytes += sum(len(name) + len(value) + 4 for name, value in headers)
        header_map = {name.lower(): value.strip() for name, value in headers}
        if header_bytes > self.maximum_header_bytes:
            response = _response(431, "text/plain", "headers too large\n")
        elif b"transfer-encoding" in header_map or header_map.get(b"content-length", b"0") != b"0":
            response = _response(400, "text/plain", "bad request\n")
        elif method != "GET":
            response = _response(405, "text/plain", "method not allowed\n", {"Allow": "GET"})
        else:
            response = None
        try:
            if response is not None:
                await response(scope, receive, record)
            else:
                await self.app(scope, receive, record)  # type: ignore[operator]
        finally:
            if self.status:
                self.status.record_http_request(
                    _route(scope["path"]), status_code, time.monotonic() - started
                )


def _protocol(status: RuntimeStatus | None, logger: Logger | None,
              config: HttpServerConfig) -> type[H11Protocol]:
    maximum_connections = config.workers + config.queue_depth

    class BoundedH11Protocol(H11Protocol):
        def connection_made(self, transport: asyncio.Transport) -> None:
            if len(self.connections) >= maximum_connections:
                if status:
                    status.connection_rejected()
                if logger:
                    logger.log_rate_limited(
                        LogLevel.WARN, "http.request_rejected",
                        "HTTP connection limit reached", "http:connection_limit",
                    )
                transport.write(
                    b"HTTP/1.1 503 Service Unavailable\r\nContent-Length: 20\r\n"
                    b"Content-Type: text/plain\r\nConnection: close\r\n\r\n"
                    b"service unavailable\n"
                )
                transport.close()
                return
            super().connection_made(transport)
            if status:
                status.connection_opened()
            self._initial_request_timeout = self.loop.call_later(
                config.request_deadline, self._request_timed_out
            )

        def data_received(self, data: bytes) -> None:
            super().data_received(data)
            if self.scope is not None:
                self._cancel_request_timeout()

        def connection_lost(self, exc: Exception | None) -> None:
            self._cancel_request_timeout()
            if status and self in self.connections:
                status.connection_closed()
            super().connection_lost(exc)

        def _cancel_request_timeout(self) -> None:
            timeout = getattr(self, "_initial_request_timeout", None)
            if timeout is not None:
                timeout.cancel()
                self._initial_request_timeout = None

        def _request_timed_out(self) -> None:
            if self.scope is None and not self.transport.is_closing():
                self.transport.write(
                    b"HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n"
                    b"Content-Type: text/plain\r\nConnection: close\r\n\r\n"
                    b"bad request\n"
                )
                self.transport.close()

    return BoundedH11Protocol


class HttpServer:
    def __init__(self, listen_address: str, port: int,
                 metrics_provider: Callable[[], str],
                 devices_provider: Callable[[], str],
                 runtime_status: RuntimeStatus | None = None,
                 logger: Logger | None = None,
                 config: HttpServerConfig | None = None) -> None:
        self.address, self.port = listen_address, port
        self.metrics_provider, self.devices_provider = metrics_provider, devices_provider
        self.runtime_status, self.logger = runtime_status, logger
        self.config = config or HttpServerConfig()
        self._server: uvicorn.Server | None = None

    def _application(self) -> _ContractMiddleware:
        async def metrics(_: Request) -> Response:
            return _response(200, "text/plain; version=0.0.4; charset=utf-8",
                             self.metrics_provider())

        async def devices(_: Request) -> Response:
            return _response(200, "application/json", self.devices_provider())

        async def health(_: Request) -> Response:
            return _response(200, "text/plain", "ok\n")

        async def ready(_: Request) -> Response:
            readiness = (self.runtime_status.readiness() if self.runtime_status
                         else ReadinessReason.INITIAL_COLLECTION)
            is_ready = readiness is ReadinessReason.READY
            body = f'{{"ready":{str(is_ready).lower()},"reason":"{readiness.value}"}}\n'
            return _response(200 if is_ready else 503, "application/json", body)

        async def missing(_: Request) -> Response:
            return _response(404, "text/plain", "not found\n")

        app = Starlette(routes=[
            Route("/metrics", metrics), Route("/v1/devices", devices),
            Route("/healthz", health), Route("/readyz", ready),
            Route("/{path:path}", missing),
        ])
        return _ContractMiddleware(app, self.runtime_status,
                                   self.config.maximum_header_bytes)

    def serve(self, running: Event) -> int:
        if not running.is_set():
            return 0
        configuration = uvicorn.Config(
            self._application(), host=self.address, port=self.port,
            http=_protocol(self.runtime_status, self.logger, self.config),
            ws="none", lifespan="off", access_log=False, log_config=None,
            log_level="critical", backlog=self.config.queue_depth,
            timeout_keep_alive=0,
            timeout_graceful_shutdown=self.config.shutdown_grace_period,
            # Let the contract middleware produce a deterministic 431 for the
            # documented 8 KiB limit while retaining a bounded parser buffer.
            h11_max_incomplete_event_size=max(64 * 1024, self.config.maximum_header_bytes),
        )
        self._server = uvicorn.Server(configuration)
        if self.logger:
            self.logger.log(LogLevel.INFO, "http.listen_started", "HTTP listener started",
                            {"address": self.address, "port": self.port})
        try:
            self._server.run()
        except (OSError, SystemExit):
            if self.logger:
                self.logger.log(LogLevel.ERROR, "http.request_rejected",
                                "listener activation failed")
            return 1
        return 0 if self._server.started else 1

    def stop(self) -> None:
        if self._server:
            self._server.should_exit = True
