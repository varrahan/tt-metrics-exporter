#!/usr/bin/env python3

import http.client
import json
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import tempfile
import time


def reserve_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def request(port: int, path: str) -> tuple[int, bytes]:
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=1)
    try:
        connection.request("GET", path)
        response = connection.getresponse()
        return response.status, response.read()
    finally:
        connection.close()


def wait_for(port: int, path: str, expected: int, timeout: float = 5) -> bytes:
    deadline = time.monotonic() + timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            status, body = request(port, path)
            if status == expected:
                return body
        except (ConnectionError, OSError, http.client.HTTPException) as error:
            last_error = error
        time.sleep(0.05)
    raise AssertionError(f"{path} did not return {expected}; last error: {last_error}")


def test_lifecycle_contract() -> None:
    executable = Path(__file__).parents[1] / "support/run_exporter.py"
    invalid = subprocess.run(
        [
            executable,
            "--poll-interval",
            "5",
            "--max-snapshot-age",
            "5",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    assert invalid.returncode == 2

    with tempfile.TemporaryDirectory(prefix="tt-exporter-lifecycle-") as temp:
        sysfs_root = Path(temp) / "sysfs"
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
                "3",
                "--shutdown-grace-period",
                "3",
                "--require-device",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        stalled = None
        try:
            assert wait_for(port, "/healthz", 200) == b"ok\n"
            body = json.loads(wait_for(port, "/readyz", 503))
            assert body == {"ready": False, "reason": "initial_collection"}

            (sysfs_root / "0").mkdir(parents=True)
            body = json.loads(wait_for(port, "/readyz", 200))
            assert body == {"ready": True, "reason": "ready"}
            status, devices = request(port, "/v1/devices")
            assert status == 200
            assert json.loads(devices)["devices"][0]["id"] == "0"

            shutil.rmtree(sysfs_root)
            body = json.loads(wait_for(port, "/readyz", 503))
            assert body == {"ready": False, "reason": "critical_source"}
            status, retained = request(port, "/v1/devices")
            assert status == 200
            assert json.loads(retained)["devices"][0]["id"] == "0"

            stalled = socket.create_connection(("127.0.0.1", port), timeout=1)
            started = time.monotonic()
            process.send_signal(signal.SIGTERM)
            process.wait(timeout=3)
            assert time.monotonic() - started < 3
            assert process.returncode == 0
        finally:
            if stalled is not None:
                stalled.close()
            if process.poll() is None:
                process.kill()
                process.wait()
