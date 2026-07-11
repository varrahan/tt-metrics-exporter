#!/usr/bin/env python3

import http.client
import json
from pathlib import Path
import signal
import socket
import subprocess
import tempfile
import time


def reserve_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def wait_for_health(port: int) -> None:
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        try:
            connection = http.client.HTTPConnection("127.0.0.1", port, timeout=1)
            connection.request("GET", "/healthz")
            response = connection.getresponse()
            response.read()
            connection.close()
            if response.status == 200:
                return
        except OSError:
            pass
        time.sleep(0.05)
    raise AssertionError("exporter did not become live")


def parse_json_lines(data: bytes) -> list[dict[str, object]]:
    lines = data.decode().splitlines()
    assert lines
    records = [json.loads(line) for line in lines]
    for record in records:
        assert {"timestamp", "severity", "event", "message"} <= record.keys()
    return records


def test_logging_process_contract() -> None:
    executable = Path(__file__).parents[1] / "support/run_exporter.py"
    invalid = subprocess.run(
        [
            executable,
            "--log-format",
            "json",
            "--poll-interval",
            "5",
            "--max-snapshot-age",
            "5",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        check=False,
    )
    assert invalid.returncode == 2
    assert parse_json_lines(invalid.stderr)[0]["event"] == "exporter.config_invalid"

    with tempfile.TemporaryDirectory(prefix="tt-exporter-logging-") as temp:
        root = Path(temp) / "sysfs"
        profiler = Path(temp) / "profiler"
        (root / "0").mkdir(parents=True)
        (profiler / "0").mkdir(parents=True)
        (profiler / "0" / "bad.state").write_text("schema_version=999\nworkload_id=do-not-log-this\n")
        port = reserve_port()
        process = subprocess.Popen(
            [
                executable,
                "--sysfs-root",
                root,
                "--metalium-profiler-state-root",
                profiler,
                "--listen-address",
                "127.0.0.1",
                "--port",
                str(port),
                "--poll-interval",
                "1",
                "--max-snapshot-age",
                "3",
                "--log-format",
                "json",
                "--log-level",
                "debug",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        wait_for_health(port)
        process.send_signal(signal.SIGTERM)
        _, stderr = process.communicate(timeout=5)
        assert process.returncode == 0
        records = parse_json_lines(stderr)
        events = {record["event"] for record in records}
        assert {
            "exporter.starting",
            "exporter.ready",
            "collector.completed",
            "http.listen_started",
            "state.record_rejected",
            "exporter.shutdown_started",
            "exporter.shutdown_completed",
        } <= events
        text = stderr.decode()
        assert "schema_version=" not in text
        assert "do-not-log-this" not in text

        quiet_port = reserve_port()
        quiet = subprocess.Popen(
            [
                executable,
                "--sysfs-root",
                root,
                "--listen-address",
                "127.0.0.1",
                "--port",
                str(quiet_port),
                "--log-format",
                "json",
                "--log-level",
                "error",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        wait_for_health(quiet_port)
        quiet.send_signal(signal.SIGTERM)
        _, quiet_stderr = quiet.communicate(timeout=5)
        assert quiet.returncode == 0
        assert quiet_stderr == b""
