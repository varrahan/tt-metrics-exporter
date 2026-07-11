#!/usr/bin/env python3

import json
import math
from pathlib import Path
import re
import unittest

import jsonschema

from tt_metrics_exporter.models import (
    CharacterDevice,
    DeviceTelemetry,
    MetaliumWorkloadTelemetry,
)
from tt_metrics_exporter.renderers import render_devices_json, render_prometheus


SCHEMA = Path(__file__).parents[2] / "docs/schema/telemetry.tenstorrent.com-v1.schema.json"
METRIC_NAME = re.compile(r"^[a-zA-Z_:][a-zA-Z0-9_:]*$")


def contract_devices() -> list[DeviceTelemetry]:
    device = DeviceTelemetry(
        id="0",
        sysfs_path=Path("/sys/class/tenstorrent/0"),
        architecture="wormhole",
        board_type="n300",
        health="ok",
        character_device=CharacterDevice(10, 0, "tenstorrent/0"),
    )
    device.pci.bdf = "0000:01:00.0"
    device.pci.driver = "tenstorrent"
    device.pci.numa_node = 0
    device.pci.iommu_group = 17
    device.firmware.heartbeat = 42
    device.firmware.thermal_trip_count = 0
    device.memory.used_bytes = 1024
    device.memory.total_bytes = 4096
    device.tensix.used = 1
    device.tensix.available = 79
    device.tensix.total = 80
    device.tensix.source = "metalium_profiler"
    device.health_detail.oom_fault_count = 0
    device.health_detail.hang_fault_count = 0
    device.janitor.scrub_count = 1
    device.janitor.reset_count = 0
    device.metalium_workloads = [
        MetaliumWorkloadTelemetry(
            workload_id=f"default/workload-{index}",
            pod_namespace="default",
            pod_name=f"worker-{index}",
            container_name="model",
            active=1,
            programs_observed=2,
            tensix_cores_used=index % 80,
            tensix_cores_total=80,
            sample_timestamp_seconds=1_800_000_000,
        )
        for index in range(1024)
    ]
    return [device]


class RendererContractTest(unittest.TestCase):
    def test_json_is_deterministic_complete_and_schema_valid(self) -> None:
        rendered = render_devices_json(contract_devices())
        self.assertEqual(rendered, render_devices_json(contract_devices()))
        self.assertTrue(rendered.startswith('{"apiVersion":"telemetry.tenstorrent.com/v1"'))
        self.assertTrue(rendered.endswith("\n"))
        document = json.loads(rendered)
        jsonschema.validate(document, json.loads(SCHEMA.read_text()))
        self.assertEqual(document["summary"]["devicesDiscovered"], 1)
        self.assertEqual(len(document["devices"][0]["metaliumWorkloads"]), 1024)

    def test_prometheus_is_deterministic_valid_and_bounded(self) -> None:
        rendered = render_prometheus(contract_devices())
        self.assertEqual(rendered, render_prometheus(contract_devices()))
        help_names, type_names, series = set(), {}, set()
        samples = 0
        for line in rendered.splitlines():
            if not line:
                continue
            if line.startswith("# HELP "):
                name = line.split(" ", 3)[2]
                self.assertRegex(name, METRIC_NAME)
                self.assertNotIn(name, help_names)
                help_names.add(name)
            elif line.startswith("# TYPE "):
                _, _, name, metric_type = line.split()
                self.assertIn(metric_type, ("counter", "gauge"))
                self.assertNotIn(name, type_names)
                type_names[name] = metric_type
            else:
                identity, value = line.rsplit(" ", 1)
                name = identity.split("{", 1)[0]
                self.assertRegex(name, METRIC_NAME)
                self.assertNotIn(identity, series)
                self.assertTrue(math.isfinite(float(value)))
                self.assertIn(name, help_names)
                self.assertIn(name, type_names)
                series.add(identity)
                samples += 1
        self.assertEqual(help_names, set(type_names))
        self.assertTrue(7000 < samples < 10000)
        self.assertIn(
            'tt_metalium_workload_core_occupancy_ratio{device="0",architecture="wormhole",board_type="n300",pci_bdf="0000:01:00.0",workload_id="default/workload-1",pod_namespace="default",pod_name="worker-1",container_name="model"} 0.0125',
            rendered,
        )


if __name__ == "__main__":
    unittest.main()
