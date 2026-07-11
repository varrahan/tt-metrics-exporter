#!/usr/bin/env python3

from pathlib import Path
import subprocess
import tempfile
import unittest

from tt_metrics_exporter import SysfsCollector
from tt_metrics_exporter.models import CollectorConfig
from tt_metrics_exporter.renderers import render_devices_json, render_prometheus

EXPORTER = Path(__file__).parents[1] / "support/run_exporter.py"


class CliContractTest(unittest.TestCase):
    def test_one_shot_output_matches_library(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "sysfs"
            device = root / "0"
            device.mkdir(parents=True)
            (device / "architecture").write_text("wormhole\n")
            (device / "board_type").write_text("n300\n")
            (device / "memory_usage").write_text(
                "used_bytes 1 KiB\ntotal_bytes 4 KiB\n"
            )
            arguments = ["--sysfs-root", root, "--once"]
            devices = SysfsCollector(CollectorConfig(sysfs_root=root)).collect().devices
            for suffix, expected in (
                ([], render_prometheus(devices)),
                (["--json"], render_devices_json(devices)),
            ):
                process = subprocess.run([EXPORTER, *arguments, *suffix], capture_output=True)
                self.assertEqual(process.returncode, 0)
                self.assertEqual(process.stdout.decode(), expected)
                self.assertEqual(process.stderr, b"")

    def test_missing_critical_root_fails(self) -> None:
        missing = Path("/definitely/not/a/telemetry/root")
        result = subprocess.run(
            [EXPORTER, "--sysfs-root", missing, "--once"],
            stdout=subprocess.DEVNULL,
        )
        self.assertEqual(result.returncode, 1)


if __name__ == "__main__":
    unittest.main()
