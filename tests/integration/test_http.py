#!/usr/bin/env python3

import http.client
import json
from pathlib import Path
import re
import signal
import socket
import subprocess
import tempfile
import time


def reserve_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def request(port: int, path: str) -> tuple[int, bytes, dict[str, str]]:
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
    try:
        connection.request("GET", path)
        response = connection.getresponse()
        return response.status, response.read(), {name.lower(): value for name, value in response.getheaders()}
    finally:
        connection.close()


def raw_request(port: int, payload: bytes, bytewise: bool = False) -> bytes:
    with socket.create_connection(("127.0.0.1", port), timeout=2) as client:
        client.settimeout(3)
        if bytewise:
            for byte in payload:
                client.sendall(bytes([byte]))
        else:
            client.sendall(payload)
        chunks = []
        while True:
            chunk = client.recv(65536)
            if not chunk:
                return b"".join(chunks)
            chunks.append(chunk)


def status_code(response: bytes) -> int:
    return int(response.split(b" ", 2)[1])


def wait_status(port: int, path: str, expected: int, timeout: float = 6) -> bytes:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            status, body, _ = request(port, path)
            if status == expected:
                return body
        except (OSError, http.client.HTTPException):
            pass
        time.sleep(0.05)
    raise AssertionError(f"{path} did not return {expected}")


def test_http_server_contract() -> None:
    executable = Path(__file__).parents[1] / "support/run_exporter.py"
    with tempfile.TemporaryDirectory(prefix="tt-exporter-http-") as temp:
        sysfs_root = Path(temp) / "sysfs"
        device = sysfs_root / "0"
        device.mkdir(parents=True)
        port = reserve_port()
        process = subprocess.Popen(
            [
                executable,
                "--sysfs-root",
                sysfs_root,
                "--listen-address",
                "127.0.0.1",
                "--port",
                str(port),
                "--poll-interval",
                "1",
                "--max-snapshot-age",
                "2",
                "--shutdown-grace-period",
                "3",
                "--http-workers",
                "2",
                "--http-queue-depth",
                "2",
                "--http-request-deadline",
                "1",
                "--maximum-rendered-payload-bytes",
                "100000",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        sockets: list[socket.socket] = []
        try:
            wait_status(port, "/readyz", 200)
            status, body, headers = request(port, "/healthz?probe=1")
            assert status == 200 and body == b"ok\n"
            assert headers["content-length"] == "3"
            assert headers["connection"] == "close"
            assert headers["x-content-type-options"] == "nosniff"

            assert status_code(raw_request(port, b"GET /healthz HTTP/1.1\r\nHost: localhost\r\n\r\n", True)) == 200
            assert status_code(raw_request(port, b"POST /healthz HTTP/1.1\r\nHost: localhost\r\n\r\n")) == 405
            method_response = raw_request(port, b"HEAD /healthz HTTP/1.1\r\nHost: localhost\r\n\r\n")
            assert status_code(method_response) == 405
            assert b"allow: GET\r\n" in method_response
            assert status_code(raw_request(port, b"not-http\r\n\r\n")) == 400
            assert (
                status_code(
                    raw_request(
                        port,
                        b"GET /healthz HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1\r\n\r\nx",
                    )
                )
                == 400
            )
            assert status_code(raw_request(port, b"GET /healthz/ HTTP/1.1\r\nHost: localhost\r\n\r\n")) == 404
            oversized = b"GET /healthz HTTP/1.1\r\nHost: localhost\r\nX-Large: " + b"a" * 9000 + b"\r\n\r\n"
            assert status_code(raw_request(port, oversized)) == 431

            # One silent client consumes one worker; the other still serves.
            stalled = socket.create_connection(("127.0.0.1", port), timeout=1)
            sockets.append(stalled)
            started = time.monotonic()
            assert request(port, "/healthz")[0] == 200
            assert time.monotonic() - started < 0.5
            stalled.close()
            sockets.remove(stalled)

            unterminated = socket.create_connection(("127.0.0.1", port), timeout=1)
            unterminated.settimeout(3)
            unterminated.sendall(b"GET /healthz HTTP/1.1\r\nHost: localhost")
            timeout_response = unterminated.recv(4096)
            assert status_code(timeout_response) == 400
            unterminated.close()

            # Disconnecting during a large response must not terminate a worker.
            disconnected = socket.create_connection(("127.0.0.1", port), timeout=1)
            disconnected.sendall(b"GET /metrics HTTP/1.1\r\nHost: x\r\n\r\n")
            disconnected.close()
            assert request(port, "/healthz")[0] == 200

            # Saturate workers and the bounded queue; excess accepts are rejected.
            for _ in range(10):
                try:
                    client = socket.create_connection(("127.0.0.1", port), timeout=0.5)
                    sockets.append(client)
                except OSError:
                    pass
            time.sleep(1.5)
            for client in sockets:
                client.close()
            sockets.clear()
            metrics = request(port, "/metrics")[1].decode()
            match = re.search(
                r"^tt_exporter_http_connections_rejected_total (\d+)$",
                metrics,
                re.MULTILINE,
            )
            assert match and int(match.group(1)) >= 1

            # Repeated requests leave only the fixed listener/worker footprint.
            fd_path = Path(f"/proc/{process.pid}/fd")
            before = len(list(fd_path.iterdir()))
            for _ in range(100):
                assert request(port, "/healthz")[0] == 200
            time.sleep(0.1)
            after = len(list(fd_path.iterdir()))
            assert after <= before + 2

            # Oversized rendering retains the old payload and expires readiness.
            (device / "board_type").write_text("x" * 60000)
            stale_body = json.loads(wait_status(port, "/readyz", 503, timeout=6))
            assert stale_body == {"ready": False, "reason": "snapshot_stale"}
            retained = json.loads(request(port, "/v1/devices")[1])
            assert retained["devices"][0]["boardType"] is None
            (device / "board_type").unlink()
            assert json.loads(wait_status(port, "/readyz", 200)) == {
                "ready": True,
                "reason": "ready",
            }

            process.send_signal(signal.SIGTERM)
            process.wait(timeout=3)
            assert process.returncode == 0
        finally:
            for client in sockets:
                client.close()
            if process.poll() is None:
                process.kill()
                process.wait()
